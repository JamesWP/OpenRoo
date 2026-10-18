#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

SKIP_LAUNCHER=0
AUTO_EXIT_SECS=0
HEADLESS=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    # Auto-dismiss the launcher dialog so the run needs nobody at the keyboard.
    # This is NOT headless — the game still opens a window and renders. The
    # dialog is the only place the video mode is chosen, so a run that skips it
    # takes the mode from openroo.ini; see the default-config block below.
    --skip-launcher) SKIP_LAUNCHER=1 ;;
    # Truly headless: no window, no graphics, no display needed.  Implies
    # --skip-launcher (the dialog is a window too).  The render device is
    # created with no OpenGL behind it -- see src/gl/createdevice.cpp --
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

# build/ must be a Linux build (the CMake option OPENROO_PLATFORM); the Windows
# build runs with launch.ps1.
BUILD_DIR=${BUILD_DIR:-build}
# Bring the build up to date first; a failed build does not launch a stale exe.
# OPENROO_SKIP_BUILD=1 launches what is already built: a caller that starts many
# runs (tools/replaytest.py) builds once itself, and the no-op build check
# costs about a second a launch.
if [[ -z "${OPENROO_SKIP_BUILD:-}" ]]; then
  [[ -f "$BUILD_DIR/CMakeCache.txt" ]] || cmake -S . -B "$BUILD_DIR" >&2 || { echo "ERROR: cmake configure failed"; exit 1; }
  cmake --build "$BUILD_DIR" -j"$(nproc)" >&2 || { echo "ERROR: build failed"; exit 1; }
fi
[[ -f "$BUILD_DIR/CMakeCache.txt" ]] || { echo "ERROR: $BUILD_DIR is not configured" >&2; exit 1; }
PLATFORM=$(sed -n 's/^OPENROO_PLATFORM:[A-Z]*=//p' "$BUILD_DIR/CMakeCache.txt")
case "$PLATFORM" in
  linux)  EXE=OpenRoo ;;
  windows) echo "ERROR: $BUILD_DIR is a Windows build; run launch.ps1 from PowerShell" >&2; exit 1 ;;
  rg35xx)  echo "ERROR: $BUILD_DIR is an rg35xx build; it runs on the device (see README.md)" >&2; exit 1 ;;
  *) echo "ERROR: $BUILD_DIR has no OPENROO_PLATFORM (got '$PLATFORM'); reconfigure it" >&2; exit 1 ;;
esac
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
# game writes -- openroo.ini (settings and key bindings), SavedGames/, highscores/,
# every log and report -- and a symlink to each read-only data entry in
# game/, so the game's relative paths resolve exactly as they did in an
# install.  The executable is copied in, so its directory is the cwd too.
[[ -d game ]] || { echo "ERROR: game/ missing — run tools/import_assets.py --from <your Ka'roo>" >&2; exit 1; }
mkdir -p run/SavedGames run/highscores
for _entry in game/*; do
  _name=$(basename "$_entry")
  case "$_name" in
    SavedGames|highscores|ProgableControl.sav|Karoo.exe|res) continue ;;  # (the old binary bindings, no longer read)
  esac
  [[ -L "run/$_name" ]] || ln -s "../game/$_name" "run/$_name"
done
cp -p "$BUILD_DIR/$EXE" run/

# openroo.ini is ours, not the game's, and holds the settings and the key
# bindings.  Without it the game runs on its built-in defaults; install the
# harness's (1024x768x32, music off, quiet effects) so a run needs nobody at
# the keyboard -- only when absent: never overwrite a file the player has
# since changed through the launcher or the options.
if [[ ! -f run/openroo.ini ]]; then
  echo "run/openroo.ini missing — installing data/openroo.ini.default (1024x768x32)"
  cp data/openroo.ini.default run/openroo.ini
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
# of wall clock.  The wait is
# in the GL/Vulkan present.  These are the driver-side switches
# (Mesa GL, Mesa Vulkan WSI, NVIDIA GL), and unset really does mean untouched
# now — passing them as empty strings is NOT the same thing.
for _name in XAUTHORITY DBUS_SESSION_BUS_ADDRESS XDG_RUNTIME_DIR \
             vblank_mode MESA_VK_WSI_PRESENT_MODE __GL_SYNC_TO_VBLANK; do
  if [[ -n ${!_name+set} ]]; then
    FORWARD_ENV+=("$_name=${!_name}")
  fi
done

# Slow-frame detection (src/prof/prof.h) is on for every launch: frames over
# 16 ms are kept and written to the log on exit.  KAROO_SLOWFRAME_MS=0 turns it
# off, any other value moves the threshold.
export KAROO_SLOWFRAME_MS="${KAROO_SLOWFRAME_MS-16}"

# Every KAROO_* the caller set, exported or not (compgen sees both).  The
# three the script controls itself are excluded here and set explicitly below.
for _name in $(compgen -v); do
  case "$_name" in
    KAROO_SKIP_LAUNCHER|KAROO_AUTO_EXIT_SECS|KAROO_HEADLESS) continue ;;
    KAROO_*) FORWARD_ENV+=("$_name=${!_name}") ;;
  esac
done

RUN_PREFIX=(
  env -i
  HOME="$HOME" USER="$USER"
  DISPLAY="${DISPLAY:-}"
  PATH="$PATH"
  KAROO_SKIP_LAUNCHER="$SKIP_LAUNCHER"
  KAROO_AUTO_EXIT_SECS="$AUTO_EXIT_SECS"
  KAROO_HEADLESS="$HEADLESS"
  "${FORWARD_ENV[@]}"
)
# OPENROO_WRAPPER: a command to run the game under, e.g. "valgrind --error-exitcode=9".
# shellcheck disable=SC2206
exec "${RUN_PREFIX[@]}" ${OPENROO_WRAPPER:-} "./$EXE"
