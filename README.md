# DxTetris

A Tetris clone in C++20 / Direct3D 11, built with CMake + Ninja against [DirectX Tool Kit](https://github.com/microsoft/DirectXTK).

## Features

- SRS rotation with wall kicks, 7-bag randomizer, 3-piece preview, hold
- DAS (133 ms) / soft-drop repeat, 0.5 s lock delay with 15 reset cap
- Guideline-ish scoring (single/double/triple/quad, back-to-back, combo, level curve)
- Ghost piece (landing preview) and top-out indicator
- Bitmap font rendered from a generated glyph atlas (`tools/gen_font.py`)
- Sound effects (move/rotate/lock/clear/combo/level-up/game over) via DirectXTK AudioEngine; the game still runs if no playback device exists
- `--snapshot` mode renders a frame to PNG (useful for CI / smoke tests)

## Controls

| Key | Action |
| --- | --- |
| ← / → | move left / right (DAS auto-repeat) |
| X | rotate clockwise |
| Z | rotate counter-clockwise |
| Space | rotate 180° |
| ↓ | soft drop (hold to speed up) |
| ↑ | hard drop (lock immediately) |
| E | hold / swap |
| Y | start / restart after game over |
| Esc | pause / resume |

## Build

Requirements: Visual Studio 2022+ (MSVC x64), CMake 3.29+, Ninja, Python 3 with Pillow, and a DirectXTK source tree under `third_party/DirectXTK`. Build DirectXTK once with `tools\build_dxtk.bat`: it configures the static CRT (`/MT`) and XAudio 2.9, matching the game's runtime settings.

```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
tools\build_dxtk.bat
python tools\gen_assets.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`tools\gen_assets.py` rebuilds the generated asset sections from `assets/src` (run it once after cloning, and after changing any source asset).

The game links the MSVC runtime statically (`/MT`), so no Visual C++ redistributable DLLs are required. Its remaining DLL imports (`d3d11`, `dxgi`, `ole32`, `user32`, `winmm`, `kernel32`, `imm32`, `XAudio2_9`) are all Windows system components; `XAudio2_9.dll` ships with Windows 10/11.

The executable and its runtime assets land in `build/`; run `build\tetris.exe`.

Screenshot / smoke test:

```bat
build\tetris.exe --snapshot --delay=3000
```

writes `snapshot.png` after three seconds of gameplay.
