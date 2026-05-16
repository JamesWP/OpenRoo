#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

> JJ.log; > StreamSoundBuffer.log; rm -f steam-123456.log

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
