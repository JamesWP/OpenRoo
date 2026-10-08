# RG35XX Plus: cross-compilation environment report (SDL3 + GLES)

> Update: the project uses **SDL3** (fetched and built statically, `cmake/FetchSDL.cmake`) and an
> OpenGL 3.3 core renderer; where this report says SDL2, read SDL3. The owner has the **stock
> firmware** installed, so Track A (§2) is the primary target. See `docs/gles-port-audit.md`
> for the renderer/code audit.

Status: research/plan only. Nothing here has been run on a device yet.
Confidence is marked per claim: **[src]** = seen in a web source during research,
**[mem]** = from background knowledge, **[verify]** = must be confirmed on hardware.

## 1. Target summary

| Item | Value |
|---|---|
| SoC | Allwinner H700, 4x Cortex-A53 (ARMv8) **[mem]** |
| GPU | Mali-G31 MP2 (Bifrost), GLES 3.x capable **[mem]** |
| RAM / screen | 1 GB LPDDR4, 640x480 IPS **[mem]** |
| Stock OS | Anbernic's own Linux, booted from SD card **[src]** |
| Stock userland ABI | **Probably 32-bit armhf** (Anbernic's SDK is `arm-buildroot-linux-gnueabihf`) **[src, verify]** even though the CPU/kernel can be 64-bit |
| Custom firmwares | muOS, Knulli etc. ship a 64-bit aarch64 userland **[src/mem]** |

The single most important early question is **which ABI and GL stack we target**:
stock firmware (likely armhf, vendor Mali blob) vs a 64-bit custom firmware
(aarch64, SDL2 KMSDRM + GLES). Everything below is built so the toolchain
choice is a swap of one CMake toolchain file.

## 2. Recommended strategy

Target **both** via two toolchain files, but start with whichever the device
proves it runs:

1. **Track A – stock firmware (armhf)**: use Anbernic's published buildroot SDK
   (`arm-buildroot-linux-gnueabihf_sdk-buildroot.tar.gz`, from the RG35XX-CFW
   releases, referenced by the muOS RetroArch build guide) **[src]**. Its sysroot
   matches the device's libc/GL libs, which avoids the glibc-version and
   bionic/library-mixing problems seen on the original RG35XX **[src]**.
2. **Track B – custom firmware (aarch64)**: Debian/Ubuntu
   `gcc-aarch64-linux-gnu` + arm64 SDL2 dev packages, as used by community
   RG35XXSP/H700 builds **[src]**. Simpler and easier to keep up to date, and
   the likely long-term home if we want a robust, cross-platform game.

Because glibc on the device is old, **always link against a sysroot that is
the device's (or older) glibc** – building with a modern host glibc and
copying the binary over is the usual cause of `GLIBC_2.xx not found`.

## 3. Build environment

Use a Docker image so the toolchain is reproducible (the community
`union-rg35xx-toolchain` repo is a model: `make shell` builds an image and
bind-mounts a workspace) **[src]**. Note there is no official
`union-rg35xxplus-toolchain` I could find; do not assume one exists.

Proposed layout:

```
tools/rg35xxplus/
  Dockerfile              # debian base, cmake, ninja, pkg-config, python3
  toolchain-armhf.cmake   # Track A
  toolchain-aarch64.cmake # Track B
  build-sdl2.sh           # builds SDL2 into the sysroot
  sysroot/                # copied from device (see §5), gitignored
```

Example `toolchain-aarch64.cmake`:

```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_SYSROOT      ${RG_SYSROOT})
set(CMAKE_FIND_ROOT_PATH ${RG_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-a53")
```

Track A is the same with `arm-buildroot-linux-gnueabihf-*` compilers and
`-mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard` (flags **[verify]**
against how the stock binaries were built).

## 4. SDL2 + GLES specifics

- There is no X11/Wayland on these devices; apps use SDL2 directly on
  KMSDRM or the vendor's framebuffer/EGL path, with OpenGL ES (or desktop GL
  via gl4es) **[src]**.
- Build SDL2 ourselves into the sysroot with `--enable-video-kmsdrm
  --enable-video-opengles2 --disable-video-x11 --disable-video-wayland`
  (requires libdrm + libgbm + libEGL/libGLESv2 headers/libs in the sysroot).
  Whether the stock firmware's own SDL2 is usable (and which video driver it
  was built for) is **[verify]** on device.
- GPU userspace: Mali-G31 needs either the vendor `libmali` blob or open
  Panfrost/Mesa. Which one the stock firmware ships (and whether it exposes
  GBM/KMSDRM or a vendor window system) is **[verify]** – I found no
  authoritative source in research.
- OpenRoo-side: the current renderer must be able to run on GLES2/3 at
  640x480; that needs an audit of what GL calls the code uses (a separate
  task, not covered here).

## 5. Getting the stock firmware and exploring it

### 5.1 Obtaining the image
- Official: Anbernic's download page lists RG35XX Plus firmware (a V1.2.3
  dated 251225 appeared in search results) **[src]**. It is an SD-card image,
  usually a zip/7z containing an `.img` per card size; Anbernic has removed
  the 64 GB variant, so use the 16 GB one **[src]**.
- Retailer mirror: DroiX's knowledge base links an "ANBERNIC OS v20240118"
  image on Google Drive **[src]** – compare checksums against the official
  one before trusting it.
- Licensing: the image is proprietary. Keep it and any extracted files
  **out of the repo** (same policy as `game/` assets).

### 5.2 Offline exploration (no device needed)
Work in a fresh empty directory, not the repo:

```
7z x firmware.7z -oimg/
fdisk -l img/*.img                 # find partitions + offsets
sudo losetup -Pf --show img/*.img  # or mount with offset= manually
sudo mount -o ro /dev/loopXpN mnt/
```

Look for: the rootfs (libc version: `ls mnt/lib*/libc*`; `file mnt/bin/busybox`
→ armhf vs aarch64), `libSDL2*`, `libEGL*`, `libGLESv2*`, `libmali*`,
`/usr/lib/dri`, kernel modules and `uname`/`/proc/config` (kernel config),
the launcher/frontend binary, init scripts (how apps are started, which
env vars like `SDL_VIDEODRIVER` are set), and any bundled emulators (they
show a known-working SDL/GL recipe). Use `readelf -d`/`strings` on those to
find the SDL video backend and Mali window system in use. The bootloader
partition (raw offsets, u-boot/boot0) is outside any filesystem – inspect
with `binwalk`.

### 5.3 Pulling libs for the sysroot
Copy `lib`, `usr/lib`, `usr/include` (if present; otherwise take headers from
the SDK) from the mounted rootfs into `tools/rg35xxplus/sysroot/`. Fix
absolute symlinks with `symlinks -cr` (or `rsync -L` selectively).

## 6. Verification plan on a real device

Do these in order; each step gates the next.

1. **Access**: boot the stock card; enable SSH/telnet in the OS settings if
   offered, else mount the card's rootfs partition on a PC. Alternatively use
   the USB-serial UART pads (3.3 V only) for a console log **[verify]**.
2. **Fingerprint**: `uname -a; cat /etc/os-release; file /bin/busybox;
   ldd --version; ls /usr/lib | grep -Ei 'sdl|egl|gles|mali|drm|gbm'`.
   Record results in this doc – it decides Track A vs B.
3. **Hello world (no GL)**: cross-compile a static-ish `hello` and run it;
   confirms ABI and how to launch binaries (stock frontend entry hook,
   or a "ports/APPS" folder – **[verify]**).
4. **SDL2 window**: minimal SDL2 program that opens a 640x480 window and
   fills colours; prints `SDL_GetCurrentVideoDriver()`.
5. **GLES context**: SDL2 + `SDL_GL_CONTEXT_PROFILE_ES`, version 2.0 then
   3.0; clear to colour, print `glGetString(GL_RENDERER/VERSION)`. Confirms
   the Mali driver is reachable.
6. **Input + audio**: dump SDL joystick/gamecontroller events and the audio
   device list; the D-pad/buttons likely appear as a joystick or evdev device.
7. **Perf smoke**: textured quad benchmark at 640x480 to estimate fill-rate
   headroom, then the OpenRoo renderer.
8. **Package**: script `tools/rg35xxplus/package.sh` producing a folder
   (binary + bundled libs + launcher script setting `LD_LIBRARY_PATH` and
   SDL env vars) to copy to the SD card.

## 7. Risks / open questions

- Stock userland ABI and GL stack are unconfirmed (biggest unknown).
- Older glibc on the device may force building inside Anbernic's SDK or an
  old Debian container (e.g. buster) rather than a current distro.
- Game assets are licensed: deployment bundle must never include them; the
  user copies them manually.
- Memory (1 GB shared with GPU) and the 4x A53 CPU budget vs. current code.
- Stock firmware updates may change libs; pin the firmware version in docs.

## 8. Suggested next steps

1. You confirm which firmware the device runs (stock vs muOS/Knulli).
2. I build the Docker image + both toolchain files and cross-compile the
   hello/SDL/GLES test programs (steps 3–5 above) – these can be verified
   only on your device.
3. Audit OpenRoo's rendering for GLES2/3 compatibility.

## Sources
- [muOS RetroArch build guide (Anbernic SDK path)](https://muos.dev/tech/retroarch)
- [muOS community: tooling thread](https://community.muos.dev/t/greetings-any-other-devs-in-here-some-related-tooling-requests/503)
- [shauninman/union-rg35xx-toolchain](https://github.com/shauninman/union-rg35xx-toolchain)
- [Reverse Engineering RG35XX Stock Firmware (tinyhack)](https://tinyhack.com/2023/12/)
- [Anbernic firmware downloads](https://win.anbernic.com/download_data/371.html)
- [DroiX RG35XX Plus firmware/knowledge base](https://droix.net/knowledge-base/device/rg35xx-plus/)
- [Joey's Retro Handhelds: RG35XX Plus setup guide](https://joeysretrohandhelds.com/guides/anbernic-rg35xx-plus-setup-guide/)
