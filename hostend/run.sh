#!/usr/bin/env bash

qemu-system-x86_64 \
	-serial mon:stdio \
	-drive file=/root/base/huc.qcow2,format=qcow2 \
	-enable-kvm -m 2G -smp 2 \
	-device e1000,netdev=ssh \
	-netdev user,id=ssh,hostfwd=tcp::2222-:22 \
	-vnc 0.0.0.0:0 \
	-virtfs local,id=sfs,path=/root/huc,security_model=passthrough,mount_tag=shared \
	-device testdemo
