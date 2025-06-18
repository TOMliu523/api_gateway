#!/bin/bash

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

TARGET=api_gateway
BIN=${APP_DIR}/bin/
APP=${BIN}/${TARGET}
DEVBIND=${BIN}/dpdk-devbind.py

if [[ -z ${API_LIBYANG_PATH} ]]; then
    export API_LIBYANG_PATH=${APP_DIR}/conf/yang
fi

if [[ -z ${SYSREPO_REPOSITORY_PATH} ]]; then
    export SYSREPO_REPOSITORY_PATH=${APP_DIR}/data
fi

export LD_LIBRARY_PATH=${APP_DIR}/lib:${APP_DIR}/lib64:${LD_LIBRARY_PATH}

# check process exists
pid=`pidof "${TARGET}"`
if [[ -n "${pid}" ]]; then
    kill -9 ${pid}
fi

# cpu list
CPU_LIST=`egrep -o isolcpus=[0-9,-]* /etc/default/grub | awk -F= '{print $2}'`
# memory channel
CHANEL=`dmidecode -t memory | awk '
            /Socket Locator/ {socket=$3}
            /Bank Locator/ {bank[bank_count++]=$3}
            /Size:/ && $2 ~ /[0-9]/ {used++}
            END {
            print used
        }'`

# use vfio
sudo modprobe vfio
sudo modprobe vfio-pci

# uninstall nic from dpdk
for dev in $(./release/bin/dpdk-devbind.py --status \
    | awk '/drv=(vfio-pci|igb_uio|uio_pci_generic)/ {print $1}')
do
    ${DEVBIND} --unbind="${dev}"
done

nic_pci=("0000:04:00.0"
         "0000:04:00.1"
         "0000:04:00.2"
         "0000:04:00.3")

for pci in "${nic_pci[@]}"
do
    ${DEVBIND} -b vfio-pci ${pci}
done

${DEVBIND} --status

echo "starting application ..."
echo "Executable: ${APP}"
echo "Library path: ${LD_LIBRARY_PATH}"
echo "Libyang path: ${API_LIBYANG_PATH}"
echo "Repository path: ${SYSREPO_REPOSITORY_PATH}"
echo "${APP} -l ${CPU_LIST} -n ${CHANEL}"

${APP} -l ${CPU_LIST} -n ${CHANEL}
