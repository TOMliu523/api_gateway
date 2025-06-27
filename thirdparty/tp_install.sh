#!/bin/bash

#1. The name of the compressed file needs to match the name of the directory where it is compressed plus the suffix .zip  (dirname.zip  --> unzip  --> dirname)
#2. It is best to keep the version number used in the zip name

set -ex

# The first parameter is the compressed file name
# The second parameter is the library name to be checked, avoiding multiple compilations
# The third parameter is the path where the source code is located
# Compile the command, using double quotes to form a parameter
function install_lib()
{
    local pack="$1"
    local lib="$2"
    local dirname=
    local exec_cmd=

    shift 2

    # Avoid secondary compilation while inventory is in place
    [[ -f ${lib} ]] && return

    # Check the file and set the unzip command
    if [[ ${pack} =~ \.zip$ ]]; then
        dirname=`basename ${pack} ".zip"`;
        exec_cmd="unzip ${pack}"
    elif [[ ${pack} =~ \.tar\.bz2$ ]]; then
        dirname=`basename ${pack} ".tar.bz2"`
        exec_cmd="tar -jxvf ${pack}"
    elif [[ ${pack} =~ \.tar\.xz ]]; then
        dirname=`basename ${pack} ".tar.xz"`
        exec_cmd="tar -xvJf ${pack}"
    elif [[ ${pack} =~ \.tar\.gz ]]; then
        dirname=`basename ${pack} ".tar.gz"`
        exec_cmd="tar -xzf ${pack}"
    fi

    # If the directory does not exist, unzip it
    [[ ! -d ${dirname} ]] && ${exec_cmd}

    # Compile
    pushd ${dirname} && for i in "$@" ;do echo "[DEBUG] Running command: $i";eval "$i"; done && popd

    # Delete source code path
    [[ -d ${dirname} ]] && rm -rf ${dirname}
}

# variable
PROC=8
CURDIR=`pwd`/`dirname $0`
INSTALL=${CURDIR}/install
[[ $# -eq 2 ]] && INSTALL="$1"

# Dependent apt-get -install -y pkgconf
export PKG_CONFIG_PATH=${INSTALL}/lib/pkgconfig:${PKG_CONFIG_PATH}
export LD_LIBRARY_PATH=${INSTALL}/lib:${LD_LIBRARY_PATH}

function install_cmake()
{
    zip_file=$1
    lib=$2

    install_lib \
        $zip_file \
        $lib \
        "mkdir -p build" \
        "cd build" \
        "cmake -DCMAKE_INSTALL_PREFIX=${INSTALL} .." \
        "make -j ${PROC}" \
        "make install"
}

function install_make()
{
    zip_file=$1
    lib=$2

    install_lib \
        $zip_file \
        $lib \
        "./configure --prefix=${INSTALL}" \
        "make -j ${PROC}" \
        "make install"
}

# Go to the third-party library directory
pushd $CURDIR

install_make jemalloc-5.3.0.tar.bz2 ${INSTALL}/lib/libjemalloc.so
install_cmake jansson-2.14.1.tar.bz2 ${INSTALL}/lib/libjansson.a
install_cmake libyang-3.12.2.zip ${INSTALL}/lib/libyang.so
install_cmake sysrepo-master.zip ${INSTALL}/lib/libsysrepo.so

install_lib \
    mongoose-master.zip \
    ${INSTALL}/lib/libmongoose.a \
    "gcc -c mongoose.c" \
    "ar -cr libmongoose.a mongoose.o" \
    "mkdir -p ${INSTALL}/include" \
    "mkdir -p ${INSTALL}/lib" \
    "install mongoose.h ${INSTALL}/include" \
    "install libmongoose.a ${INSTALL}/lib"

install_lib \
    LuaJIT-2.1.zip \
    ${INSTALL}/lib/libluajit-5.1.so \
    "make -j ${PROC}" \
    "make install DPREFIX=${INSTALL}"

install_lib \
    openssl-3.5.0.tar.gz \
    ${INSTALL}/lib64/libcrypto.so \
    "./config --prefix=${INSTALL}" \
    "make -j ${PROC}" \
    "make install"

install_lib \
    dpdk-stable-24.11.2.tar.xz \
    ${INSTALL}/lib/x86_64-linux-gnu/librte_ring.so \
    "meson setup build --prefix=${INSTALL} --default-library=static" \
    "cd build" \
    "ninja -j {PROC}" \
    "ninja install"

# Compilation complete, return
popd
