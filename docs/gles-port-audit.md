# GLES port audit (for RG35XX Plus, stock firmware)

Scope: what stops OpenRoo's renderer, which targets **OpenGL 3.3 core**, from
running on **OpenGL ES 3.x** (Mali-G31 via SDL3). Based on a read of `src/gl/*`,
`src/windev/window.cpp` and the CMake files. Nothing was compiled or run for
ARM; every item is from reading code.

## Headline

The port is small. All GL is behind `src/gl` (2.2k lines), a CMake check
(`check-native-gl`) stops GL leaking elsewhere, entry points are loaded by name
through `SDL_GL_GetProcAddress` (nothing links libGL), and the draw path is one
shader program with UBOs and a sampler object, which is all ES 3.0. Expect
roughly: shader header, 3 enum/format tweaks, a few entry-point swaps, plus a
context-profile switch. The bigger risks are **non-GL**: the SDL3 video
backend on the stock firmware, FFmpeg, and 32-bit/64-bit assumptions.

## A. Must change for GLES 3.0

| # | Where | Issue | Fix |
|---|---|---|---|
| 1 | `window.cpp:171-173`, `createdevice.cpp` (`want = {3,3,24,8}`) | Requests a 3.3 **core** profile. | For ES: `SDL_GL_CONTEXT_PROFILE_MASK = SDL_GL_CONTEXT_PROFILE_ES`, version 3.0 (or 3.2 if the driver gives it). Make it part of `GLContextConfig`. |
| 2 | `shaders.cpp` `compile()` | Source starts `#version 330 core`; no precision qualifiers. | Prefix `#version 300 es` + `precision highp float; precision highp int;` (highp is fine in the fragment stage on G31). Body is already ES-3.0-clean: `layout(std140)` UBOs, `layout(location)`, `in/out`, `texture()`. Check `1e-6` literal and `int()` casts compile (they should). |
| 3 | `renderdevice.cpp:437,444` | `VertexAttribPointer(..., GL_BGRA, ...)` — BGRA vertex-attribute size is `GL_ARB_vertex_array_bgra`, **not in ES**. | Pass size 4 and swizzle `.bgra` in the vertex shader (colour and specular), or swizzle on CPU when filling the stream. Shader swizzle is cheapest; keep `GL_TRUE` normalise. |
| 4 | `glapi.h` `ClearDepth` ; `createdevice.cpp:134` | ES has `glClearDepthf`, no `glClearDepth`. | Switch to `ClearDepthf` (valid on desktop 4.1+/ARB too; for the 3.3 build keep a thin shim). |
| 5 | `createdevice.cpp:131` | `glDisable(GL_MULTISAMPLE)` — enum absent in ES (would raise `GL_INVALID_ENUM`). | Wrap in `#ifndef`/runtime “is ES” flag and skip. |
| 6 | `renderdevice.cpp:173,240` | `GL_CLAMP_TO_BORDER` + `GL_TEXTURE_BORDER_COLOR`: core only in **ES 3.2** (or `OES/EXT_texture_border_clamp`). | Check whether the game ever uses `AddressMode::Border` (grep call sites); if not, drop it. Otherwise map to `CLAMP_TO_EDGE`, or emulate in the shader. |
| 7 | `renderdevice.cpp:899` | `glGetBufferSubData` — not in ES 3.0 (only used when draw-trace is on). | Trace-only: `#ifdef` it out for ES, or use `glMapBufferRange(GL_MAP_READ_BIT)`. |
| 8 | `glapi.h` | Includes `<GL/glcorearb.h>`. | Add `<GLES3/gl3.h>` path (Khronos headers vendored, no libGLES link needed since procs are loaded by name). |
| 9 | `renderdevice.cpp:64` | `Prim::TriangleFan` → `GL_TRIANGLE_FAN`. | Supported in ES. Fine. |

## B. Verified fine for ES 3.0

`glDrawElements` with `GL_UNSIGNED_SHORT`; UBOs with std140 and
`BindBufferBase`; sampler objects; VAOs; `GL_RGBA8` + `GL_RGBA/UNSIGNED_BYTE`
texture upload (the code deliberately widens all formats to RGBA8 on the CPU —
`devicetexture.cpp` — so no ES format-support worries); `GL_TEXTURE_MAX_LEVEL`
(ES 3.0 ok); `ReadPixels` `GL_RGB/UNSIGNED_BYTE` with pack alignment 1
(spec-guaranteed for ES); blend factors incl. `SRC_ALPHA_SATURATE`; stencil;
orphaning the stream buffer with `BufferData(NULL)`.

## C. Performance / memory risks on Mali-G31 (unmeasured)

- **Per-vertex lighting and fog** are cheap; fragment shader is light. OK.
- `GL_STREAM_DRAW` 4 MiB + 1 MiB ring buffers with orphan-on-full: fine for
  Mali, but Mali dislikes `glBufferSubData` on in-flight UBOs every draw
  (`DrawBlock` update per draw, `renderdevice.cpp` ~L409). Profile draws/frame;
  consider a per-frame UBO ring if CPU-bound.
- State is already lazily set with dirty flags — good for a tile-based GPU.
- Framebuffer is the default one; no render-to-texture → no tiler flushes. Good.
- 1 GB shared RAM: textures are RGBA8-widened even for 16-bit formats (2x
  overhead). Measure peak texture memory per level before optimising.
- Stencil: requested 8-bit stencil + 24-bit depth; confirm the EGL config on
  device offers it (some Mali setups only give D24S8 via specific configs).

## D. Non-GL blockers (likely bigger than section A)

1. **SDL3 on the stock firmware.** The README builds SDL3 from source
   (`cmake/FetchSDL.cmake`, `SDL_SHARED OFF`, static). On Linux SDL3 needs a
   video backend: KMSDRM/GBM + EGL, or a vendor path. The stock OS's Mali
   userspace may be fbdev/vendor-EGL (no GBM) in which case stock SDL3 can’t
   open a GL window → need to inspect the firmware (`libmali*`, `libgbm*`,
   `libdrm*`, `/dev/dri`, how its bundled emulators open GL). **This is the
   gating question; do the firmware exploration before any code.**
2. **FFmpeg** (`cmake/FetchFFmpeg.cmake`) uses the system's dev packages on
   Linux via pkg-config — need an ARM sysroot build of libav* or a build option
   to disable the intro movie (`src/videodev`).
3. **Launcher window** (`windev/launcher.cpp`) uses `SDL_CreateRenderer(...,
   SDL_SOFTWARE_RENDERER)` and a borderless always-on-top window; on a
   single-fullscreen KMSDRM device it should be skipped (or the launcher
   replaced by config). Also `SDL_PROP_WINDOW_WIN32_HWND_POINTER` use is
   harmless but Windows-only in intent.
4. **Display modes / aspect.** The mode list keeps only 4:3 modes (`list_modes`)
   and the device is 640x480 so that works, but the 16-bit “modes” are only
   CPU quantisation. Fullscreen uses the desktop mode (`enterFullscreen`
   ignores requested size) — good for a fixed-panel device.
5. **Input.** `inputdev/sdlinput.cpp` — check gamepad mapping for the RG35XX
   Plus’ built-in controls (SDL gamepad DB entry or custom mapping) and menu
   key to quit. Not audited in depth.
6. **64-bit / ARM cleanliness.** No SIMD intrinsics or inline asm found; no
   packed-struct pragmas. A few `uintptr_t` ↔ pointer casts (`ui/theme.cpp`
   uses `>= 0x10000` as a "valid pointer" heuristic; `audio/soundobj.cpp:91`
   truncates a pointer to `unsigned`) are 32-bit-int-truncation spots to review
   for **aarch64**; if the stock userland is **armhf** pointers are 32-bit and
   these are fine. Data-file endianness is a non-issue (ARM Linux is LE).
7. **Build system.** `-Wall -Wextra -Werror` plus a toolchain file: expect new
   warnings from an older cross GCC. `tools/replaytest.py` replays recorded
   sessions with `KAROO_HEADLESS` — run it on the *host* build; it can’t
   validate GLES.

## E. Suggested order

1. Explore stock firmware (video stack, libc, SDL, Mali) → decide ABI +
   SDL video backend (report §5).
2. Cross-compile an SDL3 “GLES clear + triangle” test; run on device.
3. Introduce a GL-flavour switch in `src/gl` (profile, headers, shader
   header, items A1–A8) behind a CMake option `OPENROO_GLES`, so the desktop
   path is unchanged and replay tests keep guarding it.
4. Cross-compile the full game with FFmpeg off; fix section-D items.
5. Profile on device; only then touch section C.

## Open questions for the owner

- Is the intro movie required on device (FFmpeg cost)?
- Is `AddressMode::Border` ever hit at runtime?
- Preferred: keep one codebase with an `OPENROO_GLES` flag (recommended), or
  fork a separate backend?
