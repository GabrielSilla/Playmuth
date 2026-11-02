#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
foundation="$root/.deps/native-foundation"
pin=9c0b994a048521af6fb84c73ded364504fe250e9
if [[ ! -d $foundation/.git ]]; then
    git clone --filter=blob:none --no-checkout \
        https://github.com/mpereiraesaa/ps5-native-app-boilerplate.git "$foundation"
    git -C "$foundation" checkout --detach "$pin"
fi
[[ $(git -C "$foundation" rev-parse HEAD) == "$pin" ]] || {
    echo "Native foundation must be at $pin: $foundation" >&2; exit 2;
}
# This generates the companion module from public source and verifies its digest.
if [[ ! -f $foundation/runtime/libc.prx || ! -x $foundation/build/runtime-shim/ps5-native-tool ]]; then
    bash "$foundation/tools/rebuild-libc.sh"
fi
(cd "$foundation/runtime" && sha256sum --check --strict libc.prx.sha256)
printf '%s\n' "$pin" > "$root/build/foundation-revision.txt"
