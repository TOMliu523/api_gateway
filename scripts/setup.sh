#!/bin/bash

if [ "$EUID" -ne 0 ]; then
    exec sudo -E "$0" "$@"
fi

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

TARGET=api_gateway
BIN=${APP_DIR}/bin/
APP=${BIN}/${TARGET}
DEVBIND=${BIN}/dpdk-devbind.py

if [[ -z ${API_LIBYANG_PATH} ]]; then
    export API_LIBYANG_PATH=${APP_DIR}/conf/yang:${APP_DIR}/conf/yang/common
fi

if [[ -z ${SYSREPO_REPOSITORY_PATH} ]]; then
    export SYSREPO_REPOSITORY_PATH=${APP_DIR}/data
fi

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

nic_pci=(
    "0000:00:06.0"
    "0000:00:07.0"
)

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

echo "starting application ..."
echo "Executable: ${APP}"
echo "Library path: ${LD_LIBRARY_PATH}"
echo "Libyang path: ${API_LIBYANG_PATH}"
echo "Repository path: ${SYSREPO_REPOSITORY_PATH}"
echo "${APP} -l ${CPU_LIST} -n ${CHANEL}"

${APP} -l ${CPU_LIST} -n ${CHANEL}
#gdb ${APP}
