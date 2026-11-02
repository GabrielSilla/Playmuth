# Componentes externos

- PS5 Native App Boilerplate, BlackBearReloaded, GPL-3.0-or-later.
  Fonte: https://github.com/mpereiraesaa/ps5-native-app-boilerplate
  Revisão fixada: `9c0b994a048521af6fb84c73ded364504fe250e9`.
  `eboot.ld` deriva de `tooling/native/ps5-pie-high.ld`, com os limites de
  unwind acrescentados para libunwind. O startup, conversor ELF/SCE, assinador
  e gerador do runtime são usados da cópia dessa revisão em `.deps/`.
  Texto da licença em `licenses/GPL-3.0.txt`.
- ps5-payload-sdk, John Törnblom e contribuidores, e suas bibliotecas C/C++.
  Fonte: https://github.com/ps5-payload-dev/sdk
  Usado do SDK instalado no ambiente (v0.45); consultar as licenças upstream
  dos componentes do SDK ao redistribuir o conjunto.
- `runtime/emutls.c`: LLVM/ps5-payload-sdk, Apache-2.0 WITH LLVM-exception.
  Copiado de `libc/emutls.c` do SDK v0.45, revisão
  `a7685ce8e97ce1f8a4f38253a41d54241c9124f1`.
  Modificação: uma rodada adicional de limpeza de pthread no build
  `AZIMUTH_TITLE`, antes de liberar o armazenamento do TLS emulado.
  Licença em `licenses/Apache-2.0-with-LLVM-exception.txt`.

- `radv/libvulkan.prx`: Mesa 26.2.0 (driver Vulkan RADV) compilado para o
  PS5, MIT. Binário tirado do kit wine do Playden (`031302d-kit-0.13`),
  SHA-256 `db21b394c79af0fab48072a7a86e2f25609f2d122552831810a47bad0c641b94`.
  Fonte: https://gitlab.freedesktop.org/mesa/mesa. Licença em
  `licenses/MIT-Mesa.txt`.
- `dll/d3d12.dll` e `dll/d3d12core.dll`: vkd3d-proton (x86_64-windows),
  LGPL-2.1-or-later. Binários do mesmo kit, SHA-256 `08e44b05...` e
  `5b4257b3...`. Fonte: https://github.com/HansKristian-Work/vkd3d-proton.
  Licença em `licenses/LGPL-2.1-vkd3d-proton.txt`.

Os fontes de integração deste diretório e os fontes do host/probes estão no
projeto Azimuth_PS5. O pacote não contém o motor Azimuth, o jogo ou firmware.

- Símbolo Azimuth: marca de Almost There Games. Reproduzido em vetor para o
  ícone do Azimuth Launcher a partir da arte na página oficial:
  https://almost-there-games.itch.io/azimuth . Fonte editável em
  `assets/azimuth-symbol.svg`; referência e procedência em `assets/README.md`.
