# Knight of Knights: PC and PSP port

A native port of Tomoshibi's *Knight of Knights* (2002, Windows/DirectX) to Linux (SDL2 + OpenGL)
and the Sony PSP.

The game logic is the original code: it is decompiled from the game executable, translated to C,
and runs in a small virtual machine. The Win32 / DirectDraw / Direct3D Retained Mode /
DirectSound layer it depends on is reimplemented in `port/src`.

**This repository contains no game files and no decompiled code.** You need your own copy of the
original game (version 0.50). The decompile and all game data are produced locally from it.

## Repository layout

| Path | What it is |
|---|---|
| `port/src/core` | VM memory, x87/CRT helpers |
| `port/src/rm`, `port/src/dx` | Direct3D Retained Mode, DirectDraw/Direct3D IM, DirectSound/DirectMusic replacements |
| `port/src/plat/pc`, `port/src/plat/psp` | platform backends (SDL2/OpenGL, PSP GU) |
| `port/src/game/sys.c`, `port/src/game/hand_*.c` | hand-written replacements and hand ports of game functions |
| `port/src/game/gen/rt.c`, `vmc.h` | runtime for the generated game code |
| `port/src/game/gen/g_4*.c`, `fnreg.c`, `protos.h` | **generated, not in git**: the game code (step 2 below) |
| `port/re/scripts` | Ghidra scripts and inputs used by `tools/decompile.sh` |
| `port/tools` | decompile/generation pipeline, data preparation, PSP packaging, test helpers |
| `port/tools/patches.py` | every fix applied to the generated code (decompiler defects found so far) |

## Requirements

- The original game: copy its installation folder (the one with `Knight of Knights.exe`, `dat/`,
  `obj/`, `pic/` ...) to `game/Knight050/` next to `port/`.
  Expected exe MD5: `9f24022f66b8fa6ae4a82057bd1d3b33`.
- Linux with: `cmake`, a C compiler, `python3`, `binutils` (objdump).
- To generate the game code: Ghidra 12.x (headless) with JDK 21, and a Python venv with
  `pycparser`:
  ```sh
  python3 -m venv port/tools/.venv && port/tools/.venv/bin/pip install pycparser
  ```
- To prepare the data: Pillow (`python3-pil`), FluidSynth with the FluidR3 GM soundfont
  (`fluidsynth`, `fluid-soundfont-gm`), `oggenc` (`vorbis-tools`). The fonts used to render the
  game's Japanese GDI text are `fonts-droid-fallback` and `fonts-dejavu-core`. Paths can be passed
  to `tools/bakefont.py`; set `SF2=` to use another soundfont.
- PC build: SDL2 and OpenGL development packages (`libsdl2-dev`, `libgl-dev`).
- PSP build: the [pspdev](https://github.com/pspdev/pspdev) toolchain (`PSPDEV`, default `~/pspdev`).

Debian/Ubuntu example:
```sh
sudo apt install cmake build-essential python3 python3-pil python3-venv binutils \
    fluidsynth fluid-soundfont-gm vorbis-tools fonts-droid-fallback fonts-dejavu-core \
    libsdl2-dev libgl-dev
```

## 1. Prepare the game data

```sh
cd port
tools/make_data.sh ../game/Knight050 build-data
```

This copies the game data into `build-data/`, renders the MIDI music to OGG, bakes the font
(`kokfont.bin`), and creates the PSP menu images `psp/ICON0.PNG` and `psp/PIC1.PNG` from the
title screen.

## 2. Generate the game code

This step is required: the game code the port runs is the decompile of your exe, translated to C.
It cannot be shipped in this repository, so every checkout generates it once.

```sh
tools/decompile.sh "../game/Knight050/Knight of Knights.exe" ~/ghidra_12.1.4_PUBLIC
tools/postgen.sh
```

- `decompile.sh` runs Ghidra headless on the exe and writes `re/decomp/`. It takes a few minutes:
  analysis, extra entry points, the x87/parameter fixes, the decompile with marked bit-casts,
  and the global type tables.
- `postgen.sh` translates it into `src/game/gen/g_4*.c`, removes the functions that have hand
  ports, and applies `tools/patches.py`. It fails loudly if any patch no longer matches.

Run step 2 again only if the decompile or the patches change.

## 3. PC build

```sh
cmake -S . -B build-pc -DCMAKE_BUILD_TYPE=Release
cmake --build build-pc -j
build-pc/kok build-data          # or: KOK_DATA=build-data build-pc/kok
```

- The window opens at 1280x720 in widescreen. Resize it to change the aspect: a 4:3 window gives
  the original layout. `KOK_WIDE=0` forces 4:3.
- Alt+Enter toggles fullscreen.

## 4. PSP build

```sh
tools/package_psp.sh ../game/Knight050
```

This builds `build-psp/EBOOT.PBP` (configured with `psp-cmake`, Release) and lays out a
ready-to-copy install in `dist/psp/`.

To configure the PSP build by hand instead:
```sh
export PSPDEV=~/pspdev PATH=~/pspdev/bin:$PATH
cmake -S . -B build-psp -DCMAKE_TOOLCHAIN_FILE=$PSPDEV/psp/share/pspdev.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-psp -j
```

### What to copy to the PSP

Copy the `PSP` folder from `port/dist/psp/` to the root of the memory stick. You get:

```
ms0:/PSP/GAME/KnightOfKnights/
    EBOOT.PBP         the port (from build-psp/)
    kok.exe           the original exe (the port reads its data tables from it)
    kokfont.bin       baked font
    dat/ obj/ pic/ tex/ wave/ save/     original game data
    music/            the MIDI music rendered to OGG
```

If you assemble it by hand, that is `build-psp/EBOOT.PBP` plus the whole contents of `build-data/`
in one folder under `PSP/GAME/`. Saves are written to `save/` in that folder.

The PSP build needs a PSP that can run homebrew. It uses the extended 64 MB memory, so it runs on
PSP-2000 and later models (and PPSSPP), not on the PSP-1000.

## Controls

| Action | PC keyboard | PSP |
|---|---|---|
| Move | arrows | d-pad / analog stick |
| Right-hand attack (hold: special move) | X | Square |
| Left-hand attack (hold: special move) | Z | Triangle |
| Registered combo (hold: magic) | Z + X | L |
| Jump | C | Cross |
| Use item | V | Circle |
| Item ring / pick up | S | R |
| Minimap | F1 | Select |
| Pause (battle) | Enter or P | Start |
| Quit | Esc | Home |

Menus and the item ring:

| | PC keyboard | PSP |
|---|---|---|
| Confirm | Z or X | Cross (or Start) |
| Back | Backspace | Circle |
| Ring: select / equip | Z or X | Cross |
| Ring: close | S | Circle or R |
| Ring: drop item | C | Square |

The original game has no back key. The port adds one: it returns from character select to stage
select, from stage select to mode select, and from mode select to the title; in the options it
acts as Exit. A PSP button still held when the screen changes (for example Circle after closing the
ring) is ignored until you release it.

The 2P modes need a second set of controls, so on the PSP they are greyed out and the cursor
skips them. A game controller works on PC as well.

## Options and test helpers

- `KOK_WIDE=0` turns widescreen off. On the PSP, put the line `env KOK_WIDE=0` in `test.txt`
  next to the EBOOT.
- `test.txt` on the PSP: `perf` logs frame and CPU timings once a second to `log.txt` (next to the
  EBOOT) and itemises every battle frame slower than 40 ms (`SPIKE` lines: logic, collision,
  scene, mesh rebuilds/loads, uploads, file reads). `env NAME=value` sets an environment option
  (`env KOK_SPIKE=30000` changes the threshold in microseconds). `shot f1,f2` saves screenshots at
  those frames.
- `KOK_BOT=1` plays automatically (menus, then Stage 1 fights). `KOK_BOT=menu` only gets through the menus. `KOK_BOT=explore` walks through
  rooms ignoring enemies. `KOK_MAP=2` (with the bot) starts the second stage.
- `KOK_SHOT=prefix:f1,f2` and `KOK_KEYS="frame:+VK,..."` (PC) take screenshots and script key
  presses.

## Working on the code

- Never edit `src/game/gen/g_4*.c` by hand; they are regenerated. Fix decompiler defects in
  `tools/patches.py` (per-function regexes, checked against the original asm), then rerun
  `tools/postgen.sh`.
- Larger rewrites go in `src/game/hand_*.c`. Register the function in `tools/postgen.sh`
  (`rmfn.py`) so the generated copy is removed.
- `tools/dis.sh <start> <end>` prints the original disassembly (from `re/decomp/text.asm`).
