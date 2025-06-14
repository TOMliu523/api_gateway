# API_GATEWAY

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

# Install Dependency
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

# System Config
You only need to update “GRUB_CMDLINE_LINUX”
Intel CPU: intel_iommu=on
AMD CPU: amd_iommu=on
```
# cat /etc/default/grub

GRUB_DEFAULT=0
GRUB_TIMEOUT_STYLE=hidden
GRUB_TIMEOUT=10
GRUB_DISTRIBUTOR=`lsb_release -i -s 2> /dev/null || echo Debian`
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash"
GRUB_CMDLINE_LINUX="default_hugepagesz=1G hugepagesz=1G hugepages=32 isolcpus=12-23 nohz_full=12-23
rcu_nocbs=12-23 numa=off crashkernel=auto console=tty0 console=ttyS0,115200n8 iommu=pt intel_iommu=on"

# update-grub
# reboot
```

# Codebase Overview
```
.
├── build.sh        # Script for building and installing the entire project
├── CMakeLists.txt  # Main CMake build configuration file
├── conf/           # Configuration files
├── document/       # Design documents and technical specifications
├── .gitignore      # Git ignore rules
├── inc/            # Header files shared across modules
├── plugin/         # Optional plugin modules
├── README.md       # Project overview and instructions
├── release/        # Build outputs or release artifacts
├── scripts/        # Utility scripts for running, testing, or deploying
├── src/            # Source code directory
│   ├── api/        # Configuration interface module (e.g., config parsing, command handling)
│   ├── common/     # Common utilities (e.g., logging, helpers, shared logic)
│   ├── dpdk/       # DPDK wrapper functions (initialization, port handling, etc.)
│   ├── dataplane/  # Data plane processing (L2–L7 protocol stack logic)
│   │   ├── thread.c  # Threading framework and scheduling
│   │   ├── l2.c      # Layer 2 protocol handling
│   │   ├── l3.c      # Layer 3 protocol handling (e.g., IP)
│   │   ├── l4.c      # Layer 4 protocol handling (e.g., TCP)
│   │   └── ...       # Extendable to Layer 7 or application protocols
│   └── main.c      # Main program entry point
└── thirdparty/     # External dependencies or prebuilt third-party libraries
```

# Compile Command
```
./build.sh

# or
# mkdir -p build
# cd build
# cmake -DCMAKE_INSTALL_PREFIX=`pwd`/../release ..
# make -j 4
# make install
```

# Running
```
./release/setup.sh
```
