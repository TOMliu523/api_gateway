# API_GATEWAY

# API Gateway / DPDK L4 Load Balancer

A high-performance user-space L4 load balancer built with DPDK, focusing on
NUMA-aware dataplane design, lock-free per-core processing, fast routing/ARP
lookup, and REST/YANG-based control-plane configuration.

## Highlights

- DPDK-based packet I/O with poll-mode dataplane
- NUMA-aware resource allocation and per-core dataplane threads
- L2/L3/L4 forwarding pipeline: Ethernet / ARP / IPv4 / ICMP / TCP / UDP
- Route / ARP / session table abstraction for high-speed forwarding
- Control-plane and dataplane separation
- YANG/sysrepo-based configuration model
- RESTful management API
- CMake-based build system

## Architecture

```text
        REST / YANG / sysrepo
                 |
          Control Plane
                 |
        Config Snapshot / RCU
                 |
+----------------+----------------+
|                                 |
Dataplane Thread 0        Dataplane Thread N
RX Burst -> Parse -> Route/ARP -> NAT/LB -> TX Burst

# Environment

SYSTEM ubuntu 22.04  
OS 5.15.0  
DPDK 24.11.2  

# DPDK 24.11.2

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
GRUB_CMDLINE_LINUX="default_hugepagesz=1G hugepagesz=1G hugepages=48 isolcpus=8-23 nohz_full=8-23
rcu_nocbs=8-23 numa=off crashkernel=auto console=tty0 console=ttyS0,115200n8 iommu=pt intel_iommu=on"

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
├── scripts/        # Utility scripts for running, testing, or deploying
├── src/            # Source code directory
│   ├── api/        # Configuration interface module (e.g., config parsing, command handling)
│   ├── common/     # Common utilities (e.g., logging, helpers, shared logic)
│   ├── dpdk/       # DPDK wrapper functions (initialization, port handling, etc.)
│   ├── protocol/   # Network protocol logic implementation (L2–L7)
│   │   ├── l2.c      # Layer 2 protocol logic (e.g., Ethernet, ARP)
│   │   ├── l3.c      # Layer 3 protocol logic (e.g., IP, ICMP)
│   │   ├── l4.c      # Layer 4 protocol logic (e.g., TCP, UDP)
│   │   └── ...       # Extendable to L7 or custom application protocols
│   ├── dataplane/  # Data plane logic (threads, packet I/O, forwarding)
│   │   ├── dataplane.c  # Main loop and scheduling logic
│   │   └── ...       # Invokes protocol and uses configuration data
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
