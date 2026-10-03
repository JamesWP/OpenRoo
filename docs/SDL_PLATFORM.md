# The platform groups on SDL3

`windev`, `audiodev`, `inputdev`, `sysdev` and `videodev` now sit on SDL3
instead of Win32, DirectSound, DirectInput and MCI. Rendering is still
Direct3D 9 (`src/d3d`), on the HWND of the SDL window. The point of the
exercise was to find out how much of the platform API surface survives a move
to a second backend, and what would still stop a non-Direct3D renderer.

SDL3.dll ships beside the executable (`tools/fetch_sdl3.sh` fetches the SDL
MinGW package; the build copies the DLL; `launch.sh` copies it into `run/`).
The executable is still a Win32 one with its own `WinMain`.

## Where it lined up

The public headers needed no signature changes except where noted below.

| Group | Became | Notes |
|---|---|---|
| `sysdev` | `SDL_GetTicks`, `SDL_GetPerformanceCounter`, `SDL_GetCurrentTime`, `SDL_getenv` | `tickMs`/`timerMs` now count from SDL's first use, not boot (only differences are used). `timerMs` and `tickMs` are the same clock again. |
| `inputdev` | `SDL_GetKeyboardState` | A 256-entry table maps the game's DirectInput scan codes (saved in `ProgableControl.sav`, so they cannot change) to SDL scancodes. |
| `windev` window | `SDL_CreateWindow`, `SDL_PollEvent`, `SDL_AddTimer` | Focus, key-up and close map one to one onto `WindowHandler`. |
| `windev::messageBox` | `SDL_ShowMessageBox` | |
| `videodev` | `SDL_PushEvent` | Still the stub that "plays" nothing. |
| `audiodev` 2D playback | one `SDL_AudioStream` per `Buffer`/`Stream`/`Music` | Looping is refilled from the stream's callback; the watcher thread and MCI notify messages are gone. |

## Where it did not line up

These are the findings. None was worked around in the game; each is a gap a
generic API (and so a new backend) would have to close.

1. **3D sound has no SDL equivalent.** `Device::setListener*` and
   `Buffer::setPosition`/`set3DEnabled` are accepted and ignored: every sound
   plays 2D, so positional pan and distance falloff are lost (a gameplay-visible
   regression). A port needs its own spatialiser (pan and attenuation computed
   per voice, applied as stream gain) or SDL_mixer. `is3D()`/`reload3D()` are
   now just a flag.
2. **Master volume was the system's, and is now the program's.** The game
   saved the user's wave-out volume at start, set its own, and restored it at
   exit. SDL has no system volume, so `masterVolume`/`setMasterVolume` act on
   this program's own output (mean of the two channels; balance dropped), and
   the "save and restore" in `Game` now only juggles that program-local gain.
   Probably the right thing to delete from the game rather than emulate.
3. **The launcher stays Win32.** `windev/launcher.cpp` is a frameless window
   painted from bitmaps with a combo-box device dialog, all from the resource
   script. SDL has no dialogs or widgets, so it was not ported; `windev`
   still links gdi32 and owns `openroo.rc`. Cross-platform means redrawing it
   with our own UI (the game already has a menu system) or dropping it for the
   in-game options screen. It also lists *Direct3D* adapters and modes through
   `LauncherModel`, so it is tied to item 5 below.
4. **`handle()` is an HWND.** `Window::handle()` still returns the native
   handle because the Direct3D 9 backend wants one. `Window::sdlWindow()`
   was added for a backend that makes its own surface (OpenGL, Vulkan,
   SDL_GPU). Nothing else may use `handle()` once there is a second backend;
   `RenderDevice::Create(void *hWnd, ...)` should take an opaque
   "window" the backend interprets.
5. **The render device API is Direct3D-shaped, not just D3D-implemented.**
   `RenderDevice::Create` takes an `AdapterId` that is a D3D adapter GUID,
   which the launcher writes to `Karoo.cfg`, and a mode index into a list of
   full-screen display modes with Direct3D pixel formats; `Create` makes the
   window full screen, and device-loss recovery (`restore_surfaces`, the
   `lost` flag) leaks into `main.cpp`. SDL wants a window created with the
   right flags before the context exists, and has no notion of device loss.
   Cleaning this up is the largest remaining piece of work.
6. **Window messages as the audio/video callback channel.** `Music` and
   `Player::handleWindowMessage` existed because MCI and the movie player
   reported back by window message. `Music` no longer needs it (it now returns
   false), but the hook remains in `WindowHandler::onNativeMessage` and the
   movie still uses an SDL user event with the old message id. Both should
   become plain callbacks or polled state (`Player::playing()` already is) and
   the hook can go.
7. **`keyName` text differs.** DirectInput gave "Up Arrow", SDL gives "Up".
   Cosmetic; it is what the controls menu shows for a binding.
8. **`asyncKeyState` reads the focused window only.** `GetAsyncKeyState` was
   global; SDL's keyboard state is per window, fed by the event pump. Both
   `asyncKeyState` and `readKeyboard` therefore only change when
   `runMessageLoop` runs, which it does once per frame. `Devices::acquire`,
   `unacquire` and the controller setters are now empty; the mouse and
   controller were never read. Gamepads would be an SDL gamepad API addition.
9. **`sysdev::executablePath()` became `executableDir()`.** SDL gives the
   directory, not the file. Its one use was a log line.
10. **Headless has no window at all.** The old message-only HWND is gone;
    `messageOnly` initialises SDL's events only, `handle()` is NULL, and
    `requestClose` ends the process directly when there is no visible window
    (as before, where the message-only window was not found either).
11. **The window class icon.** The old window loaded the icon from the
    resource script. SDL picks its own; `SDL_SetWindowIcon` with a decoded
    image would be needed for a nicer taskbar entry.

## What is verified, and what is not

- Builds with no warnings; the `check-native-*` rules pass, including the new
  `check-native-sdl`, which keeps SDL out of game code.
- The whole replay suite passes with `--headless` (16/16). That covers
  `sysdev`, the event loop and quit handling, and input *injection*, but
  draws nothing and, with no display, never opens an audio device.
- A real windowed run (`launch.sh --skip-launcher --auto-exit`) opens the SDL
  window, creates the Direct3D 9 device on its HWND, renders the main menu,
  opens the SDL audio device, and shuts down in order through the close request.
- **Not verified:** real key presses (the scan-code table is untested against
  hardware), audible sound and looping music, the launcher dialog in the same
  process as SDL, full-screen focus behaviour under SDL's window procedure,
  and the movie path (a stub in any case).
