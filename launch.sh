#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

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

env -i \
  HOME="$HOME" USER="$USER" \
  DISPLAY="$DISPLAY" XAUTHORITY="${XAUTHORITY:-}" \
  DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-}" \
  XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-}" \
  PATH="$PATH" \
  STEAM_COMPAT_DATA_PATH="$HOME/.proton/Karoo.exe" \
  STEAM_COMPAT_CLIENT_INSTALL_PATH="$HOME/.steam/root" \
  PROTON_LOG_DIR="$(pwd)" \
  SteamGameId=123456 SteamAppId=123456 \
  WINEDLLOVERRIDES="ddraw=n,b" \
  "$HOME/.steam/root/steamapps/common/Proton - Experimental/proton" run Karoo.exe JJ
