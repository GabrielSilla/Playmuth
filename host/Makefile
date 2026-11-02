# Azimuth_PS5 host.
#   make                          PS5 payload (PS5_PAYLOAD_SDK set)
#   make send PS5_HOST=<ip>       send it to the console's ELF loader
#   make linux                    Linux x86-64 build of the same code
#   make test-linux ENGINE=<azimuth.exe>
#   make test-gdi                 windows and GDI checks on Linux
#   make test-vulkan              vulkan-1.dll through its Windows x64 adapters, on Linux

PS5_HOST ?= ps5
PS5_PORT ?= 9021
ELF := azimuth-ps5-host.elf
LINUX := azimuth-ps5-host-linux
CXXFLAGS := -O2 -std=c++17 -Wall -Wextra -Werror -Ithird_party/vulkan
CFLAGS := -O2 -std=gnu11 -Wall -Wextra -Werror
# x86_64-sie-ps5 rejects ms_abi: guest_abi.c and gpu/vulkan_abi.c are compiled
# for FreeBSD, same CPU and System V ABI, and call only this project's code.
GUEST_ABI_FLAGS := -target x86_64-pc-freebsd12 -fPIC
# Vulkan 1.4.354 headers (third_party/vulkan/README.md) for the adapters.
VULKAN_INCLUDE := -Ithird_party/vulkan
SOURCES := src/main.cpp src/PeImage.cpp src/Image.cpp src/Teb.cpp src/Platform.cpp src/Sha256.cpp src/win32/Kernel32.cpp src/win32/LauncherApi.cpp src/win32/Locale.cpp src/win32/Exceptions.cpp src/win32/Handles.cpp src/win32/Files.cpp src/win32/Profile.cpp src/win32/Threads.cpp src/GuestThreads.cpp src/win32/Memory.cpp src/win32/WriteWatch.cpp src/win32/VectoredExceptions.cpp src/win32/User32.cpp src/win32/D3d12.cpp src/win32/Bcrypt.cpp src/win32/Gdi.cpp src/win32/Msvcrt.cpp src/win32/Advapi32.cpp src/win32/Misc.cpp src/gfx/Font.cpp src/gfx/Png.cpp src/gfx/VideoOut.cpp src/gpu/Radv.cpp src/gpu/Vulkan.cpp src/gpu/D3d12Check.cpp src/gpu/Dxgi.cpp src/gpu/SwapChain.cpp src/Modules.cpp src/HostHeap.cpp src/Input.cpp src/win32/Desktop.cpp src/win32/Audio.cpp src/win32/D3dCompiler.cpp src/win32/Wic.cpp src/win32/XAudio2.cpp src/win32/Xinput.cpp src/win32/Fibers.cpp src/win32/Dwrite.cpp
HEADERS := $(wildcard src/*.h src/win32/*.h src/win32/*.inc src/gfx/*.h src/gpu/*.h)
ABI_PS5 := build/ps5/guest_abi.o build/ps5/vulkan_abi.o build/ps5/dxgi_abi.o build/ps5/audio_abi.o build/ps5/wic_abi.o build/ps5/xaudio2_abi.o build/ps5/dwrite_abi.o build/ps5/gamepad_abi.o
ABI_LINUX := build/linux/guest_abi.o build/linux/vulkan_abi.o build/linux/dxgi_abi.o build/linux/audio_abi.o build/linux/wic_abi.o build/linux/xaudio2_abi.o build/linux/dwrite_abi.o build/linux/gamepad_abi.o

ifdef PS5_PAYLOAD_SDK
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
endif

all: $(ELF)

build/ps5/guest_abi.o: src/guest_abi.c src/guest_abi.h src/win32/exports.inc
ifndef PS5_PAYLOAD_SDK
	$(error PS5_PAYLOAD_SDK nao definido: instale o ps5-payload-sdk e exporte o caminho)
endif
	mkdir -p build/ps5
	$(CC) $(CFLAGS) $(GUEST_ABI_FLAGS) -c -o $@ src/guest_abi.c

build/ps5/vulkan_abi.o: src/gpu/vulkan_abi.c src/gpu/VulkanAbi.h src/gpu/vulkan_commands.inc
	mkdir -p build/ps5
	$(CC) $(CFLAGS) $(GUEST_ABI_FLAGS) $(VULKAN_INCLUDE) -c -o $@ src/gpu/vulkan_abi.c

build/ps5/dxgi_abi.o: src/gpu/dxgi_abi.c src/gpu/DxgiAbi.h
	mkdir -p build/ps5
	$(CC) $(CFLAGS) $(GUEST_ABI_FLAGS) -c -o $@ src/gpu/dxgi_abi.c

build/ps5/audio_abi.o: src/win32/audio_abi.c src/win32/AudioAbi.h
	mkdir -p build/ps5
	$(CC) $(CFLAGS) $(GUEST_ABI_FLAGS) -c -o $@ src/win32/audio_abi.c

build/ps5/wic_abi.o: src/win32/wic_abi.c src/win32/WicAbi.h
	mkdir -p build/ps5
	$(CC) $(CFLAGS) $(GUEST_ABI_FLAGS) -c -o $@ src/win32/wic_abi.c

build/ps5/xaudio2_abi.o: src/win32/xaudio2_abi.c src/win32/XAudio2Abi.h
	mkdir -p build/ps5
	$(CC) $(CFLAGS) $(GUEST_ABI_FLAGS) -c -o $@ src/win32/xaudio2_abi.c

build/ps5/dwrite_abi.o: src/win32/dwrite_abi.c src/win32/DwriteAbi.h
	mkdir -p build/ps5
	$(CC) $(CFLAGS) $(GUEST_ABI_FLAGS) -c -o $@ src/win32/dwrite_abi.c

build/ps5/gamepad_abi.o: src/win32/gamepad_abi.c src/win32/GamepadAbi.h
	mkdir -p build/ps5
	$(CC) $(CFLAGS) $(GUEST_ABI_FLAGS) -c -o $@ src/win32/gamepad_abi.c

$(ELF): $(SOURCES) $(HEADERS) $(ABI_PS5)
	$(CXX) $(CXXFLAGS) -o $@ $(SOURCES) $(ABI_PS5)

send: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<

build/linux/guest_abi.o: src/guest_abi.c src/guest_abi.h src/win32/exports.inc
	mkdir -p build/linux
	cc $(CFLAGS) -c -o $@ src/guest_abi.c

build/linux/vulkan_abi.o: src/gpu/vulkan_abi.c src/gpu/VulkanAbi.h src/gpu/vulkan_commands.inc
	mkdir -p build/linux
	cc $(CFLAGS) $(VULKAN_INCLUDE) -c -o $@ src/gpu/vulkan_abi.c

build/linux/dxgi_abi.o: src/gpu/dxgi_abi.c src/gpu/DxgiAbi.h
	mkdir -p build/linux
	cc $(CFLAGS) -c -o $@ src/gpu/dxgi_abi.c

build/linux/audio_abi.o: src/win32/audio_abi.c src/win32/AudioAbi.h
	mkdir -p build/linux
	cc $(CFLAGS) -c -o $@ src/win32/audio_abi.c

build/linux/wic_abi.o: src/win32/wic_abi.c src/win32/WicAbi.h
	mkdir -p build/linux
	cc $(CFLAGS) -c -o $@ src/win32/wic_abi.c

build/linux/xaudio2_abi.o: src/win32/xaudio2_abi.c src/win32/XAudio2Abi.h
	mkdir -p build/linux
	cc $(CFLAGS) -c -o $@ src/win32/xaudio2_abi.c

build/linux/dwrite_abi.o: src/win32/dwrite_abi.c src/win32/DwriteAbi.h
	mkdir -p build/linux
	cc $(CFLAGS) -c -o $@ src/win32/dwrite_abi.c

build/linux/gamepad_abi.o: src/win32/gamepad_abi.c src/win32/GamepadAbi.h
	mkdir -p build/linux
	cc $(CFLAGS) -c -o $@ src/win32/gamepad_abi.c

$(LINUX): $(SOURCES) $(HEADERS) $(ABI_LINUX)
	c++ $(CXXFLAGS) -o $@ $(SOURCES) $(ABI_LINUX) -lpthread -ldl

linux: $(LINUX)

test-linux: $(LINUX)
	./$(LINUX) $(ENGINE)

# Placeholders and section views (VirtualAlloc2, MapViewOfFile3)
build/linux/memory-test: tests/MemoryTest.cpp $(filter-out src/main.cpp,$(SOURCES)) $(HEADERS) $(ABI_LINUX)
	c++ $(CXXFLAGS) -o $@ tests/MemoryTest.cpp $(filter-out src/main.cpp,$(SOURCES)) $(ABI_LINUX) -lpthread -ldl

test-memory: build/linux/memory-test
	./build/linux/memory-test

# WIC through its Windows x64 vtables, on a PNG the host writes
build/linux/wic-test: tests/WicTest.cpp $(filter-out src/main.cpp,$(SOURCES)) $(HEADERS) $(ABI_LINUX)
	c++ $(CXXFLAGS) -o $@ tests/WicTest.cpp $(filter-out src/main.cpp,$(SOURCES)) $(ABI_LINUX) -lpthread -ldl

test-wic: build/linux/wic-test
	./build/linux/wic-test

# File handles beyond the descriptor limit (the descriptor cache)
build/linux/files-test: tests/FilesTest.cpp $(filter-out src/main.cpp,$(SOURCES)) $(HEADERS) $(ABI_LINUX)
	c++ $(CXXFLAGS) -o $@ tests/FilesTest.cpp $(filter-out src/main.cpp,$(SOURCES)) $(ABI_LINUX) -lpthread -ldl

test-files: build/linux/files-test
	./build/linux/files-test

# Windows and GDI checks plus a 1920x1080 test picture (fonts from /mnt/c/Windows/Fonts)
build/linux/gdi-test: tests/GdiTest.cpp $(filter-out src/main.cpp,$(SOURCES)) $(HEADERS) $(ABI_LINUX)
	c++ $(CXXFLAGS) -o $@ tests/GdiTest.cpp $(filter-out src/main.cpp,$(SOURCES)) $(ABI_LINUX) -lpthread -ldl

test-gdi: build/linux/gdi-test
	./build/linux/gdi-test build/linux/gdi-test.png

# vulkan-1.dll as a Windows module sees it, over the system's Vulkan (lavapipe in WSL)
build/linux/vulkan-test: tests/VulkanTest.cpp $(filter-out src/main.cpp,$(SOURCES)) $(HEADERS) $(ABI_LINUX)
	c++ $(CXXFLAGS) $(VULKAN_INCLUDE) -o $@ tests/VulkanTest.cpp $(filter-out src/main.cpp,$(SOURCES)) $(ABI_LINUX) -lpthread -ldl

test-vulkan: build/linux/vulkan-test
	./build/linux/vulkan-test

clean:
	rm -rf build $(ELF) $(LINUX) host.txt

.PHONY: all send linux test-linux test-gdi test-memory test-wic test-files test-vulkan clean

# Wait semantics and concurrent address wakeups.
build/linux/threads-test: tests/ThreadsTest.cpp $(filter-out src/main.cpp,$(SOURCES)) $(HEADERS) $(ABI_LINUX)
	c++ $(CXXFLAGS) -o $@ tests/ThreadsTest.cpp $(filter-out src/main.cpp,$(SOURCES)) $(ABI_LINUX) -lpthread -ldl

test-threads: build/linux/threads-test
	./build/linux/threads-test

.PHONY: test-threads

build/linux/writewatch-test: tests/WriteWatchTest.cpp $(filter-out src/main.cpp,$(SOURCES)) $(HEADERS) $(ABI_LINUX)
	c++ $(CXXFLAGS) -o $@ tests/WriteWatchTest.cpp $(filter-out src/main.cpp,$(SOURCES)) $(ABI_LINUX) -lpthread -ldl -Wl,--wrap=mprotect

test-writewatch: build/linux/writewatch-test
	./build/linux/writewatch-test
	AZIMUTH_BLOCK16K=1 ./build/linux/writewatch-test
.PHONY: test-writewatch

build/linux/vectored-test: tests/VectoredTest.cpp $(filter-out src/main.cpp,$(SOURCES)) $(HEADERS) $(ABI_LINUX)
	c++ $(CXXFLAGS) -o $@ tests/VectoredTest.cpp $(filter-out src/main.cpp,$(SOURCES)) $(ABI_LINUX) -lpthread -ldl

test-vectored: build/linux/vectored-test
	./build/linux/vectored-test
	AZIMUTH_BLOCK16K=1 ./build/linux/vectored-test
.PHONY: test-vectored

build/linux/frame-intervals-test: tests/FrameIntervalsTest.cpp src/gpu/FrameIntervals.h
	c++ $(CXXFLAGS) -o $@ tests/FrameIntervalsTest.cpp

test-frame-intervals: build/linux/frame-intervals-test
	./build/linux/frame-intervals-test
.PHONY: test-frame-intervals

# Coalesce only the known repeated XMA diagnostic, retaining exact counts.
build/linux/repeated-audio-log-test: tests/RepeatedAudioLogTest.cpp src/win32/RepeatedAudioLog.h
	mkdir -p build/linux
	c++ $(CXXFLAGS) -o $@ tests/RepeatedAudioLogTest.cpp -lpthread

test-repeated-audio-log: build/linux/repeated-audio-log-test
	./build/linux/repeated-audio-log-test
.PHONY: test-repeated-audio-log

build/linux/handles-test: tests/HandlesTest.cpp src/win32/Handles.cpp src/win32/Win32.h
	mkdir -p build/linux
	c++ $(CXXFLAGS) -o $@ tests/HandlesTest.cpp src/win32/Handles.cpp -lpthread

test-handles: build/linux/handles-test
	./build/linux/handles-test
.PHONY: test-handles

build/linux/frame-window-test: tests/FrameWindowTest.cpp src/gpu/FrameWindow.h
	mkdir -p build/linux
	c++ $(CXXFLAGS) -o $@ tests/FrameWindowTest.cpp

test-frame-window: build/linux/frame-window-test
	./build/linux/frame-window-test
.PHONY: test-frame-window

build/linux/direct-memory-release-test: tests/DirectMemoryReleaseTest.cpp src/win32/DirectMemoryRelease.h
	mkdir -p build/linux
	c++ $(CXXFLAGS) -o $@ tests/DirectMemoryReleaseTest.cpp

test-direct-memory-release: build/linux/direct-memory-release-test
	./build/linux/direct-memory-release-test
.PHONY: test-direct-memory-release
