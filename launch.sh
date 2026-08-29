#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

DEBUG=0
HEADLESS=0
AUTO_EXIT_SECS=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --debug)    DEBUG=1 ;;
    --headless) HEADLESS=1 ;;
    --auto-exit)
      shift
      [[ $# -gt 0 ]] || { echo "ERROR: --auto-exit requires a seconds argument" >&2; exit 1; }
      AUTO_EXIT_SECS="$1"
      ;;
    *) echo "ERROR: unknown arg: $1" >&2; exit 1 ;;
  esac
  shift
done

[[ -f ddraw.dll ]]        || { echo "ERROR: ddraw.dll missing — run build.sh first"; exit 1; }
[[ -f karoo_hooks.dll ]] || { echo "ERROR: karoo_hooks.dll missing — run build.sh first"; exit 1; }

roll_log() {
  local base="$1" keep=5
  rm -f "${base}.${keep}"
  for i in $(seq $((keep-1)) -1 1); do
    [[ -f "${base}.${i}" ]] && mv "${base}.${i}" "${base}.$((i+1))"
  done
  [[ -f "${base}" ]] && mv "${base}" "${base}.1"
  > "${base}"
}

roll_log JJ.log
roll_log StreamSoundBuffer.log
roll_log steam-123456.log
roll_log karoo_hooks.log

PROTON_DIR="$HOME/.steam/root/steamapps/common/Proton - Experimental"
PROTON_RUN=(
  env -i
  HOME="$HOME" USER="$USER"
  DISPLAY="$DISPLAY" XAUTHORITY="${XAUTHORITY:-}"
  DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-}"
  XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-}"
  PATH="$PATH"
  STEAM_COMPAT_DATA_PATH="$HOME/.proton/Karoo.exe"
  STEAM_COMPAT_CLIENT_INSTALL_PATH="$HOME/.steam/root"
  PROTON_LOG_DIR="$(pwd)"
  SteamGameId=123456 SteamAppId=123456
  WINEDLLOVERRIDES="ddraw=n,b"
  KAROO_HEADLESS="$HEADLESS"
  KAROO_D3D_PROXY="${KAROO_D3D_PROXY:-1}"
  KAROO_D3D_FX="${KAROO_D3D_FX:-}"
  KAROO_FAKTMESH_FX="${KAROO_FAKTMESH_FX:-}"
  KAROO_PARTICLE_FX="${KAROO_PARTICLE_FX:-}"
  KAROO_AUTO_EXIT_SECS="$AUTO_EXIT_SECS"
  "$PROTON_DIR/proton" run
)

if (( DEBUG )); then
  roll_log debug.log
  [[ -f debug.gdb ]] || { echo "ERROR: debug.gdb missing"; exit 1; }
  command -v gdb >/dev/null || { echo "ERROR: gdb not installed (apt install gdb)"; exit 1; }

  PORT="${WINEDBG_PORT:-9999}"

  cleanup() {
    [[ -n "${WINEDBG_PID:-}" ]] && kill "$WINEDBG_PID" 2>/dev/null || true
    "${PROTON_RUN[@]}" wineserver -k 2>/dev/null || true
  }
  trap cleanup EXIT

  "${PROTON_RUN[@]}" winedbg --gdb --no-start --port "$PORT" Karoo.exe JJ &
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
  gdb -q -x debug.gdb
  cleanup
  exit 0
fi

exec "${PROTON_RUN[@]}" Karoo.exe JJ
