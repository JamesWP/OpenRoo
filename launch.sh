#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

DEBUG=0
SKIP_LAUNCHER=0
AUTO_EXIT_SECS=0
HEADLESS=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --debug)    DEBUG=1 ;;
    # Auto-dismiss the launcher dialog so the run needs nobody at the keyboard.
    # This is NOT headless — the game still opens a window and renders. The
    # dialog is the only place the video mode is chosen, so a run that skips it
    # takes the mode from Karoo.cfg; see the default-config block below.
    --skip-launcher) SKIP_LAUNCHER=1 ;;
    # Truly headless: no window, no graphics, no display needed.  Implies
    # --skip-launcher (the dialog is a window too).  The render device is
    # created with no Direct3D behind it -- see src/d3d/createdevice.cpp --
    # so nothing here touches X, nothing takes focus, and no desktop mode is
    # switched.  This is what to use for a background test run.
    --headless) HEADLESS=1; SKIP_LAUNCHER=1 ;;
    --auto-exit)
      shift
      [[ $# -gt 0 ]] || { echo "ERROR: --auto-exit requires a seconds argument" >&2; exit 1; }
      AUTO_EXIT_SECS="$1"
      ;;
    *) echo "ERROR: unknown arg: $1" >&2; exit 1 ;;
  esac
  shift
done

EXE=KarooOwn.exe
BUILD_DIR=${BUILD_DIR:-build}
# Bring the build up to date first; a failed build does not launch a stale exe.
[[ -f "$BUILD_DIR/CMakeCache.txt" ]] || cmake -S . -B "$BUILD_DIR" -DCMAKE_TOOLCHAIN_FILE="cmake/mingw.cmake" >&2 || { echo "ERROR: cmake configure failed"; exit 1; }
cmake --build "$BUILD_DIR" -j"$(nproc)" >&2 || { echo "ERROR: build failed"; exit 1; }
[[ -f "$BUILD_DIR/$EXE" ]] || { echo "ERROR: $BUILD_DIR/$EXE missing after build"; exit 1; }

roll_log() {
  local base="$1" keep=5
  rm -f "${base}.${keep}"
  for i in $(seq $((keep-1)) -1 1); do
    [[ -f "${base}.${i}" ]] && mv "${base}.${i}" "${base}.$((i+1))"
  done
  [[ -f "${base}" ]] && mv "${base}" "${base}.1"
  > "${base}"
}

# ─── The run directory ────────────────────────────────────────────────────
#
# The game runs in run/ (gitignored) and never writes to game/, the data
# imported from your own copy by tools/import_assets.py.  run/ holds what the
# game writes -- Karoo.cfg, SavedGames/, highscores/, ProgableControl.sav,
# every log and report -- and a symlink to each read-only data entry in
# game/, so the game's relative paths resolve exactly as they did in an
# install.  The executable is copied in, so its directory is the cwd too.
[[ -d game ]] || { echo "ERROR: game/ missing — run tools/import_assets.py --from <your Ka'roo>" >&2; exit 1; }
mkdir -p run/SavedGames run/highscores
for _entry in game/*; do
  _name=$(basename "$_entry")
  case "$_name" in
    SavedGames|highscores|ProgableControl.sav|Karoo.exe|res) continue ;;
  esac
  [[ -L "run/$_name" ]] || ln -s "../game/$_name" "run/$_name"
done
# The input bindings: the game rewrites this file, so run/ gets a copy.
[[ -f run/ProgableControl.sav ]] || cp game/ProgableControl.sav run/
cp -p "$BUILD_DIR/$EXE" run/

# Karoo.cfg is ours, not the game's.  Without it Game::Load leaves the video
# mode index and adapter GUID zero-initialised, so the game comes up in
# whatever mode Direct3D enumerates first.  Install a known-good config
# (1024x768x32, music off) -- only when absent: never overwrite a config the
# player has since changed through the launcher.
if [[ ! -f run/Karoo.cfg ]]; then
  echo "run/Karoo.cfg missing — installing Karoo.cfg.default (1024x768x32)"
  cp data/Karoo.cfg.default run/Karoo.cfg
fi

cd run
roll_log steam-123456.log
roll_log karoo_hooks.log

# X authority self-heal.  The game needs a display, and under Wayland+Mutter
# the Xwayland cookie lives in a randomly-named file that is REPLACED whenever
# Xwayland restarts (a suspend/resume or a session restart will do it).  A
# shell — or a long-running agent session — started before that keeps the old
# XAUTHORITY path in its environment, and every launch then dies with
#   Invalid MIT-MAGIC-COOKIE-1 key
#   err:winediag:nodrv_CreateWindow ... "The explorer process failed to start."
# and the game exits having rendered zero frames.  That looks exactly like a
# crash in whatever was last changed, which is an expensive misdiagnosis.
#
# So: if the current XAUTHORITY does not actually work, fall back to the
# newest mutter Xwayland cookie.  Only ever overrides a broken value.
if (( HEADLESS )); then
  : # No display is needed or wanted; skip the X checks entirely.
elif ! DISPLAY="${DISPLAY:-}" XAUTHORITY="${XAUTHORITY:-}" xdpyinfo >/dev/null 2>&1; then
  newest_cookie=$(ls -t "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
  if [ -n "$newest_cookie" ] && DISPLAY="$DISPLAY" XAUTHORITY="$newest_cookie" xdpyinfo >/dev/null 2>&1; then
    echo "XAUTHORITY was stale; using $newest_cookie"
    export XAUTHORITY="$newest_cookie"
  else
    echo "WARNING: cannot reach the X display (${DISPLAY:-unset})." >&2
    echo "  The game needs a display; it will start and exit with 0 frames." >&2
    echo "  Pass --headless to run with no display at all." >&2
  fi
fi

# ─── Environment forwarding ───────────────────────────────────────────────
#
# The game runs under `env -i`, a deliberately hermetic environment: replays
# are only reproducible because the run does not inherit whatever happens to
# be in the caller's shell.  That stays.
#
# What changed (2026-09-02) is HOW the optional variables get forwarded.  This
# block used to list every KAROO_* flag as `KAROO_X="${KAROO_X:-}"`, which
# meant an UNSET flag still reached the game as an EMPTY STRING.  Hook code
# testing `getenv("KAROO_X") != NULL` therefore saw every flag as enabled.
# That silently turned the TGA acceptance test into an original-vs-original
# no-op for three runs, including two that reported clean passes — see
# RENDER_PLAN.md, 2026-09-02.  Unset now stays unset, so a NULL test means
# what it looks like it means.  (Comparing the value, not just the pointer,
# is still the more robust habit in the DLL.)
#
# It also sweeps KAROO_* out of the environment instead of naming each flag,
# so a new mode no longer has to be added here to reach the game — which was
# its own recurring trap, documented in CLAUDE.md.
FORWARD_ENV=()

# Optional host variables: forwarded only when actually set.  The vsync
# switches especially — the replay clock is virtual, so nothing in the game
# paces itself against wall time, but presentation still blocks on the display
# refresh, which pinned a run at ~60 fps and made a 4150-frame replay take 70 s
# of wall clock.  DDFLIP_NOVSYNC through ddraw does not lift it; the wait is
# below Wine, in the GL/Vulkan present.  These are the driver-side switches
# (Mesa GL, Mesa Vulkan WSI, NVIDIA GL), and unset really does mean untouched
# now — passing them as empty strings is NOT the same thing.
for _name in XAUTHORITY DBUS_SESSION_BUS_ADDRESS XDG_RUNTIME_DIR \
             vblank_mode MESA_VK_WSI_PRESENT_MODE __GL_SYNC_TO_VBLANK; do
  if [[ -n ${!_name+set} ]]; then
    FORWARD_ENV+=("$_name=${!_name}")
  fi
done

# Every KAROO_* the caller set, exported or not (compgen sees both).  The
# three the script controls itself are excluded here and set explicitly below.
for _name in $(compgen -v); do
  case "$_name" in
    KAROO_SKIP_LAUNCHER|KAROO_AUTO_EXIT_SECS|KAROO_HEADLESS) continue ;;
    KAROO_*) FORWARD_ENV+=("$_name=${!_name}") ;;
  esac
done

PROTON_DIR="$HOME/.steam/root/steamapps/common/Proton - Experimental"
PROTON_RUN=(
  env -i
  HOME="$HOME" USER="$USER"
  DISPLAY="${DISPLAY:-}"
  PATH="$PATH"
  STEAM_COMPAT_DATA_PATH="$HOME/.proton/Karoo.exe"
  STEAM_COMPAT_CLIENT_INSTALL_PATH="$HOME/.steam/root"
  PROTON_LOG_DIR="$(pwd)"
  SteamGameId=123456 SteamAppId=123456
  # Script-controlled; these are always set, so the DLL can rely on them.
  KAROO_SKIP_LAUNCHER="$SKIP_LAUNCHER"
  KAROO_AUTO_EXIT_SECS="$AUTO_EXIT_SECS"
  KAROO_HEADLESS="$HEADLESS"
  "${FORWARD_ENV[@]}"
  "$PROTON_DIR/proton" run
)

if (( DEBUG )); then
  roll_log debug.log
  [[ -f ../debug.gdb ]] || { echo "ERROR: debug.gdb missing"; exit 1; }
  command -v gdb >/dev/null || { echo "ERROR: gdb not installed (apt install gdb)"; exit 1; }

  PORT="${WINEDBG_PORT:-9999}"

  cleanup() {
    [[ -n "${WINEDBG_PID:-}" ]] && kill "$WINEDBG_PID" 2>/dev/null || true
    "${PROTON_RUN[@]}" wineserver -k 2>/dev/null || true
  }
  trap cleanup EXIT

  "${PROTON_RUN[@]}" winedbg --gdb --no-start --port "$PORT" "$EXE" JJ &
  WINEDBG_PID=$!

  # Wait for the gdb stub's listening socket. IMPORTANT: probe PASSIVELY with ss
  # — winedbg's gdb proxy accepts the first TCP connection as *the* gdb client,
  # so a /dev/tcp readiness probe would steal the slot and make the real gdb
  # handshake time out.
  echo "Waiting for winedbg gdb stub on port $PORT (pid $WINEDBG_PID)..."
  for _ in $(seq 1 100); do
    if ss -ltnH "sport = :$PORT" 2>/dev/null | grep -q .; then break; fi
    kill -0 "$WINEDBG_PID" 2>/dev/null || { echo "ERROR: winedbg exited before opening port $PORT" >&2; exit 1; }
    sleep 0.2
  done

  echo "Attaching gdb (probes log to ./debug.log)..."
  gdb -q -x ../debug.gdb
  cleanup
  exit 0
fi

exec "${PROTON_RUN[@]}" "$EXE" JJ
