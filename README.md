# Ka'roo on Linux

Ka'roo is a 1990s Windows DirectDraw game running under Linux via Proton/Wine with a patched `ddraw.dll`.

## Quick start

**Build the patched ddraw.dll** (needs the Wine build tree at `/home/james/proton-wine-build`):

```bash
bash build.sh
```

**Launch the game:**

```bash
bash launch.sh
```

## How it works

The game uses DirectDraw 7 / Direct3D 7 and crashes in Wine's `pack_strided_data` when it passes
partially-initialised `D3DDRAWPRIMITIVESTRIDEDDATA` structs. `build.sh` compiles a patched Wine
`ddraw.dll` from the `proton-wine-src` submodule (`JamesWP/wine`, branch `karoo-patches`) and
strips the Wine builtin marker so Proton loads it as a native DLL.

See `CLAUDE.md` for full build/patch details and `log.md` for the crash investigation notes.
