all:qemu kernel drv guest host

qemu:
	docker run --rm -it --privileged \
		-v ~/src/qemu:/code \
		huc
	cp ~/src/qemu/newdev-qemu.deb guestend/

kernel:
	docker run --rm -it --privileged \
		--entrypoint=/code/compile-kernel.sh \
		-v ${PWD}/script/compile-kernel.sh:/code/compile-kernel.sh \
		-v ${HOME}/src/linux:/usr/src/linux bpf2004

drv:
	docker run --rm -it --privileged \
		--entrypoint=/code/compile.sh \
		-v ${PWD}/guestend/driver:/code \
		-v ${HOME}/src/linux:/usr/src/linux bpf2004

guest:
	cp ${PWD}/guestend/daemon.c ${HOME}/src/linux/samples/bpf/
	cp ${PWD}/guestend/bpf_injection_msg.h ${HOME}/src/linux/samples/bpf/
	cp ${PWD}/guestend/bytecode.c ${HOME}/src/linux/samples/bpf/
	cp ${PWD}/guestend/rw-testdemo.c ${HOME}/src/linux/samples/bpf/
	docker run --rm -it --privileged \
		--entrypoint=/code/compile.sh \
		-v ${PWD}/script/compile.sh:/code/compile.sh \
		-v ${HOME}/src/linux:/usr/src/linux bpf2004
	cp ${HOME}/src/linux/samples/bpf/daemon ${PWD}/guestend/
	cp ${HOME}/src/linux/samples/bpf/bytecode.o ${PWD}/guestend/
	cp ${HOME}/src/linux/samples/bpf/rw-testdemo ${PWD}/guestend/
	docker run --rm -it --privileged \
		-v ${PWD}/guestend/simplified-spscq:/code \
		bpf2004 make
	docker run --rm -it --privileged \
		-v ${PWD}/guestend/affinity_test:/code \
		bpf2004 make

host:
	make -C ${PWD}/hostend/
