#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}
build="$root/build/tls-test"
mkdir -p "$build"
# The SDK fallback object is x86-64 ELF and uses the same Itanium ABI and
# pthread interface here. Bind its optional glibc hook to null, as on PS5.
# Use a non-PIE test executable: GNU ld otherwise turns --defsym=0 in this
# particular GOT reference into RELATIVE+0, which becomes nonnull under ASLR.
# The PS5 build uses lld and check_runtime.py rejects that relocation.
ar p "$sdk/target/lib/libc++abi.a" cxa_thread_atexit.cpp.o > "$build/fallback.o"
clang -O2 -c "$root/runtime/emutls.c" -o "$build/baseline.o"
clang -O2 -DAZIMUTH_TITLE=1 -c "$root/runtime/emutls.c" -o "$build/fixed.o"
for variant in baseline fixed; do
    clang++ -O2 -std=c++17 -femulated-tls "$root/tests/TlsExitTest.cpp" \
        "$build/fallback.o" "$build/$variant.o" -pthread \
        -no-pie -Wl,--defsym=__cxa_thread_atexit_impl=0 -o "$build/$variant"
done
set +e
"$build/baseline" > "$build/baseline.txt"
baseline_status=$?
set -e
cat "$build/baseline.txt"
[[ $baseline_status == 1 ]] || { echo "Baseline did not reproduce the defect" >&2; exit 1; }
"$build/fixed" > "$build/fixed.txt"
cat "$build/fixed.txt"
echo "Baseline reproduced; fixed emutls passed."
