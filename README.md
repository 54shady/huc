# HyperUpCall

## 准备运行环境

- host 20.04 or 22.04 and guest 20.04
- guest ubuntu 20.04 kernel version: linux-5.4.0
- enable guest root ssh enable and password to 0

## 代码实现原理

虚拟设备

	qemu中实现一个虚拟设备(qemu/hw/misc/hucdev.c) 作为TCP server
		该虚拟设备会创建本地socket用于给host和guest进行通信

设备驱动(guest中使用驱动)

	其对应的驱动是guestend/driver/huc-driver.c(安装在guest系统中)
	guest中运行应用程序 guestend/daemon.c 来使用这个虚拟设备
		应用程序通过ioctl来操作虚拟设备驱动
			daemon---ioctl--->driver

		虚拟设备驱动通过io操作来访问虚拟设备,虚拟设备中执行最终操作
			driver---iowrite--->device

主机如何触发全流程

	host中运行 hostend/injectByteCode.c (TCP client)向服务端发送数据(guestend/bytecode.c编译出来的bytecode)

代码执行流程

	wrapper-test.py
		test.py
			injectProgram.sh(run injectProgram)
				1. 将bytecode.c编译产生的bpf bytecode通过网络发送到虚拟设备的缓存中
				2. 虚拟设备根据bitecode的header中的type类型来判断需要如何处理
					虚拟设备发送中断给guest来通知收取bpf bitecode
					中断中通过irq_status来告知guest driver是什么中断
				3. driver中断处理函数进行处理
					中断唤醒等待队列上的程序read函数
				4. daemon一启动就会调用系统调用read等待驱动返回数据
					加载bpf bitecode到内核后,等待bpfmap的更新

					daemon等待bpfmap数据有变动后,进行bpf map update, 再调用ioctl set_affinity
				5. 回到驱动中处理daemon的ioctl请求
					iowrite32对设备进行写操作,命令码NEWDEV_REG_SETAFFINITY
				6. 虚拟设备处理该写操作请求
					对应的set_affinity,在虚拟设备中进行系统调用,来实现
					if (sched_setaffinity(cpu->thread_id, SET_SIZE, set) == -1){

			远程触发guest中的脚本python3 affinity_test.py
			将会在guest中运行spscq
			spscq的运行会导致guest中调用到内核的sched_setaffinity
			此时就会进入到bytecode中运行对应的钩子程序bpf_prog1


在guest中运行了守护程序daemon来读取虚拟设备的缓存

	将主机发送过来的bytecode程序保存到本地并加载运行
	设置好cpu affinity后调用 ioctl(fd, IOCTL_SCHED_SETAFFINITY) 来设置cpu亲和性
		iowrite32(requested_cpu, bufmmio + NEWDEV_REG_SETAFFINITY); // guestend/driver/huc-driver.c
			hucdev_bufmmio_write
				sched_setaffinity //qemu/hw/misc/hucdev.c

## 准备qemu代码

checkout qemu code

	git checkout v5.0.0-rc4 -b v5p0p0rc4
	git apply ${HOME}/github/huc/qemu-patch/*.patch
	git config --global --add safe.directory /code
	git submodule update

compile qemu

	make qemu

Run qemu

	/usr/local/bin/qemu-system-x86_64 \
			-serial mon:stdio \
			-drive file=ubt2004.qcow2,format=qcow2 \
			-enable-kvm -m 2G -smp 2 \
			-device e1000,netdev=ssh \
			-netdev user,id=ssh,hostfwd=tcp::2222-:22 \
			-vnc 0.0.0.0:0 \
			-virtfs local,id=sfs,path=/root/huc,security_model=passthrough,mount_tag=shared \
			-device newdev -device hucdev

## 准备内核代码(tag 5.4)

	cd ${HOME}/src/linux
	git checkout v5.4 -b v5p4
	patch -p1 < ${HOME}/github/huc/guestend/0001-Patch-huc-daemon.patch
	cd ${HOME}/github/huc
	make kernel

## 编译代码

	make all
	make qemu
	make kernel
	make drv
	make guest
	make host

编译完内核后,在容器中make install安装内核到容器的/boot

	tar cvf boot.tar /boot/*

然后将这个boot.tar解压到guest中即可更新guest内核
或者直接替换arch/x86_64/boot/bzImage为/boot/vmlinuz然后更新grub

## 测试

测试步骤:

1. 在测试主机上新建目录/root/huc并将(guestend, hostend都拷贝到该目录下)

2. 启动虚拟机

3. 在guest中先挂载目录后安装驱动

	mount -t 9p -o trans=virtio,version=9p2000.L shared shared
	insmod /root/shared/guestend/driver/huc-driver.ko
	/root/shared/guestend/driver/mknoddev.sh hucdev
	/root/shared/guestend/daemon

在host上执行脚本

先安装依赖

	apt install -y python3-pip
	pip3 install invoke
	pip3 install fabric

4. 执行测试脚本

	cd /root/huc/hostend/test
	python3 wrapper-test.py --d 2 --v yes
