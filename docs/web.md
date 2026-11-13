# Tutankhamun in the browser

[← README](../README.md)

## Running

Open [dalerank.github.io/tutankhamun/play](https://dalerank.github.io/tutankhamun/play/) in a desktop browser (a recent
Chrome, Edge or Firefox) and choose the folder of your installed Pharaoh + Cleopatra, the one that holds `Data`, or drop
it on the page. Its files are copied into the browser's own storage (IndexedDB) once: nothing is uploaded anywhere,
and the next visit starts with the Play button. The programs, manuals and archives of the folder are left out; the
rest is several hundred megabytes, so the first copy takes a while and the browser may ask whether the site may use
that much room.

The game writes the players, the saved games and `tutankhamun.ini` into the browser's storage, every few seconds while
it runs, when the tab is hidden and when the game ends. **Saves** on the page downloads a saved game, **Choose another
folder** replaces the game's files, **Delete all files** removes them and every save from the browser. Clearing the
site's data in the browser deletes them too.

The page is the latest build of `main`, the same game as on the desktop, without the Discord presence.

## Building

Emscripten's SDK 4.0.1 and Ninja; in the SDK's environment (`emsdk_env.sh`, or `emsdk_env.ps1` on Windows):

    emcmake cmake -S . -B out/build/web -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build out/build/web
    python -m http.server -d out/build/web/bin

and open `http://localhost:8000/tutankhamun.html` (a `file://` page cannot load the `.wasm`). SDL2 and SDL2_mixer are
Emscripten's ports and the configs are built into the program; the game's own loops give the browser its turn through
ASYNCIFY. The page is `res/web/shell.html`. The CI (`.github/workflows/tutankhamun_web.yml`) builds it on every push,
puts `web_build.zip` in the release `nightly` and publishes it on the repository's GitHub Pages.
