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

## build deps

The application needs SDL3 and FFMPEG. Optionally for building the game for windows (which is cross compiled from linux) youll need additional cross compilation toolchain and if you want to test that on linux, Proton.

### For Ubuntu *26+*

```bash
$ sudo apt-get install git cmake build-essential libsdl3-dev libavformat-dev libavcodec-dev libswscale-dev libswresample-dev
```

### For Ubuntu *24* or earlier, SDL3 is built from source.
<details>
<summary>install the following build deps for SDL3 + FFMPEG</summary>
```bash
$ sudo apt-get install build-essential git make pkg-config cmake ninja-build gnome-desktop-testing libasound2-dev libpulse-dev libaudio-dev libfribidi-dev libjack-dev libsndio-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev libdbus-1-dev libibus-1.0-dev libudev-dev libthai-dev libusb-1.0-0-dev libpipewire-0.3-dev libwayland-dev libdecor-0-dev liburing-dev libavformat-dev libavcodec-dev libswscale-dev libswresample-dev
```
</details>

### For the windows build (from linux)

In addition to the compiler package below you will also need to install steam's "Proton - Experimental"

```bash
$ sudo apt-get install mingw-w64
```


## Build and run

```bash
python3 tools/import_assets.py --from KaRoo.zip   # once: fills game/ from your copy
cmake -S . -B build                               # once; native Linux; uses system SDL3 or fetches it
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
`CMakeLists.txt`). The groups are ordered in `src/layers.yaml`, lowest first: a
group may include only groups on lines above its own. Each group declares the
groups it includes with `target_link_libraries` (PUBLIC when its headers use
them, PRIVATE otherwise), and those links give it the include paths;
`tools/check_layers.py` (the `check-layers` target) checks the links against the
includes and `src/layers.yaml`. To add a group, give it a line in
`src/layers.yaml` in the position its includes allow.

`cmake --build build --target group-graph` draws the groups and their links to
`build/groups.svg` (`tools/group_graph.py`; `--all` for every link rather than
just those not implied by others, `--dot` for Graphviz).

OpenGL headers are for `gl` alone (`cmake/CheckNativeGL.cmake`); everything else
goes through the `RenderDevice` interface (`renderapi/renderdevice.h`), which `gl`
implements and makes with `createGLRenderDevice()`.
SDL and FFmpeg are for the platform groups (`windev`, `audiodev`, `inputdev`, `sysdev`,
`videodev`) alone (`cmake/CheckNativeSDL.cmake`).
