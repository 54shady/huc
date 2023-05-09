all:qemu kernel drv guest host

qemu:
	docker run --rm -it --privileged \
		-v ~/src/qemu:/code \
		huc
	cp ~/src/qemu/newdev-qemu.deb guestend/

kernel:
	docker run --rm -it --privileged \
		--entrypoint=/code/compile-kernel.sh \
		-v ${PWD}:/code \
		-v ${PWD}/linux-5.4.0:/usr/src/linux bpf2004

drv:
	docker run --rm -it --privileged \
		--entrypoint=/code/compile.sh \
		-v ${PWD}/guestend/driver:/code \
		-v ${PWD}/linux-5.4.0:/usr/src/linux bpf2004

guest:
	cp ${PWD}/guestend/daemon.c ${PWD}/linux-5.4.0/samples/bpf/
	cp ${PWD}/guestend/bpf_injection_msg.h ${PWD}/linux-5.4.0/samples/bpf/
	cp ${PWD}/guestend/bytecode.c ${PWD}/linux-5.4.0/samples/bpf/
	docker run --rm -it --privileged \
		--entrypoint=/code/compile.sh \
		-v ${PWD}:/code \
		-v ${PWD}/linux-5.4.0:/usr/src/linux bpf2004
	cp ${PWD}/linux-5.4.0/samples/bpf/daemon ${PWD}/guestend/
	cp ${PWD}/linux-5.4.0/samples/bpf/bytecode.o ${PWD}/guestend/
	docker run --rm -it --privileged \
		-v ${PWD}/guestend/simplified-spscq:/code \
		bpf2004 make
	docker run --rm -it --privileged \
		-v ${PWD}/guestend/affinity_test:/code \
		bpf2004 make

host:
	make -C ${PWD}/hostend/
