# Native title; run from Linux/WSL. The payload builds stay in ../host and ../gpu-probe.
.DEFAULT_GOAL := all
PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
export PS5_PAYLOAD_SDK

all:
	bash build.sh

test-tls:
	bash tests/run-tls.sh

test-gpu:
	bash tests/run-gpu-linux.sh

verify:
	python3 check_runtime.py build/llvm-pie.elf
	python3 verify.py build/eboot.elf dist/FAKE78002/eboot.bin
	.deps/native-foundation/build/runtime-shim/ps5-native-tool self --inspect --file dist/FAKE78002/eboot.bin > build/self-inspection.txt
	grep -qx 'integrity: valid' build/self-inspection.txt

.PHONY: all verify test-tls test-gpu
