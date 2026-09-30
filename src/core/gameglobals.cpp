/* The shared objects gameglobals.h declares.  All are zero-initialised; the
 * ones with constructors are built by static initialisation, before WinMain.
 */

#include "gameglobals.h"
#include "gamelog.h"
#include "cdm.h"
#include "progctrl.h"
#include "scenetexture.h"

ProgableControl g_progCtrl;
HINSTANCE       g_moduleInstance;
char            g_levelTitle[128];
LoadedImage     g_fallbackImage;
CDM             g_cdAudio;
LoadedImage     g_demoImage;
char            g_gameDir[260];  // GG_GAME_DIR_LEN
LoadedImage     g_loadingImage;
double          g_lastTickMs;

// Loggers last so we get the final log lines
GameLogger      g_logger;
GameLogger      g_soundLogger("StreamSoundBuffer.log", 0);  // the level title as the map file gives it