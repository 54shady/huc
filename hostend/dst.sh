#!/usr/bin/env bash

qemu-system-x86_64 \
	-serial mon:stdio \
	-drive file=dst.qcow2,format=qcow2 \
	-enable-kvm -m 1G -smp 2 \
	-device e1000,netdev=ssh -netdev user,id=ssh,hostfwd=tcp::2223-:22 \
	-vnc 0.0.0.0:1 \
	-device testdemo \
	-incoming tcp:0:6666
