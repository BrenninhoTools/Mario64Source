# Mario64Source

A native port of Super Mario 64 for Windows, Linux and Android, built on top of the
[n64decomp/sm64](https://github.com/n64decomp/sm64) decompilation with a platform layer written
from scratch.

This repository contains **no ROM and no game assets**. Every build extracts textures, models,
sounds and music from a ROM that you provide yourself. Do not commit or distribute your ROM, the
extracted assets, or any build produced from them.

## Supported versions

| Version | Make option   | ROM file name         |
|---------|---------------|-----------------------|
| US      | `VERSION=us`  | `baserom.us.z64`      |
| JP      | `VERSION=jp`  | `baserom.jp.z64`      |
| EU      | `VERSION=eu`  | `baserom.eu.z64`      |
| Shindou | `VERSION=sh`  | `baserom.sh.z64`      |

## How it works

The game code from the decompilation runs natively. Everything that talked to N64 hardware is
replaced by `src/port`:

| Module                     | Replaces                                                         |
|----------------------------|------------------------------------------------------------------|
| `gfx/gfx_rsp.c`            | RSP/RDP: interprets F3DEX2 display lists, lighting, fog, TMEM    |
| `gfx/gfx_gl.c`             | Draws with OpenGL 2.1 (desktop) or OpenGL ES 2.0 (Android)       |
| `gfx/gfx_texture.c`        | Decodes RGBA16/32, IA4/8/16, I4/8 and CI4/8 textures, with cache |
| `audio/audio_rsp.c`        | Audio microcode (aspMain and the Shindou ABI) in software        |
| `audio/audio_sdl.c`        | Audio interface output through SDL2                              |
| `input.c`                  | Keyboard, gamepads, rumble and on-screen touch controls          |
| `ultra_reimpl.c`           | libultra: messages, DMA, timers, controllers, EEPROM saves       |
| `port_main.c`              | Main loop at 30 FPS (25 FPS on EU) with display-rate presentation |

Widescreen is enabled: the HUD sticks to the screen edges and the camera shows more on wide
displays.

## Building

Place your ROM in the repository root with the name from the table above.

### Windows (MSYS2)

```sh
pacman -S make git mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-python mingw-w64-x86_64-binutils
make -f Makefile.port VERSION=us -j4
```

Output: `build/us_windows/sm64.us.exe` (statically linked, no DLLs needed).

### Linux

```sh
sudo apt install build-essential libsdl2-dev python3 binutils
make -f Makefile.port VERSION=us -j4
```

Output: `build/us_linux/sm64.us`.

### Android

Requirements: Android SDK, NDK r26+, CMake, Gradle 8.9 and JDK 17.

```sh
export ANDROID_NDK_HOME=/path/to/ndk
VERSION=us ./android/build_native.sh
gradle -p android assembleRelease
```

`build_native.sh` downloads SDL2, builds it for `arm64-v8a`, compiles the game into
`libmain.so` and places both libraries in `android/app/src/main/jniLibs`. The APK ends up in
`android/app/build/outputs/apk/release`.

### Checking sources without a ROM

```sh
make -f Makefile.port NOEXTRACT=1 VERSION=us check
```

This compiles the platform layer and all game code that does not depend on extracted assets.

## Controls

| N64      | Keyboard              | Gamepad                 |
|----------|-----------------------|-------------------------|
| Stick    | W A S D (Alt = walk)  | Left stick              |
| A        | Space / J             | A                       |
| B        | K / Left Shift        | B / X                   |
| Z        | L / Left Ctrl         | LB / LT                 |
| R        | R / Right Shift       | RB / RT                 |
| Start    | Enter                 | Start                   |
| C-Up     | Up / I                | Right stick up          |
| C-Down   | Down / ,              | Right stick down / Y    |
| C-Left   | Left / U              | Right stick left        |
| C-Right  | Right / O             | Right stick right       |

F11 or Alt+Enter toggles fullscreen. On Android the touch overlay appears automatically: the left
half of the screen is a floating stick, buttons sit on the right.

Saves are written to `sm64_save.eep` in the SDL preferences folder
(`%APPDATA%\BrenninhoTools\Mario64` on Windows, app internal storage on Android).

## CI

- `Android Source Check` runs on every push and compiles the sources for all versions with the
  Android NDK, without needing a ROM.
- `Build Android APK` is manual (Actions tab, Run workflow). It reads the ROM from a private
  download link stored in the `BASEROM_URL` secret and uploads the APK as an artifact. On a public
  repository anyone with a GitHub account can download artifacts, so only run it on a private
  repository.

## Credits

- Decompilation: the n64decomp team, released under CC0 (see `LICENSE-DECOMP.md`).
- Platform layer and build system: BrenninhoTools, under the MIT license in `LICENSE`.
- SDL2 for windowing, input and audio.

Super Mario 64 is a trademark of Nintendo. This project is not affiliated with or endorsed by
Nintendo.
