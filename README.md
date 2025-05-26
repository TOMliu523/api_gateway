# api_gateway
 
SYSTEM ubuntu 22.04   
OS 5.15.0     
DPDK 24.11.2  

# DPDK24.11.2

C11(GCC 5.0+)  
Clang(3.6+)   
python(3.6+)   
meson(0.53.2+)   
pyelftools(0.22+)  
pkg-config or pkgconf  

# Install dependency
```
apt -y install cmake
apt -y install zip
apt -y install libnuma-dev
apt -y install libpcre2-dev
apt -y install meson ninja-build
apt -y install pkgconf
apt -y install python3-pip
pip3 install pyelftools
```

# compile command
```
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=`pwd`/.install ..
make -j 4
make install
```
