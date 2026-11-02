# Azimuth_PS5 viability probe.
#   make                          PS5 payload (PS5_PAYLOAD_SDK set)
#   make send PS5_HOST=<ip>       send it to the console's ELF loader
#   make linux                    Linux x86-64 build, to check the probe on a PC

PS5_HOST ?= ps5
PS5_PORT ?= 9021
ELF := azimuth-ps5-probe.elf
CFLAGS := -O2 -std=gnu11 -Wall -Wextra -Werror
# x86_64-sie-ps5 rejects ms_abi: msabi.c is compiled for FreeBSD, same CPU
# and System V ABI, and calls only this project's code (see msabi.c).
MSABI_FLAGS := -target x86_64-pc-freebsd12 -fPIC

ifdef PS5_PAYLOAD_SDK
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
endif

all: $(ELF)

msabi.o: msabi.c msabi.h
ifndef PS5_PAYLOAD_SDK
	$(error PS5_PAYLOAD_SDK nao definido: instale o ps5-payload-sdk e exporte o caminho)
endif
	$(CC) $(CFLAGS) $(MSABI_FLAGS) -c -o $@ msabi.c

$(ELF): main.c msabi.h msabi.o
	$(CC) $(CFLAGS) -o $@ main.c msabi.o

send: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<

linux: main.c msabi.c msabi.h
	cc $(CFLAGS) -o azimuth-ps5-probe-linux main.c msabi.c -lpthread

clean:
	rm -f $(ELF) msabi.o azimuth-ps5-probe-linux probe.txt

.PHONY: all send linux clean
