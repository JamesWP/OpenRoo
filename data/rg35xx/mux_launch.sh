#!/bin/sh
# HELP: Open'Roo, a reimplementation of Ka'roo
# ICON: app
# GRID: Open'Roo

# The muOS launcher.  The rg35xx-package build target puts it in
# build/rg35xx/OpenRoo/ with the game; copy that directory, and your own
# imported game data (the contents of game/), to MUOS/application/OpenRoo/ on
# the SD card.  muOS runs this with the app directory as $1 and returns to its
# menu when the game exits.

. /opt/muos/script/var/func.sh

APP_BIN="OpenRoo"
SETUP_APP "$APP_BIN" ""

APP_DIR="$1"
cd "$APP_DIR" || exit 1

# SETUP_APP exports a controller mapping numbered for muOS's SDL2 build; with
# SDL3 it scrambles the buttons.  The bundled SDL3 has the right one built in.
unset SDL_GAMECONTROLLERCONFIG SDL_GAMECONTROLLERCONFIG_FILE

# The settings, saves and high scores live beside the game; the first run
# starts from the device's defaults (640x480).
mkdir -p SavedGames highscores
[ -f openroo.ini ] || cp openroo.ini.default openroo.ini

# The launcher dialog is a second window, which the device's one-window video
# driver cannot show.
KAROO_SKIP_LAUNCHER=1 ./"$APP_BIN" > "$APP_DIR/log.txt" 2>&1
