# Open'Roo

A reimplementation of **Ka'roo**, a 1990s Windows 3D puzzle-platform game,
written from scratch in C++. It builds natively for Linux (`OpenRoo`) and
cross-compiles to Windows (`OpenRoo.exe`, which also runs on Linux under
Proton/Wine).  The platform is a configure option, `OPENROO_PLATFORM` (default `linux`); all
platform-specific code lives in the `*dev` libraries under `src/` (`windev`,
`sysdev`, `audiodev`, `inputdev`, `videodev`, `gl`), one header each with a
`_windows` and a `_linux` source file behind it.

Open'Roo contains **no game data and no code from the original**. You need
your own copy of Ka'roo: the importer takes the levels, models, textures,
sounds and the original executable's resources from it.

## Build and run

Needs `cmake` (3.20+) and Python 3, and then for the Windows build
`x86_64-w64-mingw32-g++` and Proton (Steam's "Proton - Experimental"); for the
Linux build `g++`, the FFmpeg development packages (`libavformat-dev
libavcodec-dev libavutil-dev libswscale-dev libswresample-dev`), `libgl-dev` and
what SDL3 builds against (SDL3 and SDL3_mixer are fetched and built from source;
see https://wiki.libsdl.org/SDL3/README-linux#build-dependencies).  The game draws with OpenGL 3.3 (core profile, GLSL
shaders, no fixed-function pipeline), through Wine's OpenGL under Proton, so the
graphics driver needs to offer it.

```bash
python3 tools/import_assets.py --from KaRoo.zip   # once: fills game/ from your copy
cmake -S . -B build                               # once; native Linux; fetches SDL3
cmake --build build -j                            # build/OpenRoo
bash launch.sh                                    # run it (in run/)
bash launch.sh --headless --auto-exit 30          # no display at all

cmake -S . -B build -DOPENROO_PLATFORM=windows    # instead: the Windows build (build/OpenRoo.exe)
```

`launch.sh` runs whichever platform `build/` was configured for (Windows
builds under Proton).

The game file defaults to `JJ`; name another on the command line.

`game/` is your imported data and is never written. `run/` is where the game
runs: your config, saves, high scores and logs.

## Tests

```bash
python3 tools/replaytest.py --headless   # recorded inputs, asserted end state
python3 tools/levelreport.py             # all 80 levels loaded and counted
```

See `docs/TESTING.md`. How the code is organised: `docs/DESIGN.md`.

## Licence

Open'Roo is **source-available, non-commercial**: everything in this
repository is licensed under
[CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/) (`LICENSE`).
It is not "open source" in the OSI sense.

Ka'roo, its name and its assets belong to their rights holders. Open'Roo is
an unofficial fan project and is not affiliated with them.

## Component groups

Each directory under `src/` is a CMake group (an object library in its
`CMakeLists.txt`). A group can include only its own
headers and those of the groups in its `DEPENDS` list, so those lists are the
layering and the compiler enforces them. OpenGL headers
are for `gl` alone (`cmake/CheckNativeGL.cmake`); everything else goes through `renderdevice.h`.
SDL and FFmpeg are for the platform groups (`windev`, `audiodev`, `inputdev`, `sysdev`,
`videodev`) alone (`cmake/CheckNativeSDL.cmake`).
