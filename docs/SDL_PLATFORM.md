# The platform groups on SDL3

`windev`, `audiodev`, `inputdev`, `sysdev` and `videodev` sit on SDL3 (plus
SDL3_mixer for sound and FFmpeg for the intro movie) instead of Win32,
DirectSound, DirectInput, MCI and the Win32 dialogs. Rendering is still
Direct3D 9 (`src/d3d`), on the HWND of the SDL window. The point of the
exercise was to find out how much of the platform API surface survives a move
to a second backend, and what would still stop a non-Direct3D renderer.

## Build

`cmake/FetchSDL.cmake` and `cmake/FetchFFmpeg.cmake` download the MinGW
development packages (SDL3, SDL3_mixer, a shared LGPL FFmpeg build) at
configure time into `build/_deps/` and define the imported targets `sdl3`,
`sdl3_mixer` and `ffmpeg`. Pass `-DSDL3_ROOT`, `-DSDL3_MIXER_ROOT` or
`-DFFMPEG_ROOT` to use a copy you already have. Their DLLs are copied beside
the executable (`launch.sh` copies them into `run/`). The FFmpeg build is
pinned to a BtbN autobuild tag (`FFMPEG_TAG`); if that tag is ever removed
upstream, point `FFMPEG_TAG`/`FFMPEG_FILE` at a current one.

`check-native-sdl` keeps SDL and FFmpeg headers inside the platform groups.
The executable is still a Win32 one with its own `WinMain`, but it has no
Win32 resources: `data/openroo.rc` is gone. The launcher's artwork and the
window icon are generated into C arrays by `tools/slice_launcher.py`, the
name and version come from CMake. (Lost with the resources: the icon shown
for `KarooOwn.exe` in a file manager, and the version info on its properties
page.)

## Where it lined up

| Group | Became | Notes |
|---|---|---|
| `sysdev` | `SDL_GetTicks`, `SDL_GetPerformanceCounter`, `SDL_GetCurrentTime`, `SDL_getenv`, `SDL_Delay` | `tickMs`/`timerMs` count from SDL's first use, not boot (only differences are used). |
| `inputdev` | `SDL_GetKeyboardState` | A 256-entry table maps the game's DirectInput scan codes (saved in `ProgableControl.sav`, so they cannot change) to SDL scancodes. |
| `windev` window | `SDL_CreateWindow`, `SDL_PollEvent`, `SDL_AddTimer` | Focus, key-up and close map one to one onto `WindowHandler`. |
| `windev::messageBox` | `SDL_ShowMessageBox` | |
| `windev` launcher | an SDL window, software renderer, our own bitmaps | See below. |
| `audiodev` | SDL3_mixer: tracks, with 3D positions | Two mixers, so music and effects each have a volume. |
| `videodev` | FFmpeg decoding, SDL audio stream | The game draws each frame with `RenderDevice::PresentImage`. |

### Sound

- **Volumes.** `audiodev::setMusicVolume` and `setEffectsVolume` (0..1) replace
  the system-wide `masterVolume`/`setMasterVolume`; they affect only this
  program. The options menu's CD volume drives the music mixer (the old CD
  mixer line did nothing) and the wave volume drives the effects mixer. The
  game no longer saves and restores the user's system volume; the
  `saved*Volume` fields stay in the config file for its format.
- **3D sound.** SDL_mixer's listener is fixed at the origin and it has no
  rolloff. `audiodev` keeps the real listener (position, front, top,
  rolloff) and hands the mixer each source's position relative to it, with
  distance scaled by the rolloff factor. Distance falloff is SDL_mixer's own
  curve, not DirectSound's, so levels will differ.
- **Switching 3D on or off** (the Audio options) rebuilds every 3D sound in
  DirectSound, which silenced anything playing; `Buffer::reload3D` therefore
  stops the track when the mode changes. The game relies on this: it starts
  its looping level sounds again afterwards and drops the old handles, so a
  loop left playing could never be stopped (the bees and cuckoo that carried
  into later levels).
- **Music** loops inside the mixer; the window-message hook
  (`Music::handleWindowMessage`) is now vestigial and returns false.
- Sounds are decoded by SDL_mixer, so `audiodev` no longer has its own `.wav`
  parser.

### Intro movie

`videodev::Player::load(window, path, show)` decodes `video/INTRO.AVI`
(Cinepak) with FFmpeg as it plays, converts frames to RGBA and queues the
sound on an SDL audio stream. `main.cpp` presents each new frame itself. Any key
skips it, but only one pressed *during* the intro: the release of the key
that started the game (the launcher's Enter) used to end it after one frame.
The
movie is **not shown** (it "plays" for no time, as the old stub did) when the
run is headless, a replay, or an autoplay policy, so recorded runs are
unchanged.

### Launcher

The launcher is a borderless, always-on-top SDL window of its own, drawn with
SDL's software renderer: the bitmap buttons, the painted title bar (dragged
with `SDL_SetWindowHitTest`), minimise and close boxes, and the project link.
The device dialog became a panel over it with two lists (adapter, mode) drawn
with SDL's built-in debug font, mouse and keyboard driven.

**Why not `SDL_CreatePopupWindow`:** popup windows are for menus and
tooltips. They require a visible parent (ours stays hidden until the game
starts), are positioned relative to it, and are dismissed when the pointer or
focus leaves them, which a start-up dialog must not be. So the launcher is a
normal window; the device panel is not a second window at all.

Differences from the Win32 version: adapters are listed in the model's order,
not sorted; the "hardware" check box (always checked, never read) is gone;
the panel text is SDL's 8-pixel debug font, which is plain but not themed.

## Where it did not line up

These are what a second rendering backend still has to deal with.

1. **`handle()` is an HWND.** `Window::handle()` returns the native handle
   because the Direct3D 9 backend wants one. `Window::sdlWindow()` was added
   for a backend that makes its own surface (OpenGL, Vulkan, SDL_GPU). Once
   there is a second backend nothing else should use `handle()`;
   `RenderDevice::Create(void *hWnd, ...)` should take an opaque window that
   the backend interprets.
2. **The render device API is Direct3D-shaped, not just D3D-implemented.**
   `RenderDevice::Create` takes an `AdapterId` that is a D3D adapter GUID,
   which the launcher writes to `Karoo.cfg`, and a mode index into a list of
   full-screen display modes with Direct3D pixel formats; `Create` makes the
   window full screen, and device-loss recovery (`restore_surfaces`, the
   `lost` flag) leaks into `main.cpp`. SDL wants a window created with the
   right flags before the context exists, and has no notion of device loss.
   The launcher's `LauncherModel` (adapters and modes) inherits the same
   shape. This is the largest remaining piece of work.
3. **Window messages as the audio/video callback channel.** The hook
   `WindowHandler::onNativeMessage` and `handleWindowMessage` on `Music` and
   `Player` existed because MCI and the movie player reported back by window
   message. Music no longer needs it; the movie only uses it for the
   not-shown stub (an SDL user event with the old message id). Both should
   become plain callbacks or polled state (`Player::playing()` already is)
   and the hook can go.
4. **`keyName` text differs.** DirectInput gave "Up Arrow", SDL gives "Up".
5. **`asyncKeyState` reads the focused window only.** `GetAsyncKeyState` was
   global; SDL's keyboard state is per window, fed by the event pump. Both
   `asyncKeyState` and `readKeyboard` therefore change only when
   `runMessageLoop` runs, which it does once per frame. `Devices::acquire`,
   `unacquire` and the controller setters are now empty; the mouse and
   controller were never read. Gamepads would be an SDL gamepad addition.
6. **`sysdev::executablePath()` became `executableDir()`.** SDL gives the
   directory, not the file. Its one use was a log line.
7. **Headless has no window at all.** The old message-only HWND is gone;
   `messageOnly` initialises SDL's events only, `handle()` is NULL, and
   `requestClose` ends the process directly when there is no visible window
   (as before).
8. **The FFmpeg dependency is large** (about 80 MB of package, five DLLs) for
   one 30-second Cinepak intro. If the intro is ever re-encoded, a much
   smaller decoder would do.

## What is verified, and what is not

- Builds clean from an empty build directory (the fetch modules download
  everything); all `check-native-*` rules pass.
- The whole replay suite passes with `--headless` (16/16), and a windowed
  replay (`replaytest.py bombstart-crash`) passes and skips the intro.
- Windowed runs: the SDL window, the Direct3D 9 device on its HWND, the main
  menu, shutdown through the close request. The launcher (checked by
  screenshot and scripted clicks): artwork, button focus, the device panel
  with the real adapter and modes, choosing a mode (it reached the renderer
  as 1152x864x32), Play. The intro movie presents about 24 frames a second.
- **Not verified, because nothing here can listen or press hardware keys:**
  that any sound is audible, music looping, 3D panning direction and
  distance (the listener handedness conversion is from reading the docs),
  the volume menus, the intro's audio staying in sync with its picture,
  real key presses (the scan-code table is untested on hardware), and
  full-screen focus behaviour under SDL's window procedure. The Quit
  button's synchronous sound is also unchecked.
