<p align="center"><img src="playmuth-banner.png" alt="Playmuth" width="640"></p>

# Playmuth

**Playmuth** roda o motor [Azimuth](https://almost-there-games.itch.io/azimuth) no PS5 como um aplicativo homebrew nativo. Com ele, o Forza Horizon 2 roda no console em 4K, MSAA 4x e 60 FPS, com o controle do PS5.

> Projeto independente e **não oficial**. Não tem ligação com a Almost There Games (Azimuth), a Microsoft, a Playground Games nem a Sony. O Playmuth **não inclui** o Azimuth nem nenhum arquivo de jogo: você precisa ter os seus.

## Como usar

### 1. Separe o que você precisa

- PS5 com suporte a homebrew e um jeito de registrar aplicativos em pasta (ShadowMountPlus, por exemplo).
- Sua cópia do **Azimuth** (versão 1.0.2 ou 0.9.7: o Playmuth confere o `azimuth.exe` por SHA-256) com a pasta `data`, e os arquivos do jogo configurados no `azimuth.ini` do próprio Azimuth.
- Só para o Azimuth 0.9.7: as fontes **Arial** (`arial.ttf` e `arialbd.ttf`) da sua instalação do Windows, usadas pelo launcher dele. O 1.0.2 usa as fontes que já vêm com ele.

### 2. Baixe e instale o Playmuth

Baixe o arquivo **`Playmuth-<versão>.zip`** da versão mais recente em **[Releases](../../releases)**. O arquivo com o sufixo `-source.zip` contém o código-fonte e não é o pacote de instalação.

1. Extraia o ZIP no computador.
2. Copie a pasta `FAKE78002` inteira para `/data/homebrew/FAKE78002` no PS5.
3. Mantenha a permissão de execução do `eboot.bin`, do `sce_module/libc.prx` e do `radv/libvulkan.prx`.
4. Registre o aplicativo pelo seu gerenciador de homebrew.

### 3. Copie e configure o Azimuth e o jogo

Copie a pasta completa da sua versão do Azimuth para o **SSD interno**, em `/data/PC/FH2`. Os arquivos principais devem ficar assim:

```text
/data/PC/FH2/
├── azimuth.exe
├── azimuth.ini
└── data/
```

Use a pasta `data` que acompanha a mesma versão do `azimuth.exe`; não misture os arquivos do Azimuth 0.9.7 com os do 1.0.2.

No `azimuth.ini`, configure o caminho dos seus arquivos de jogo usando os caminhos que o Azimuth enxerga:

| Caminho no PS5 | Caminho dentro do Azimuth |
|---|---|
| `/data` | `E:\` |
| `/data/PC/FH2` | `E:\PC\FH2` |

O pendrive também funciona em `/mnt/usbN/PC/FH2`, mas é lento demais para o jogo carregar direito. Use o SSD interno para jogar.

### 4. Prepare as fontes, se usar o Azimuth 0.9.7

Copie `arial.ttf` e `arialbd.ttf` da sua instalação do Windows para `/data/azimuth-ps5/fonts/` no PS5. Confira se a extensão dos arquivos é mesmo `.ttf`.

**No Azimuth 1.0.2, pule esta etapa:** ele usa as fontes da própria pasta `data`.

### 5. Abra e jogue

Abra o **Playmuth** no menu do PS5. Ele carrega o Azimuth e mostra o launcher; use o controle do PS5 para selecionar e iniciar o jogo.

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

## Para desenvolvedores

- Como compilar e o histórico técnico do título: [title/README.md](title/README.md). Em resumo: no WSL Ubuntu com o ps5-payload-sdk v0.45 em `/opt/ps5-payload-sdk`, execute `bash title/build.sh` na raiz do projeto. O script prepara as dependências e gera o pacote em `title/dist/`.
- O que se aprendeu programando para o PS5 (runtime, RADV/Vulkan, D3D12 → Vulkan, emulação do Windows): [docs/BASE_DE_CONHECIMENTO_PS5.md](docs/BASE_DE_CONHECIMENTO_PS5.md).
- APIs do Windows implementadas: [docs/APIS_IMPLEMENTADAS.md](docs/APIS_IMPLEMENTADAS.md).
- Histórico de todas as versões: [HISTORICO_DE_ALTERACOES.md](HISTORICO_DE_ALTERACOES.md). Visão geral das fases do projeto (que começou como "Azimuth_PS5"): [docs/PROJETO.md](docs/PROJETO.md).

## Licença

GPL-3.0-or-later. O aplicativo inclui componentes de terceiros: PS5 Native App Boilerplate (GPL-3.0), ps5-payload-sdk (GPL-3.0 e BSD), Mesa/RADV (MIT), vkd3d-proton (LGPL-2.1) e LLVM (Apache-2.0 com exceção LLVM). Os textos das licenças estão na pasta `licenses` do pacote. O código-fonte está neste repositório, e o de cada versão também vai anexado na respectiva release.

---

## English summary

**Playmuth** runs the [Azimuth](https://almost-there-games.itch.io/azimuth) engine on the PS5 as a native homebrew app, which plays Forza Horizon 2 at 4K, 4x MSAA, 60 fps with the PS5 controller. It is an independent, **unofficial** project, not affiliated with Almost There Games, Microsoft, Playground Games or Sony. It does **not** include Azimuth or any game files.

**Install:**

1. Copy the `FAKE78002` folder from the release zip to `/data/homebrew/FAKE78002` and register it with your homebrew manager.
2. Put your Azimuth 1.0.2 (or 0.9.7) folder at `/data/PC/FH2`, so the executable is at `/data/PC/FH2/azimuth.exe`.
3. Azimuth 0.9.7 only: put `arial.ttf` and `arialbd.ttf` in `/data/azimuth-ps5/fonts/`.
4. Launch **Playmuth**.

Logs are written to `/data/azimuth-ps5/`. License: GPL-3.0-or-later. The full source code is in this repository and attached to every release; build notes in `title/README.md`.
