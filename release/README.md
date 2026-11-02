# Playmuth

**Playmuth** roda o motor [Azimuth](https://almost-there-games.itch.io/azimuth) no PS5 como um aplicativo homebrew nativo. Com ele, o Forza Horizon 2 roda no console em 4K, MSAA 4x e 60 FPS, com o controle do PS5.

> Projeto independente e **não oficial**. Não tem ligação com a Almost There Games (Azimuth), a Microsoft, a Playground Games nem a Sony. O Playmuth **não inclui** o Azimuth nem nenhum arquivo de jogo: você precisa ter os seus.

*English summary below.*

## Downloads

Baixe a versão mais recente em **[Releases](../../releases)**. Cada release tem:
- `Playmuth-<versão>.zip`: a pasta do aplicativo (`FAKE78002`) para instalar no PS5.
- `Playmuth-<versão>-source.zip`: o código-fonte completo daquela versão (GPL-3.0).

## Requisitos

- PS5 com suporte a homebrew e um jeito de registrar aplicativos em pasta (ShadowMountPlus, por exemplo).
- Sua cópia do **Azimuth** (versão 1.0.2 ou 0.9.7: o Playmuth confere o `azimuth.exe` por SHA-256) com a pasta `data`, e os arquivos do jogo configurados no `azimuth.ini` do próprio Azimuth.
- Só para o Azimuth 0.9.7: as fontes **Arial** (`arial.ttf` e `arialbd.ttf`) da sua instalação do Windows, usadas pelo launcher dele. O 1.0.2 usa as fontes que já vêm com ele.

## Instalação

1. Extraia o `Playmuth-<versão>.zip` e copie a pasta `FAKE78002` para `/data/homebrew/FAKE78002` no PS5. Mantenha a permissão de execução do `eboot.bin`, do `sce_module/libc.prx` e do `radv/libvulkan.prx`.
2. Registre o aplicativo pelo seu gerenciador de homebrew.
3. Copie a pasta do Azimuth para o **SSD interno**, em `/data/PC/FH2`, com o `azimuth.exe` em `/data/PC/FH2/azimuth.exe`. Dentro do Azimuth, esse caminho é o `E:\PC\FH2`, e `/data` é o `E:`. O pendrive também funciona (`/mnt/usbN/PC/FH2`), mas é lento demais para o jogo carregar direito.
4. Só no Azimuth 0.9.7: copie `arial.ttf` e `arialbd.ttf` para `/data/azimuth-ps5/fonts/`, conferindo se a extensão é mesmo `.ttf`.
5. Abra o **Playmuth** no menu do PS5.

## Problemas

O Playmuth grava os logs em `/data/azimuth-ps5/`:
- `title.txt`: versão e como a execução terminou.
- `host.txt`: log completo. A execução anterior fica em `host-anterior.txt`.

O log do próprio Azimuth fica em `data/logs/azimuth.log`, dentro da pasta dele. Ao abrir um issue, anexe esses arquivos e diga a versão do Playmuth e do Azimuth.

## Como funciona

O Playmuth carrega o `azimuth.exe` original sem modificá-lo e implementa as APIs do Windows que ele usa (kernel32, D3D12/DXGI, XAudio2, DirectWrite, WIC, Windows.Gaming.Input e outras). Por baixo:
- O D3D12 vira Vulkan pelo [vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton).
- O Vulkan roda no driver RADV do [Mesa](https://mesa3d.org) compilado para o PS5.
- A tela, o áudio e o controle usam as bibliotecas do próprio console.

## Licença

GPL-3.0-or-later. O aplicativo inclui componentes de terceiros: PS5 Native App Boilerplate (GPL-3.0), ps5-payload-sdk (GPL-3.0 e BSD), Mesa/RADV (MIT), vkd3d-proton (LGPL-2.1) e LLVM (Apache-2.0 com exceção LLVM). Os textos das licenças estão na pasta `licenses` do pacote. O código-fonte de cada versão está na respectiva release.

---

## English summary

**Playmuth** runs the [Azimuth](https://almost-there-games.itch.io/azimuth) engine on the PS5 as a native homebrew app, which plays Forza Horizon 2 at 4K, 4x MSAA, 60 fps with the PS5 controller. It is an independent, **unofficial** project, not affiliated with Almost There Games, Microsoft, Playground Games or Sony. It does **not** include Azimuth or any game files.

**Install:**
1. Copy the `FAKE78002` folder from the release zip to `/data/homebrew/FAKE78002` and register it with your homebrew manager.
2. Put your Azimuth 1.0.2 (or 0.9.7) folder at `/data/PC/FH2`, so the executable is at `/data/PC/FH2/azimuth.exe`.
3. Azimuth 0.9.7 only: put `arial.ttf` and `arialbd.ttf` in `/data/azimuth-ps5/fonts/`.
4. Launch **Playmuth**.

Logs are written to `/data/azimuth-ps5/`. License: GPL-3.0-or-later; full source code is attached to every release.
