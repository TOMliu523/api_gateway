#!/bin/bash

set -x 

TOP=`pwd`
CDIR=cmake_build
INSTALL=${TOP}/release
BIN=${INSTALL}/bin

[ -d ${CDIR} ] && rm -rf ${CDIR}

mkdir ${CDIR}
pushd ${CDIR}

cmake .. -DCMAKE_INSTALL_PREFIX=${INSTALL}
make install

popd
