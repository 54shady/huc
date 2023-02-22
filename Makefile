all:kernel driver daemon spscq affinity output

kernel:
	docker run --rm -it --privileged \
		--entrypoint=/code/compile-kernel.sh \
		-v ${PWD}:/code \
		-v ${PWD}/linux-5.4.0:/usr/src/linux bpf2004

driver:
	docker run --rm -it --privileged \
		--entrypoint=/code/compile.sh \
		-v ${PWD}/eBPF-injection/shared/driver:/code \
		-v ${PWD}/linux-5.4.0:/usr/src/linux bpf2004

daemon:
	cp ${PWD}/eBPF-injection/Makefile ${PWD}/linux-5.4.0/samples/bpf/
	cp ${PWD}/eBPF-injection/shared/daemon_bpf/daemon_bpf.c ${PWD}/linux-5.4.0/samples/bpf/
	cp ${PWD}/eBPF-injection/shared/daemon_bpf/daemon.c ${PWD}/linux-5.4.0/samples/bpf/
	cp ${PWD}/eBPF-injection/shared/daemon_bpf/bpf_injection_msg.h ${PWD}/linux-5.4.0/samples/bpf/
	cp ${PWD}/eBPF-injection/bpfProg/myprog.c ${PWD}/linux-5.4.0/samples/bpf/
	docker run --rm -it --privileged \
		--entrypoint=/code/compile.sh \
		-v ${PWD}:/code \
		-v ${PWD}/linux-5.4.0:/usr/src/linux bpf2004

	cp ${PWD}/linux-5.4.0/samples/bpf/daemon_bpf ${PWD}/eBPF-injection/shared/daemon_bpf/
	cp ${PWD}/linux-5.4.0/samples/bpf/daemon ${PWD}/eBPF-injection/shared/daemon_bpf/

host:
	make -C ${PWD}/eBPF-injection/host_interface/

spscq:
	docker run --rm -it --privileged \
		-v ${PWD}/eBPF-injection/shared/simplified-spscq:/code \
		bpf2004 make

affinity:
	docker run --rm -it --privileged \
		-v ${PWD}/eBPF-injection/shared/affinity_test:/code \
		bpf2004 make

output:
	cp ${PWD}/linux-5.4.0/samples/bpf/daemon_bpf ${PWD}/eBPF-injection/shared/daemon_bpf/
	cp ${PWD}/linux-5.4.0/samples/bpf/myprog.o ${PWD}/eBPF-injection/bpfProg/
