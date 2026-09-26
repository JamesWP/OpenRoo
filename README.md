# Ka'roo on Linux

Ka'roo is a 1990s Windows DirectDraw game running under Linux via Proton/Wine.

## Quick start

**Build the game:**

```bash
cd karoo-hooks && make
```

**Launch it:**

```bash
bash launch.sh
```

## How it works

`KarooOwn.exe` is a reimplementation of the game, built from `karoo-hooks/`.
It contains no code from the original executable and runs on stock Wine
ddraw. The earlier route — patching `Karoo.exe` to call into a hooks DLL — is
retired; the last commit that builds it is tagged `hybrid-final`.

See `CLAUDE.md` for details and `OPEN_PLAN.md` for the current plan.

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

## Cheats
     "mausuruh"  Game+0x175402 += 1     -- one extra life
     "boommaker" Game+0x1752b1 += 10
     "sportsman" Game+0x1752b2 += 1
     "kaputo"    every live foe's +0x11f = 4
     "supa"      completes the level (bGame_state 3, menu node 0x28), or
                 game over when it is the last level
     "notme"     sets Game+0x1753bb and marks the player's tile
     "jjmap"     load a level by name      "jjmapnr"  load a level by number
   
