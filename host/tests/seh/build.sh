#!/usr/bin/env bash
# Builds sehtest.dll (SehTest.cpp) with clang for x86_64-pc-windows-msvc,
# linked against msvcr110.dll and KERNEL32.dll import libraries made here.
#   bash tests/seh/build.sh <output folder>
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
out=${1:-$here/../../build/linux}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/msvcr110.def" <<'EOF'
LIBRARY msvcr110.dll
EXPORTS
_CxxThrowException
__CxxFrameHandler3
__C_specific_handler
EOF
cat > "$work/kernel32.def" <<'EOF'
LIBRARY KERNEL32.dll
EXPORTS
RaiseException
EOF
llvm-dlltool -m i386:x86-64 -d "$work/msvcr110.def" -l "$work/msvcr110.lib"
llvm-dlltool -m i386:x86-64 -d "$work/kernel32.def" -l "$work/kernel32.lib"
clang++ --target=x86_64-pc-windows-msvc -O1 -fms-extensions -fexceptions -fcxx-exceptions -fasync-exceptions \
    -fno-stack-protector -c "$here/SehTest.cpp" -o "$work/SehTest.obj"
lld-link /dll /noentry /nodefaultlib /out:"$out/sehtest.dll" "$work/SehTest.obj" "$work/msvcr110.lib" "$work/kernel32.lib"
echo "sehtest.dll em $out"
