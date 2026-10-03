# Open'Roo

A reimplementation of **Ka'roo**, a 1990s Windows 3D puzzle-platform game,
written from scratch in C++. It builds to a Windows executable,
`KarooOwn.exe`, which runs on Linux under Proton/Wine.

Open'Roo contains **no game data and no code from the original**. You need
your own copy of Ka'roo: the importer takes the levels, models, textures,
sounds and the original executable's resources from it.

## Build and run

Needs `x86_64-w64-mingw32-g++`, `cmake` (3.20+), Python 3 and Proton (Steam's
"Proton - Experimental").

```bash
python3 tools/import_assets.py --from KaRoo.zip   # once: fills game/ from your copy
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw.cmake  # once
cmake --build build -j                            # build/KarooOwn.exe
bash launch.sh                                    # run it (in run/)
bash launch.sh --headless --auto-exit 30          # no display at all
```

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
layering and the compiler enforces them. Direct3D headers
are for `d3d` alone (`cmake/CheckNativeD3D.cmake`); everything else goes through `renderdevice.h`.
