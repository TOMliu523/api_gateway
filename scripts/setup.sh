#!/bin/bash

TARGET=api_gateway
APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN=${APP_DIR}/bin/${TARGET}

if [[ -z ${API_LIBYANG_PATH} ]]; then
    export API_LIBYANG_PATH=${APP_DIR}/conf/yang
fi

if [[ -z ${SYSREPO_REPOSITORY_PATH} ]]; then
    export SYSREPO_REPOSITORY_PATH=${APP_DIR}/data
fi

export LD_LIBRARY_PATH=${APP_DIR}/lib:${APP_DIR}/lib64:${LD_LIBRARY_PATH}

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

echo "starting application ..."
echo "Executable: ${BIN}"
echo "Library path: ${LD_LIBRARY_PATH}"
echo "Repository path: ${SYSREPO_REPOSITORY_PATH}"
echo "${BIN} -l ${CPU_LIST} -n ${CHANEL}"

${BIN} -l ${CPU_LIST} -n ${CHANEL}
