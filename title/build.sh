#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project=$(dirname -- "$root")
sdk=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}
export PS5_PAYLOAD_SDK="$sdk"
build="$root/build"
dist="$root/dist/FAKE78002"
mkdir -p "$build/obj" "$dist/sce_sys" "$dist/sce_module"
bash "$root/prepare.sh"
foundation="$root/.deps/native-foundation"
native="$foundation/tooling/native"
tool="$foundation/build/runtime-shim/ps5-native-tool"
[[ -x $tool && -x $sdk/bin/prospero-clang++ ]] || {
    echo "Native converter or SDK missing" >&2; exit 2;
}
cc="$sdk/bin/prospero-clang"
cxx="$sdk/bin/prospero-clang++"
flags=(-O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -DAZIMUTH_TITLE=1)
"$cxx" "${flags[@]}" -std=c++17 -c "$native/app_crt.cpp" -o "$build/obj/crt.o"
"$cxx" "${flags[@]}" -std=c++17 -c "$root/main.cpp" -o "$build/obj/title.o"
"$cc" "${flags[@]}" -std=gnu11 -c "$root/runtime/emutls.c" -o "$build/obj/emutls.o"
"$cc" "${flags[@]}" -std=gnu11 -Dmain=azimuthGpuMain -c "$project/gpu-probe/main.c" -o "$build/obj/gpu.o"
"$cc" -O2 -std=gnu11 -Wall -Wextra -Werror -fPIC -target x86_64-pc-freebsd12 \
    -c "$project/host/src/guest_abi.c" -o "$build/obj/guest_abi.o"
"$cc" -O2 -std=gnu11 -Wall -Wextra -Werror -fPIC -target x86_64-pc-freebsd12 \
    -I"$project/host/third_party/vulkan" \
    -c "$project/host/src/gpu/vulkan_abi.c" -o "$build/obj/vulkan_abi.o"
"$cc" -O2 -std=gnu11 -Wall -Wextra -Werror -fPIC -target x86_64-pc-freebsd12 \
    -c "$project/host/src/gpu/dxgi_abi.c" -o "$build/obj/dxgi_abi.o"
"$cc" -O2 -std=gnu11 -Wall -Wextra -Werror -fPIC -target x86_64-pc-freebsd12 \
    -c "$project/host/src/win32/audio_abi.c" -o "$build/obj/audio_abi.o"
"$cc" -O2 -std=gnu11 -Wall -Wextra -Werror -fPIC -target x86_64-pc-freebsd12 \
    -c "$project/host/src/win32/wic_abi.c" -o "$build/obj/wic_abi.o"
"$cc" -O2 -std=gnu11 -Wall -Wextra -Werror -fPIC -target x86_64-pc-freebsd12 \
    -c "$project/host/src/win32/xaudio2_abi.c" -o "$build/obj/xaudio2_abi.o"
"$cc" -O2 -std=gnu11 -Wall -Wextra -Werror -fPIC -target x86_64-pc-freebsd12     -c "$project/host/src/win32/dwrite_abi.c" -o "$build/obj/dwrite_abi.o"
"$cc" -O2 -std=gnu11 -Wall -Wextra -Werror -fPIC -target x86_64-pc-freebsd12 \
    -c "$project/host/src/win32/gamepad_abi.c" -o "$build/obj/gamepad_abi.o"
objects=("$build/obj/crt.o" "$build/obj/title.o" "$build/obj/emutls.o" "$build/obj/gpu.o" "$build/obj/guest_abi.o" "$build/obj/vulkan_abi.o" "$build/obj/dxgi_abi.o" "$build/obj/audio_abi.o" "$build/obj/wic_abi.o" "$build/obj/xaudio2_abi.o" "$build/obj/dwrite_abi.o" "$build/obj/gamepad_abi.o")
while IFS= read -r -d '' source; do
    name=${source#"$project/host/src/"}
    object="$build/obj/${name//\//_}.o"
    extra=()
    [[ $name != main.cpp ]] || extra+=(-Dmain=azimuthHostMain)
    "$cxx" "${flags[@]}" -std=c++17 -I"$project/host/third_party/vulkan" "${extra[@]}" -c "$source" -o "$object"
    objects+=("$object")
done < <(find "$project/host/src" -name '*.cpp' -print0 | sort -z)
# No SDK payload CRT: a title receives process arguments, not payload_args.
# Keep the SDK C++/compat implementations used by the payload host.
# The optional __cxa hook has no provider: use libc++abi's pthread fallback.
# Resolve platform calls from system modules BEFORE the SDK compatibility C
# archive. Its mmap/mprotect/syscall implementations issue raw syscalls and
# belong to the payload process model, not this native title.
"$sdk/bin/prospero-lld" -T "$root/eboot.ld" --version-script "$root/eboot.map" --eh-frame-hdr -e _start \
    -L "$sdk/target/lib" -o "$build/llvm-pie.elf" "${objects[@]}" \
    --defsym=__cxa_thread_atexit_impl=0 \
    --start-group "$sdk/target/lib/libc++.a" "$sdk/target/lib/libc++abi.a" \
    "$sdk/target/lib/libunwind.a" --end-group \
    --as-needed "$sdk/target/lib/libSceLibcInternal.so" \
    "$sdk/target/lib/libkernel.so" "$sdk/target/lib/libSceNet.so" \
    "$sdk/target/lib/libSceSysmodule.so" \
    "$sdk/target/lib/libSceVideoOut.so" "$sdk/target/lib/libSceSystemService.so" \
    "$sdk/target/lib/libScePad.so" "$sdk/target/lib/libSceUserService.so" \
    "$sdk/target/lib/libSceAudioOut.so" \
    "$sdk/target/lib/libc.a"
python3 "$root/check_runtime.py" "$build/llvm-pie.elf"
"$tool" link --in "$build/llvm-pie.elf" --out "$build/eboot.elf" \
    --stub-dir "$sdk/target/lib" --module-sdk 0x02000009 \
    --companion-sdk 0x08050001 --file-name eboot.elf
"$tool" self --sign --in "$build/eboot.elf" --out "$dist/eboot.bin" --magic 0x1D3D154F
python3 "$root/make_icon.py"
cp "$root/sce_sys/param.json" "$root/sce_sys/icon0.png" "$dist/sce_sys/"
cp "$foundation/runtime/libc.prx" "$dist/sce_module/libc.prx"
chmod 755 "$dist/eboot.bin" "$dist/sce_module/libc.prx"
readelf --wide --dyn-syms "$build/llvm-pie.elf" > "$build/dynamic-imports.txt"
python3 "$root/verify.py" "$build/eboot.elf" "$dist/eboot.bin"
"$tool" self --inspect --file "$dist/eboot.bin" > "$build/self-inspection.txt"
grep -qx 'integrity: valid' "$build/self-inspection.txt"
python3 "$root/package.py"
printf 'Native title built: %s\n' "$dist"
