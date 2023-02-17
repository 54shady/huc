#!/usr/bin/env bash

# LOCALVERSION set to null to avoid the annoying '+' sign

cd /usr/src/linux
chmod +x scripts/*
make clean
make defconfig
./scripts/config -e CONFIG_FTRACE
./scripts/config -e CONFIG_DEBUG_INFO
./scripts/config -e CONFIG_DEBUG_INFO_DWARF5
./scripts/config -e CONFIG_BPF_SYSCALL
./scripts/config -e CONFIG_DEBUG_INFO_BTF
./scripts/config -d CONFIG_DEBUG_INFO_REDUCED
yes "" | make oldconfig
make headers_install
make -j$(nproc) \
	LOCALVERSION="" \
	KERNEL_DIR=/usr/src/linux \
	BPF_TOOLS_PATH=bpf_tools \
	BPF_EXAMPLES_PATH=bpf_examples \
	CC=/usr/bin/gcc
