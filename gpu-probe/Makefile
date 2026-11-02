# Azimuth_PS5 GPU probe: RADV (libvulkan.prx) from this project's own process.
#   make                          PS5 payload (PS5_PAYLOAD_SDK set)
#   make linux                    Linux x86-64 build (system Vulkan loader), to check the probe on a PC

ELF := azimuth-ps5-gpu-probe.elf
CFLAGS := -O2 -std=gnu11 -Wall -Wextra -Werror

ifdef PS5_PAYLOAD_SDK
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
endif

all: $(ELF)

$(ELF): main.c
ifndef PS5_PAYLOAD_SDK
	$(error PS5_PAYLOAD_SDK nao definido: instale o ps5-payload-sdk e exporte o caminho)
endif
	$(CC) $(CFLAGS) -o $@ main.c

linux: main.c
	cc $(CFLAGS) -o azimuth-ps5-gpu-probe-linux main.c -ldl -lpthread

clean:
	rm -f $(ELF) azimuth-ps5-gpu-probe-linux gpu-probe.txt gpu-stderr.txt

.PHONY: all linux clean
