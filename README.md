# Ka'roo on Linux

Ka'roo is a 1990s Windows DirectDraw game running under Linux via Proton/Wine.

## Quick start

**Build the hooks DLL and patch the game binary:**

```bash
bash build.sh
```

**Launch the game:**

```bash
bash launch.sh
```

## How it works

The game uses DirectDraw 7 / Direct3D 7. It used to crash in Wine's `pack_strided_data` when it
passed a partially-initialised `D3DDRAWPRIMITIVESTRIDEDDATA` struct; that is fixed in the game's
own code path now (see `CRASH.md`), so it runs on stock Wine ddraw. `build.sh` builds
`karoo_hooks.dll` and applies the binary patches in `patch.py`.

See `CLAUDE.md` for full build/patch details and `log.md` for the crash investigation notes.

## Notes on importing types into ghidra

Have a checked out version of wine, build and install headers somewhere DESTDIR=/dir make install

Then open Ghidra Parse C Source window.

Use VistualStudio9.prf
clear out default include paths
Add include paths to /dir/usr/local/include/wine/windows
Add include paths to /dir/usr/local/include/wine/msvcrt

add two includes to top of SourceFiles to parse: /dir/usr/local/include/wine/windows/windows.h
                                                 /dir/usr/local/include/wine/windows/dinput.h

(or whatever you want to replace dinput.h with)

click parse to program and then 'Use Open Archives' if prompted
