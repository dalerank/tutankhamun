# Tutankhamun

[![Discord](res/badges/discord.svg)](https://discord.gg/HS4njmBvpb)

**Play in the browser: [dalerank.github.io/tutankhamun/play](https://dalerank.github.io/tutankhamun/play/)** (with the files of your own copy of the game)

Tutankhamun is an open-source re-implementation of **Pharaoh + Cleopatra 2.1** (Impressions Games, 2000) in C: the
game written again as a program of its own, `tutankhamun`, that plays on the files of your own copy of the game
in a window (SDL2) on modern Windows, Linux and macOS, on Android and in the browser. It is also a readable
reference for the game's behaviour, mainly to support [Akhenaten](https://github.com/dalerank/Akhenaten).

![Tutankhamun: a city of the Pharaoh campaign](res/screenshots/tutankhamun.png)

Tutankhamun will not run without the original Pharaoh + Cleopatra files. You can buy a digital copy from
[GOG](https://www.gog.com/game/pharaoh_cleopatra) or
[Steam](https://store.steampowered.com/app/564530/Pharaoh__Cleopatra/), or use an original CD-ROM version updated to
2.1.

The goal of the project is to have exactly the same game logic as Pharaoh + Cleopatra, with the same look and feel.
This means that the saved games are compatible with the original game, and any gameplay bugs present in the original
are also present in Tutankhamun. No game assets are part of this repository.

## Running the game

| Platform | Unstable build | How to run and build |
| -------- | -------------- | -------------------- |
| Windows (32-bit) | [![Github Actions](https://github.com/dalerank/tutankhamun/actions/workflows/tutankhamun_windows.yml/badge.svg)](https://nightly.link/dalerank/tutankhamun/workflows/tutankhamun_windows/main/windows_build.zip) | [docs/windows.md](docs/windows.md) |
| Windows x64 | [![Github Actions](https://github.com/dalerank/tutankhamun/actions/workflows/tutankhamun_windows.yml/badge.svg)](https://nightly.link/dalerank/tutankhamun/workflows/tutankhamun_windows/main/windows_build_x64.zip) | [docs/windows.md](docs/windows.md) |
| Linux | [![Github Actions](https://github.com/dalerank/tutankhamun/actions/workflows/tutankhamun_linux.yml/badge.svg)](https://nightly.link/dalerank/tutankhamun/workflows/tutankhamun_linux/main/linux_appimage.zip) | [docs/linux.md](docs/linux.md) |
| macOS arm64 | [![Github Actions](https://github.com/dalerank/tutankhamun/actions/workflows/tutankhamun_macos.yml/badge.svg)](https://nightly.link/dalerank/tutankhamun/workflows/tutankhamun_macos/main/macos_arm_build.zip) | [docs/macos.md](docs/macos.md) |
| macOS x64 | [![Github Actions](https://github.com/dalerank/tutankhamun/actions/workflows/tutankhamun_macos.yml/badge.svg)](https://nightly.link/dalerank/tutankhamun/workflows/tutankhamun_macos/main/macos_x64_build.zip) | [docs/macos.md](docs/macos.md) |
| Android | [![Github Actions](https://github.com/dalerank/tutankhamun/actions/workflows/tutankhamun_android.yml/badge.svg)](https://nightly.link/dalerank/tutankhamun/workflows/tutankhamun_android/main/android_apk.zip) | [docs/android.md](docs/android.md) |
| Web | [![Github Actions](https://github.com/dalerank/tutankhamun/actions/workflows/tutankhamun_web.yml/badge.svg)](https://dalerank.github.io/tutankhamun/play/) | [docs/web.md](docs/web.md) |

The unstable build is the latest build of `main`:

- **Windows**: a zip with `tutankhamun.exe` and its `config` folder, unpack it anywhere;
- **Linux**: `Tutankhamun-x86_64.AppImage`, one file for 64-bit distributions with glibc 2.34+;
- **macOS**: a disk image with `Tutankhamun.app` for Apple silicon (11.7+) or Intel (10.15+), signed ad hoc;
- **Android**: `app-release.apk` for Android 5.0+, played with touch;
- **Web**: [the game's page](https://dalerank.github.io/tutankhamun/play/) in a desktop browser; the folder of your
  game is copied into the browser once.

You need an installed Pharaoh + Cleopatra 2.1; only its data is used. The game finds the installation itself (the
Steam and GOG folders, the folder it remembers, then a folder picker) or takes it on
[the command line](docs/command-line.md). Like the original it writes the players and the saved games into the game
folder, in the original format.

**Free play**: the main menu's second button asks for the family, whose folder keeps the saved games, then lists
every Pharaoh and Cleopatra mission; its end returns to the main menu without changing the campaign progress, records
or high scores.

**Resolutions**: any size from 640x480 up to the display's; F7 / F8 / F9 and F6 (full screen) work as in the
original.

**Discord**: on the desktop the game shows the current mission in your Discord status (`config/discord.json` turns
it off).

## Building from source

Every platform builds from the same CMake project, with SDL2 and SDL2_mixer fetched and linked statically:
`build.bat` on [Windows](docs/windows.md#building), presets and an AppImage script on [Linux](docs/linux.md#building),
presets and a disk image script on [macOS](docs/macos.md#building), Gradle on [Android](docs/android.md#building)
(and [Google Play](docs/android.md#google-play)), Emscripten for [the browser](docs/web.md#building). [BUILD.md](BUILD.md)
has the details.

## Contributing

This project is not meant for further development: it exists only to preserve the heritage of the original game and
to port it to other platforms. Pull requests are accepted only for minimal fixes. See
[CONTRIBUTING.md](CONTRIBUTING.md).

## Credits

Parts of the code come from other projects (details: [LICENSES/README.md](LICENSES/README.md) and each file's
header):

- [Akhenaten](https://github.com/dalerank/Akhenaten): the Bink video decoder (src/third_party/akhenaten_bink), the SDL
  build scripts (cmake/), the Discord presence, the mouse cursors, the Discord and Patreon pictures of the main menu's
  link buttons, the model of the resolution list and of the link buttons;
- [Julius](https://github.com/bvschaik/julius): src/platform/sound_device.c and src/platform/screen.c, the cursors'
  pixel art, and the select list that the resolution list draws like (src/game/window/display_resolution_list.c);
- the GitHub picture of the link buttons is the Simple Icons mark (CC0-1.0).

Third-party libraries: SDL2 2.32.10 (zlib), SDL2_mixer 2.8.0 (zlib) with minimp3 (CC0-1.0), parson 1.5.3 (MIT,
src/third_party/parson); their licence texts are in LICENSES/.

## License

The project's own code is under the MIT License ([LICENSE](LICENSE)); the files taken from Julius and Akhenaten keep
their AGPL-3.0 ([LICENSES/AGPL-3.0.txt](LICENSES/AGPL-3.0.txt), listed in LICENSES/README.md with the libraries'
licences). A program built with those files, as every build of tutankhamun is, is distributed under AGPL-3.0 as a
whole.

Tutankhamun is not affiliated with or endorsed by the owners of Pharaoh and Cleopatra. The game's data, names and
trademarks belong to their owners; you need your own copy of the game to play.
