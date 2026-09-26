#!/bin/bash

if [ "$EUID" -ne 0 ]; then
    exec sudo -E "$0" "$@"
fi

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

TARGET=api_gateway
BIN=${APP_DIR}/bin/
APP=${BIN}/${TARGET}
DEVBIND=${BIN}/dpdk-devbind.py
MODULE_FILE=${APP_DIR}/conf/yang/v1.yang

export SYSREPO_REPOSITORY_PATH=${APP_DIR}/data
export API_LIBYANG_PATH=${APP_DIR}/conf/yang:${APP_DIR}/conf/yang/common
export LD_LIBRARY_PATH=${APP_DIR}/lib:${APP_DIR}/lib64:${LD_LIBRARY_PATH}

# check process exists
pid=`pidof "${TARGET}"`
if [[ -n "${pid}" ]]; then
    kill -9 ${pid}
    sleep 3
fi

# cpu list
CPU_LIST=`grep -E -o isolcpus=[0-9,-]* /etc/default/grub | awk -F= '{print $2}'`
# memory channel
CHANEL=`dmidecode -t memory | awk '
            /Socket Locator/ {socket=$3}
            /Bank Locator/ {bank[bank_count++]=$3}
            /Size:/ && $2 ~ /[0-9]/ {used++}
            END {
            print used
        }'`

# Usage: config_init /path/to/v1.yang
# Prints one PCI address per line. Requires jq.
function config_init()
{
    local yang_file=$1
    local modules name flags installed=0 data

    modules=$("${BIN}/sysrepoctl" -l) || return 1

    while IFS='|' read -r name _ flags _; do
        if [[ $name =~ ^[[:space:]]*v1[[:space:]]*$ &&
              $flags =~ ^[[:space:]]*I[[:space:]]*$ ]]; then
            installed=1
            break
        fi
    done <<< "$modules"

    if (( ! installed )); then
        # Install the module and search its directory for submodules.
        "${BIN}/sysrepoctl" -i "$yang_file" \
            -s "$(dirname "$yang_file")" >/dev/null || return 1

        # Initialize the startup datastore from inline JSON.
        "${BIN}/sysrepocfg" --import \
            --datastore startup --module v1 --format json >/dev/null <<'JSON' || return 1
{
    "v1:boot" : {
        "nic-pci": [
            "0000:00:06.0",
            "0000:00:07.0"
        ],
        "listener": {
            "ipv4": {
                "address": "0.0.0.0",
                "http-port": 8080,
                "https-port": 8443
            }
        }
    }
}
JSON

        # The application reads SR_DS_RUNNING, so initialize it as well.
        "${BIN}/sysrepocfg" --copy-from startup \
            --datastore running --module v1 >/dev/null || return 1
    fi

    data=$("${BIN}/sysrepocfg" --export \
        --datastore startup --module v1 --format json) || return 1

    #jq -r '.["v1:boot:nic-pci"][]?' <<< "$data"
    jq -er '
        .["v1:boot"]["nic-pci"] as $pci
        | if ($pci | type) == "array" and ($pci | length) > 0
        then $pci[]
        else error("nic-pci is missing or empty")
        end
    ' <<< "$data"
}

if ! pci_output=$(config_init "${MODULE_FILE}"); then
    echo "config_init failed" >&2
    exit 1
fi

mapfile -t nic_pci <<< "$pci_output"

declare -p nic_pci
# Output: declare -a nic_pci=([0]="0000:00:06.0" [1]="0000:00:07.0")
#nic_pci=(
#    "0000:00:06.0"
#    "0000:00:07.0"
#)

NO_IOMMU=""

#
# Check PCI devices and IOMMU support.
#
for dev in "${nic_pci[@]}"; do
    pci_path="/sys/bus/pci/devices/${dev}"

    if [ ! -d "$pci_path" ]; then
        echo "ERROR: PCI device $dev does not exist"
        exit 1
    fi

    if [ ! -L "$pci_path/iommu_group" ]; then
        echo "$dev: no IOMMU group"
        NO_IOMMU="--noiommu-mode"
    else
        echo "$dev: IOMMU group $(basename "$(readlink -f "$pci_path/iommu_group")")"
    fi
done

#
# Check whether all configured NICs are already bound to vfio-pci.
#
need_bind=0

for dev in "${nic_pci[@]}"; do
    driver_path="/sys/bus/pci/devices/${dev}/driver"

    if [ -L "$driver_path" ]; then
        driver="$(basename "$(readlink -f "$driver_path")")"
    else
        driver="none"
    fi

    echo "$dev: driver=$driver"

    if [ "$driver" != "vfio-pci" ]; then
        need_bind=1
    fi
done

#
# Bind only when necessary.
#
if (( need_bind )); then
    echo "Bind PCI devices..."

    modprobe vfio
    modprobe vfio-pci

    #
    # Enable VFIO no-IOMMU mode if required.
    #
    if [ "$NO_IOMMU" = "--noiommu-mode" ]; then
        noiommu_path="/sys/module/vfio/parameters/enable_unsafe_noiommu_mode"

        if [ ! -e "$noiommu_path" ]; then
            echo "ERROR: VFIO no-IOMMU mode is not supported"
            exit 1
        fi

        echo 1 > "$noiommu_path"
    fi

    #
    # Detach configured NICs from Linux network stack.
    #
    for pci in "${nic_pci[@]}"; do
        net_path="/sys/bus/pci/devices/${pci}/net"

        if [ -d "$net_path" ]; then
            for nic_path in "$net_path"/*; do
                [ -e "$nic_path" ] || continue

                nic="$(basename "$nic_path")"

                echo "Detach $nic from Linux network stack"

                ip link set "$nic" down 2>/dev/null || true
                ip addr flush dev "$nic" 2>/dev/null || true
                ip route flush dev "$nic" 2>/dev/null || true
            done
        fi
    done

    #
    # Bind configured NICs to vfio-pci.
    #
    for pci in "${nic_pci[@]}"; do
        driver_path="/sys/bus/pci/devices/${pci}/driver"

        if [ -L "$driver_path" ]; then
            driver="$(basename "$(readlink -f "$driver_path")")"
        else
            driver="none"
        fi

        if [ "$driver" != "vfio-pci" ]; then
            echo "Bind $pci to vfio-pci"
            "${DEVBIND}" $NO_IOMMU -b vfio-pci "$pci"
        fi
    done
else
    echo "PCI devices are already bound to vfio-pci"
fi

echo "SYSREPO_REPOSITORY_PATH: $SYSREPO_REPOSITORY_PATH"
echo "API_LIBYANG_PATH: $API_LIBYANG_PATH"
echo "LD_LIBRARY_PATH: $LD_LIBRARY_PATH"
echo "starting application ..."
echo "Executable: ${APP}"
echo "Library path: ${LD_LIBRARY_PATH}"
echo "Libyang path: ${API_LIBYANG_PATH}"
echo "Repository path: ${SYSREPO_REPOSITORY_PATH}"
echo "${APP} -l ${CPU_LIST} -n ${CHANEL}"

exec "${APP}" -l "${CPU_LIST}" -n "${CHANEL}"
#exec gdb ${APP}
