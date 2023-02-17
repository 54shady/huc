# HyperUpCall

## 代码实现原理

1. 虚拟设备

	qemu中实现一个虚拟设备(qemu/hw/misc/newdev.c) 作为TCP server
		该虚拟设备会创建本地socket用于给host和guest进行通信

2. 设备驱动(guest中使用驱动)

其对应的驱动是eBPF-injection/shared/driver/driver.c(安装在guest系统中)
guest中运行应用程序 eBPF-injection/shared/daemon_bpf/daemon_bpf.c 来使用这个虚拟设备
	应用程序通过ioctl来操作虚拟设备驱动
		daemon_bpf---ioctl--->driver

	虚拟设备驱动通过io操作来访问虚拟设备,虚拟设备中执行最终操作
		driver---iowrite--->device

3. 主机如何触发全流程

host中运行 eBPF-injection/host_interface/injectProgram.c (TCP client)向服务端发送数据(eBPF-injection/bpfProg/myprog.c)

代码执行流程

	wrapper-test.py
		test.py
			injectProgram.sh(run injectProgram)
				将bpfProg编译产生的文件通过网络发送到虚拟设备的缓存中

在guest中运行了守护程序daemon_bpf来读取虚拟设备的缓存
1. 将主机发送过来的bpfProg程序保存到本地并加载运行
2. 设置好cpu affinity后调用 ioctl(fd, IOCTL_SCHED_SETAFFINITY) 来设置cpu亲和性
		iowrite32(requested_cpu, bufmmio + NEWDEV_REG_SETAFFINITY); //eBPF-injection/shared/driver/driver.c
			newdev_bufmmio_write
				sched_setaffinity //qemu/hw/misc/newdev.c

## 编译代码(driver.ko, myprog.o, daemon_bpf)

	make all
	make kernel
	make daemon

------

bpf docker: /home/zeroway/github/kernel_drivers_examples/x86/bpf
使用ubuntu20.04 docker编译 daemon_bpf和myprog.c

compile kernel

	docker run --rm -it --privileged \
		--entrypoint=/code/compile-kernel.sh \
		-v $PWD:/code \
		-v $PWD/linux-5.4.0:/usr/src/linux bpf2004

compile driver

	docker run --rm -it --privileged \
		--entrypoint=/code/compile.sh \
		-v $PWD/eBPF-injection/shared/driver:/code \
		-v $PWD/linux-5.4.0:/usr/src/linux bpf2004

compile bytecode and daemon

	cp $PWD/eBPF-injection/Makefile $PWD/linux-5.4.0/samples/bpf/
	cp $PWD/eBPF-injection/shared/daemon_bpf/daemon_bpf.c $PWD/linux-5.4.0/samples/bpf/
	cp $PWD/eBPF-injection/shared/daemon_bpf/bpf_injection_msg.h $PWD/linux-5.4.0/samples/bpf/
	cp $PWD/eBPF-injection/bpfProg/myprog.c $PWD/linux-5.4.0/samples/bpf/

	docker run --rm -it --privileged \
		--entrypoint=/code/compile.sh \
		-v $PWD:/code \
		-v $PWD/linux-5.4.0:/usr/src/linux bpf2004

## 编译qemu

Config qemu

./configure \
	--target-list="x86_64-softmmu" \
	--enable-debug \
	--disable-docs \
	--disable-capstone \
	--disable-nettle \
	--disable-gnutls \
	--disable-gcrypt \
	--extra-cflags="-O0" \
	--enable-trace-backends=ftrace

	--with-git-submodules=ignore \

Comile qemu

	make all -j$(nproc) CONFIG_NEWDEV=y

	make all -j 9 CONFIG_NEWDEV=y CONFIG_VIRTFS=y CONFIG_VIRTIO_9P=y

Run qemu

/root/qemu-system-x86_64 \

/root/qemu/x86_64-softmmu/qemu-system-x86_64 \
        -serial mon:stdio \
        -drive file=/root/hyperupcall/ubt2004.qcow2,format=qcow2 \
        -enable-kvm -m 2G -smp 2 \
        -device e1000,netdev=ssh \
        -netdev user,id=ssh,hostfwd=tcp::2222-:22 \
		-vnc 0.0.0.0:0 \
        -device newdev

## 准备运行环境

- host and guest os:ubuntu 20.04 x86
- guest kernel version: linux-5.4.224
- enable guest root ssh enable and password to 0

## 测试

测试步骤:

1. 在guest中安装驱动

	tar xvf all.tar
	cd hyperUpCall/
	./insert_driver.sh
	./daemon_bpf

2. 在host上执行脚本

先安装依赖

	apt install -y python3-pip
	pip3 install invoke
	pip3 install fabric

执行测试脚本

	cd /root/eBPF-injection/test
	python3 wrapper-test.py --d 2 --v yes
