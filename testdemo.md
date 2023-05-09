TestDemo

	mount -t 9p -o trans=virtio,version=9p2000.L shared shared
	insmod /root/shared/guestend/driver/testdemo-drv.ko
	/root/shared/guestend/driver/mknoddev.sh testdemo
	/root/shared/guestend/rw-testdemo
