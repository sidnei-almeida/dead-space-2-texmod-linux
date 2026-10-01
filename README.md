# DS2TexInject — TexMod texture packs for Dead Space 2 on Linux

Load TexMod/uMod `.tpf` texture packs in **Dead Space 2 on Linux** (Steam + Proton/GE-Proton, DXVK, ReShade, Steam Deck).

TexMod, uMod and gMod don't work under Proton. DS2TexInject is a small `.asi` plugin that does the same job from inside the game. It uses the same texture hashes, so existing `.tpf` packs work without changes.

> 🇧🇷 **Português** mais abaixo — [ir para a versão em português](#português).

---

## Why TexMod/uMod don't work on Linux

- **TexMod and uMod** inject their DLL through Windows global hooks and launcher tricks that Wine doesn't reproduce.
- **They also need to be `d3d9.dll`.** That slot is already taken by ReShade, and below it by DXVK.
- **Dead Space 2's `.exe` is wrapped by EA's DRM.** Its import table is only built at runtime, which breaks most hooking approaches.

## How DS2TexInject works

1. The `.asi` is loaded by [MarkerPatch](https://github.com/Wemino/MarkerPatch) (`dinput8.dll`) through its built-in ASI loader. [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) also works.
2. It puts an inline hook on `Direct3DCreate9`, on whatever `d3d9.dll` the game loads (ReShade or DXVK). From there it hooks the `IDirect3DDevice9` and `IDirect3DTexture9` vtables.
3. Every texture the game uploads gets the **TexMod CRC32 hash** of its top mip level. TexMod's CRC is reflected, `0xEDB88320`, init `0xFFFFFFFF`, with no final XOR.
4. If `texmod/_cache/0xHASH.(dds|png|bmp)` exists, it is loaded and swapped in at `SetTexture`. Replacements are loaded lazily and freed when the original texture is released.

`ds2tex.py` unpacks `.tpf` files offline. A `.tpf` is a zip XOR-ed with `0x3FA4` and protected with TexMod's fixed ZipCrypto password. The script validates every texture (DDS header, truncation, unsupported DX10 formats) and writes them to `texmod/_cache/`.

## Requirements

- Dead Space 2 (Steam), running with Proton/GE-Proton.
- [MarkerPatch](https://github.com/Wemino/MarkerPatch) installed, with `LoadASIPlugins = 1` in `MarkerPatch.ini` (that's the default).
  - Windows DLLs placed in the game folder need the launch option `WINEDLLOVERRIDES="dinput8=n,b" %command%`. If you also use ReShade, use `WINEDLLOVERRIDES="d3d9,dinput8=n,b" %command%`.
- `python3` and `unzip`. Both are already installed on almost every distro.
- Building from source needs a mingw compiler. `build.sh` downloads [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) to `~/.local/opt` automatically, so no sudo is needed. You can also install `mingw-w64-gcc`.

## Install

```sh
git clone https://github.com/sidnei-almeida/ds2-texinject-linux
cd ds2-texinject-linux
# put your .tpf packs in "<Dead Space 2>/texmod/"
./install.sh                         # auto-detects the Steam library
# or: ./install.sh "/path/to/Dead Space 2"
```

`install.sh` does four things:

- builds the plugin (or uses `dist/DS2TexInject.asi`, which you can also get from the Releases page),
- copies it to `<game>/plugins/`,
- creates `DS2TexInject.ini`,
- unpacks the packs into `<game>/texmod/_cache/`.

After that, launch the game from Steam as usual.

**Adding or removing packs:** put the `.tpf` files in `<game>/texmod/` and run `python3 ds2tex.py "<game>/texmod"` again.

**Pack priority:** when two packs replace the same texture, the one that comes first alphabetically wins. To change that, create `texmod/load_order.txt` with one pack filename per line.

## In-game

- **F10** toggles replacements on and off, so you can compare before and after.
- **Log:** `<game>/DS2TexInject.log`. Lines that start with `MATCH` are textures being replaced.

### `DS2TexInject.ini`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | Turns texture replacement on or off. |
| `TextureDir` | `texmod\_cache` | Folder with the `0xHASH.*` files. |
| `Pool` | `managed` | `default` uses less RAM, which helps the 32-bit game with many 4K packs. |
| `ToggleKey` | `0x79` | Virtual-key code for the toggle key (`0x79` is F10). |
| `LogHashes` | `0` | Logs every texture hash the game loads. |
| `DumpTextures` | `0` | Saves the game's original textures as `texmod\_dump\0xHASH.dds`, so you can make your own packs. |

## Tested with

Thanks to the texture authors. Download the packs from their pages:

- [2K-4K Isaac Suits and Face](https://www.nexusmods.com/deadspace2/mods/82): `1-4KMainSuitsnew.tpf`, `2Kaio.tpf`
- [Return to Titan](https://www.nexusmods.com/deadspace2/mods/97) / [Return To Titan Texture Pack](https://www.nexusmods.com/deadspace2/mods/40): `1WEP_RTTn.tpf`, `*INTERACTABLES_RTT*.tpf`

Tested setup: GE-Proton 11, DXVK, ReShade 6.8, MarkerPatch. 396 textures were validated and replaced in-game.

## Troubleshooting

| Symptom | Fix |
|---|---|
| No `DS2TexInject.log` file | The ASI loader isn't running. Check that MarkerPatch's `dinput8.dll` is in the game folder and that `WINEDLLOVERRIDES` includes `dinput8=n,b`. |
| The log says `ERRO` around `Direct3DCreate9` | Open an issue and attach the log. |
| The log has no `MATCH` lines | Set `LogHashes=1` and compare the hashes in the log with `texmod/_cache/index.txt`. |
| The game crashes after a while with many 4K packs | 32-bit memory limit. Set `Pool=default`, or remove some 4K packs. |

## Building

```sh
./build.sh          # -> dist/DS2TexInject.asi
```

All the code is in one file: `src/ds2texinject.cpp`.

---

## Português

O **DS2TexInject** faz os pacotes de textura `.tpf` do TexMod/uMod funcionarem no **Dead Space 2 no Linux** (Steam + Proton/GE-Proton, DXVK, ReShade, Steam Deck). O TexMod, o uMod e o gMod não funcionam no Proton. Este plugin `.asi` faz a mesma coisa de dentro do jogo e usa os mesmos hashes, então os `.tpf` que você já tem funcionam sem alteração.

### Requisitos

- **[MarkerPatch](https://github.com/Wemino/MarkerPatch)** instalado, com `LoadASIPlugins = 1` (já é o padrão).
- **Opção de inicialização no Steam:** `WINEDLLOVERRIDES="dinput8=n,b" %command%`. Se também usar ReShade: `WINEDLLOVERRIDES="d3d9,dinput8=n,b" %command%`.
- **`python3` e `unzip`.**

### Instalação

```sh
git clone https://github.com/sidnei-almeida/ds2-texinject-linux
cd ds2-texinject-linux
# coloque seus .tpf em "<Dead Space 2>/texmod/"
./install.sh          # detecta a pasta do jogo sozinho
```

Depois é só abrir o jogo pelo Steam.

- **F10:** liga e desliga as texturas novas.
- **Log:** `DS2TexInject.log`, na pasta do jogo. As linhas `MATCH` são as texturas trocadas.
- **Adicionou ou removeu um `.tpf`?** Rode `python3 ds2tex.py "<jogo>/texmod"` de novo.
- **Prioridade entre pacotes:** quando dois pacotes trocam a mesma textura, vale o primeiro em ordem alfabética. Para mudar, crie `texmod/load_order.txt` com um nome de pacote por linha.
- **O jogo trava depois de um tempo com muitos pacotes 4K?** Use `Pool=default` no `DS2TexInject.ini`.

As texturas **não** vêm neste repositório. Baixe os pacotes nas páginas dos autores (links em [Tested with](#tested-with)).

## License

MIT. See [LICENSE](LICENSE). The texture packs belong to their respective authors and are not included.
