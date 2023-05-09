# TestDemo

## 1. Single read/write test

insert driver and run test app

	mount -t 9p -o trans=virtio,version=9p2000.L shared shared
	insmod /root/shared/guestend/driver/testdemo-drv.ko
	/root/shared/guestend/driver/mknoddev.sh testdemo
	/root/shared/guestend/rw-testdemo w /etc/fstab
	/root/shared/guestend/rw-testdemo r

## 2. Live migrate test

prepare image

	qemu-img create -f qcow2 -b ubt2004.qcow2 src.qcow2
	qemu-img create -f qcow2 -b ubt2004.qcow2 dst.qcow2

Run hostend/src.sh to boot src vm

	hostend/src.sh

in src guest, do write operation

	ssh -p 2222 root@localhost
	insmod testdemo-drv.ko
	./mknoddev.sh testdemo
	./rw-testdemo w /etc/fstab

Run hostend/dst.sh to boot vm with incoming parameter

	hostend/dst.sh

do migrate in src vm qemu monitor

	(qemu) migrate tcp:localhost:6666

check in dst vm guest(the driver module is already there)

	ssh -p 2223 root@localhost
	./rw-testdemo r
