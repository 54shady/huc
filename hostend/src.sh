#!/usr/bin/env bash

qemu-system-x86_64 \
	-serial mon:stdio \
	-drive file=src.qcow2,format=qcow2 \
	-enable-kvm -m 1G -smp 2 \
	-device e1000,netdev=ssh -netdev user,id=ssh,hostfwd=tcp::2222-:22 \
	-vnc 0.0.0.0:0 \
	-device testdemo
