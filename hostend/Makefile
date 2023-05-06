default: all

all: bpf_injection_msg.h reset.c pinonsame.c injectByteCode.c
	gcc reset.c bpf_injection_msg.h -o reset
	gcc pinonsame.c bpf_injection_msg.h -o pinonsame
	gcc injectByteCode.c -o injectByteCode

clean:
	-rm -f socketmain reset pinonsame injectByteCode
