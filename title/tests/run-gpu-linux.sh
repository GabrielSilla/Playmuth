#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/build/gpu-test"
mkdir -p "$build/map" "$build/full" "$build/full-title" "$build/fault" "$build/fault-title" "$build/fault-sys"
cc -O2 -std=gnu11 -Wall -Wextra -Werror "$root/../gpu-probe/main.c" \
    -ldl -lpthread -o "$build/gpu-check"
cc -O2 -std=gnu11 -Wall -Wextra -Werror -DAZIMUTH_TITLE=1 "$root/../gpu-probe/main.c" \
    -ldl -lpthread -o "$build/gpu-title-check"
printf 'carregado\n' > "$build/map/gpu-stop"
(cd "$build/map" && ../gpu-check > output.txt 2>&1)
grep -Fq '[PAROU] GPU probe: RADV mapeado; parada antes da entrada' "$build/map/output.txt"
if grep -Fq 'Etapa 2:' "$build/map/output.txt"; then
    echo "Mapping-only stop continued into Vulkan" >&2; exit 1
fi
for variant in full full-title; do
    binary=gpu-check
    [[ $variant != full-title ]] || binary=gpu-title-check
    (cd "$build/$variant" && "../$binary" > output.txt 2>&1)
    grep -Fq '[PASSOU] GPU probe: a GPU executou comandos pelo Vulkan' "$build/$variant/output.txt"
    grep -Fq '0 de 16 palavras diferentes' "$build/$variant/output.txt"
    grep -Fq '0 de 64 palavras diferentes' "$build/$variant/output.txt"
    grep -Fq '0 de 1024 palavras diferentes' "$build/$variant/output.txt"
    grep -Fq 'Etapa 4e:' "$build/$variant/output.txt"
    grep -Fq 'vkCmdUpdateBuffer (CP WRITE_DATA) gravado' "$build/$variant/output.txt"
    grep -Fq 'consulta 1: retornou' "$build/$variant/output.txt"
    grep -Fq 'RADV_DEBUG: nao definido' "$build/$variant/output.txt"
    grep -Fq 'gravadas em gpu-shader-window.bin' "$build/$variant/output.txt"
    grep -Fq 'vkQueueSubmit retornou 0' "$build/$variant/output.txt"
    grep -Fq 'vkGetFenceStatus: 0 depois de' "$build/$variant/output.txt"
    grep -Fq 'Watchdog confirmou' "$build/$variant/output.txt"
    grep -Fq 'Pausa antes do RADV retornou 0' "$build/$variant/output.txt"
    grep -Fq 'Fence antes do envio vazio: 1' "$build/$variant/output.txt"
    grep -Fq 'Envio sem command buffer concluido' "$build/$variant/output.txt"
    grep -Fq 'Command buffer vazio concluido' "$build/$variant/output.txt"
    grep -Fq 'Barreira TRANSFER_WRITE -> HOST_READ gravada' "$build/$variant/output.txt"
    grep -Fq 'vkDeviceWaitIdle retornou 0' "$build/$variant/output.txt"
    grep -Fq 'Watchdog encerrado: pthread_join 0' "$build/$variant/output.txt"
done
for variant in fault fault-title fault-sys; do
    binary=gpu-check
    stop=falha
    [[ $variant == fault ]] || binary=gpu-title-check
    [[ $variant != fault-sys ]] || stop=falha-syscall
    printf '%s\n' "$stop" > "$build/$variant/gpu-stop"
    set +e
    (cd "$build/$variant" && "../$binary" > output.txt 2>&1)
    fault_status=$?
    set -e
    [[ $fault_status == 1 ]] || { echo "Unexpected fault-test exit: $fault_status" >&2; exit 1; }
    grep -Fq 'diagnostico direto por write: descritor' "$build/$variant/gpu-probe.txt"
    grep -Fq 'FALHA CAPTURADA: sinal' "$build/$variant/gpu-probe.txt"
    grep -Fq 'capturado rip' "$build/$variant/gpu-probe.txt"
    grep -Fq 'Fim do GPU probe' "$build/$variant/gpu-probe.txt"
    grep -Fq 'Watchdog encerrado: pthread_join 0' "$build/$variant/gpu-probe.txt"
done
echo "GPU probe: planned stop, Vulkan buffer and recovered-fault diagnostics passed on Linux."
