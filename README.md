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

# system config
You only need to update “GRUB_CMDLINE_LINUX_DEFAULT”
```
# cat /etc/default/grub

GRUB_DEFAULT=0
GRUB_TIMEOUT_STYLE=hidden
GRUB_TIMEOUT=10
GRUB_DISTRIBUTOR=`lsb_release -i -s 2> /dev/null || echo Debian`
GRUB_CMDLINE_LINUX_DEFAULT="default_hugepagesz=1G hugepagesz=1G hugepages=32 isolcpus=11-23 nohz_full=11-23
rcu_nocbs=11-23 numa=off crashkernel=auto console=tty0 console=ttyS0,115200n8 iommu=off"
GRUB_CMDLINE_LINUX=""

# update-grub
# reboot
```

# compile command
```
mkdir -p build
cd build
cmake -DCMAKE_INSTALL_PREFIX=`pwd`/../release ..
make -j 4
make install
```
