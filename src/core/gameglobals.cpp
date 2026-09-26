/* gameglobals.cpp -- the shared objects gameglobals.h declares.
 *
 * Each lived in Karoo.exe.orig's .data/BSS until ENDGAME_PLAN.md
 * "Direction" moved them here (tools/owndata.py globals, tools/gamedata.txt).
 * All are plain data, zero-initialised like the BSS they came from; the
 * constructed ones are built by the static-init code, as the original's
 * _initterm table built them.
 */
#include "gameglobals.h"
#include "gamelog.h"
#include "cdm.h"
#include "progctrl.h"
#include "texture.h"

ProgableControl g_progCtrl;          /* was 0x0046c298 */
HINSTANCE       g_moduleInstance;    /* was 0x0046c49c */
GameLogger      g_logger;            /* was 0x0046c4c0 */
GameLogger      g_soundLogger;       /* was 0x004e07e0: static init 0x443c60 */
char            g_levelTitle[128];   /* was 0x0046c714: LevelMap::title_[0x80] */
LoadedImage     g_fallbackImage;     /* was 0x0046c798 */
CDM             g_cdAudio;           /* was 0x004dc640 */
LoadedImage     g_demoImage;         /* was 0x004dc7a8 */
char            g_gameDir[260];      /* was 0x004e01c4: GG_GAME_DIR_LEN */
LoadedImage     g_loadingImage;      /* was 0x004e0428 */
double          g_lastTickMs;        /* was 0x004e04b0 */
