default: all

all: injectProgram.c bpf_injection_msg.h reset.c pinonsame.c injectByteCode.c
	gcc injectProgram.c bpf_injection_msg.h -o injectProgram
	gcc reset.c bpf_injection_msg.h -o reset
	gcc pinonsame.c bpf_injection_msg.h -o pinonsame
	gcc injectByteCode.c -o injectByteCode

clean:
	-rm -f socketmain reset pinonsame injectProgram injectByteCode
