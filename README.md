# Open'Roo

A reimplementation of **Ka'roo**, a 1990s Windows 3D puzzle-platform game,
written from scratch in C++.  It builds natively for Linux (`OpenRoo`) and
for Windows (`OpenRoo.exe`, with MSVC).  The platform is a configure option,
`OPENROO_PLATFORM` (default: the host's); all
platform-specific code lives in the `*dev` libraries under `src/` (`windev`,
`sysdev`, `audiodev`, `inputdev`, `videodev`, `gl`), one header each with a
`_windows` and a `_linux` source file behind it.

Open'Roo contains **no game data and no code from the original**. You need
your own copy of Ka'roo: the importer takes the levels, models, textures,
sounds and the original executable's resources from it.

## build deps

The application needs SDL3 and FFMPEG.  On Linux they come from the system; on Windows from vcpkg.

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

### For Windows

Build natively, from PowerShell.  You need Visual Studio 2022 (the "Desktop
development with C++" workload), CMake, Python 3 and [vcpkg](https://vcpkg.io).
The dependencies (SDL3, SDL3_mixer, FFmpeg) are listed in `vcpkg.json` and are
built by the first configure, which takes a while.

```powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg; C:\vcpkg\bootstrap-vcpkg.bat   # once
$env:VCPKG_ROOT = "C:\vcpkg"
```

## Build and run

```bash
python3 tools/import_assets.py --from KaRoo.zip   # once: fills game/ from your copy
cmake -S . -B build                               # once; native Linux; uses system SDL3 or fetches it
cmake --build build -j                            # build/OpenRoo
bash launch.sh                                    # run it (in run/)
bash launch.sh --headless --auto-exit 30          # no display at all
```

On Windows, from PowerShell:

```powershell
python tools/import_assets.py --from KaRoo.zip    # once
cmake --preset windows                            # once; vcpkg builds the dependencies
cmake --build build --preset windows              # build/RelWithDebInfo/OpenRoo.exe
.\launch.ps1                                      # run it (in run\); -Headless, -AutoExit 30
```

The game file defaults to `JJ`; name another on the command line.

`game/` is your imported data and is never written. `run/` is where the game
runs: your config, saves, high scores and logs.

### Without a copy of Ka'roo

```bash
python3 tools/gen_assets.py                       # fills game/ with generated placeholders
```

`tools/gen_assets.py` makes every file the game needs from nothing: flat
colours, boxes, beeps, particle effects, a 5x7 font, and one theme, Basic,
with six small levels (`tools/freeassets/levels.py`, drawn as text, with a
`.leo` block on each impassable cell).  It writes under the
names the original install used, so the game reads it unchanged.  It never
deletes a `game/` it did not make; a `game/` that is a symlink (a worktree's)
is replaced, link only, with `--force`.  The replay suite records the original
data, so it does not pass on these; the bot plays them:
`python3 tools/bot/backtest.py run`.

## RG35XX Plus (muOS)

The Anbernic RG35XX Plus handheld is a cross-compiled target, built with the
SDK from [rg35xx-plus-sdk](https://github.com/JamesWP/rg35xx-plus-sdk) (its
toolchain, and an SDL3 with a video driver for the device's Mali GPU).  Build
that, or unpack its SDK tarball, then:

```bash
cmake -S . -B build-rg35xx -DOPENROO_PLATFORM=rg35xx -DRG35XX_SDK=<sdk>   # <sdk>: the unpacked SDK, or that repo's output/host
cmake --build build-rg35xx -j --target rg35xx-package                       # build-rg35xx/rg35xx/OpenRoo/
```

Copy `build-rg35xx/rg35xx/OpenRoo/`, and the contents of your imported
`game/` beside it, to `MUOS/application/OpenRoo/` on the SD card (`adb push`
works); it is then under **Applications**.  `video/` can be left out: the
SDK has no FFmpeg, so this build skips the intro movie (`OPENROO_VIDEO`).

The controls (any SDL gamepad on the other platforms too):

| Button | Key | In the game |
|---|---|---|
| D-pad | arrows | move, turn; menus |
| A | Return | confirm |
| B | B | bomb |
| X / Y | Page Up / Page Down | tilt the camera |
| L1 / R1 | X / C | rotate the camera |
| L2 / R2 | Z / A | zoom out / in |
| Select | Tab | overview |
| Start | Escape | pause, back |
| Menu | Delete | harakiri |

## Tests

```bash
python3 tools/replaytest.py --headless   # recorded inputs, asserted end state
```

The harness runs the Linux build; it does not yet drive the Windows one.

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
