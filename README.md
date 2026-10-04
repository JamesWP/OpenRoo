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
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw.cmake  # once; fetches SDL3 and FFmpeg
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

## Known issue: stray black pixels under DXVK on Intel

Under DXVK (Proton's default Direct3D 9 layer) on an Intel HD Graphics 620 with
Mesa ANV, lit and textured meshes get scattered single-pixel black glitches that
flicker from frame to frame.  The same game data renders cleanly on the CPU
Vulkan renderer (lavapipe) and through Wine's own OpenGL Direct3D (wined3d), and
making the device wait for the GPU after the draws of a frame's first stretch
removes them, so it looks like a driver or DXVK batching problem and not a game
or backend one; `INTEL_DEBUG=noccs` does not help, and it resembles
[DXVK #4041](https://github.com/doitsujin/dxvk/issues/4041).  `launch.sh`
therefore runs wined3d by default (`PROTON_USE_WINED3D=1`; set it to 0 for DXVK).

To revisit when the rendering backend changes again: the glitches need no
particular draw, state or depth-stencil setup, and submitting commands after
every draw (about 60 fps against 44 with a full wait) is a fallback if DXVK is
wanted again.

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
SDL and FFmpeg are for the platform groups (`windev`, `audiodev`, `inputdev`, `sysdev`,
`videodev`) alone (`cmake/CheckNativeSDL.cmake`); `docs/SDL_PLATFORM.md` records
what that port found.
