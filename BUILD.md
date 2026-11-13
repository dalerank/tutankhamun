# Building tutankhamun

## What you need

- Windows 10 or 11.
- Visual Studio 2022 with the "Desktop development with C++" workload (MSVC x86 tools, the Windows SDK).
- CMake 3.22 or newer (the one that comes with Visual Studio is enough) and git on PATH: the first configure of a
  build folder fetches SDL2 2.32.10 and SDL2_mixer 2.8.0 from GitHub and builds them as static libraries.
- Ninja, only for the Ninja presets.

The Windows presets build a 32-bit program (Win32) or, with `-x64` in their name, a 64-bit one (x64). Linux builds
are 64-bit (see [Linux](#linux)).

## build.bat

    build.bat [release|debug] [vs|ninja] [x86|x64]

configures and builds one preset (default `release vs x86`; `x64` builds the 64-bit program), installs the game folder (tutankhamun.exe and `config\`)
into `out\install\<preset>` and prints both paths. With `ninja` it sets up the vcvars32 environment itself;
`TUTANKHAMUN_JOBS` limits the parallel jobs. `update-workspace.bat [Debug|Release] [x86|x64]` configures the Visual Studio preset
and opens its solution.

## Presets

| configuration | Visual Studio 2022 (Win32) | Visual Studio 2022 (x64) | Ninja (vcvars32 environment) | Ninja (vcvars64 environment) | Linux (Ninja) |
|---|---|---|---|---|---|
| Debug | `win-msvc-dev-vs2022` | `win-msvc-dev-x64-vs2022` | `win-msvc-dev-ninja` | `win-msvc-dev-x64-ninja` | `linux-dev` |
| Release | `win-msvc-release-vs2022` | `win-msvc-release-x64-vs2022` | `win-msvc-release-ninja` | `win-msvc-release-x64-ninja` | `linux-release` |

    cmake --preset win-msvc-release-vs2022
    cmake --build --preset win-msvc-release-vs2022

## Plain CMake

    cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
    cmake --build build --config Release

The program is `build\bin\Release\tutankhamun.exe` with `config\` beside it (copied on every build).
`cmake --install build --config Release --prefix <folder>` writes only those two. SDL2, SDL2_mixer and the C run time
are linked statically: no DLL has to be copied.

| CMake option | default | effect |
|---|---|---|
| `TUTANKHAMUN_SDL_LIBS_DIR` | `<build folder>\libs` | where the static SDL2 and SDL2_mixer are built and installed; a folder that already holds them is used as it is, so build folders can share one |

Building offline: `-DFETCHCONTENT_SOURCE_DIR_SDL2=<dir>` and `-DFETCHCONTENT_SOURCE_DIR_SDL2_MIXER=<dir>` take SDL
from source folders you already have.

## Linux

A 64-bit Linux with GCC 9+ or Clang 10+, CMake 3.22 or newer, Ninja (or make), git and the development headers SDL
builds against, so that the game can use them at run time (SDL loads them when it starts, they are not linked). On
Debian and Ubuntu:

    sudo apt install build-essential cmake ninja-build git wget libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev \
        libxcursor-dev libxi-dev libxss-dev libxfixes-dev libwayland-dev libxkbcommon-dev wayland-protocols \
        libdecor-0-dev libasound2-dev libpulse-dev libdbus-1-dev libudev-dev

`update-workspace-linux.sh [Debug|Release]` configures the `linux-dev` or `linux-release` preset (with
`compile_commands.json`); then

    cmake --build --preset linux-release

writes `out/build/linux-release/bin/tutankhamun` with `config/` beside it. SDL2 and SDL2_mixer are built and linked
statically as on Windows, and the C++ run time of the Bink decoder too: the program needs only the C library.
`cmake --install <build dir> --prefix <dir>` writes an installed tree: `bin/tutankhamun`, the configs in
`share/tutankhamun/config`, the desktop entry, the icons and the AppStream metadata.

### The AppImage

    packaging/linux/build_appimage.sh [build dir]

builds Release in `build/appimage` (or the given folder), installs it into an AppDir and packs it with linuxdeploy
(downloaded on the first run; `LINUXDEPLOY=<path>` takes one you have) into `Tutankhamun-x86_64.AppImage`. The
AppImage needs a glibc at least as new as the one it was built with, so build it on the oldest distribution it should
run on: the CI (`.github/workflows/tutankhamun_linux.yml`) builds on Ubuntu 22.04 (glibc 2.34).

## The browser

Emscripten's SDK (4.0.1, as the CI) and Ninja. In its environment (`emsdk_env.sh`, or `emsdk_env.ps1` on Windows):

    emcmake cmake -S . -B out/build/web -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build out/build/web

writes `out/build/web/bin/tutankhamun.html` with its `.js` and `.wasm`. SDL2 and SDL2_mixer are Emscripten's own
ports, the configs are built into the program, and there is no Discord presence. Serve the folder over http (a
`file://` page cannot load the `.wasm`), for instance with `python -m http.server -d out/build/web/bin`, and open
`tutankhamun.html`. The page (`res/web/shell.html`) asks once for the folder of the player's own game; its files are
copied into the browser's IndexedDB, and the saves and `tutankhamun.ini` are written back there while the game runs.
The CI (`.github/workflows/tutankhamun_web.yml`) puts `web_build.zip` (`index.html`, `.js`, `.wasm`) in the release
`nightly` and publishes the page on the repository's GitHub Pages, in `play/`.

## Checking the configs

`tutankhamun --check-configs config` loads every file of the `config` folder and compares it with the tables built
into the program (exit 0 when all are identical); `tutankhamun --export-configs <dir>` writes a fresh set.
