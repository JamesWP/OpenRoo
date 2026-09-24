/* inputsetup.cpp -- see inputsetup.h. */
#include <windows.h>
#include "inputsetup.h"
#include "progctrl.h"
#include "gamelog.h"
#include "gamestr.h"
#include "gameglobals.h"

extern "C" __declspec(dllexport) void __cdecl
Input_TrySaveSettings(void)
{
    GameLog_LogMessage(GG_LOGGER, 1, GS_CONTROL_SAVE_SETTINGS);
    ProgCtrl_WriteBindings(GG_PROGCTRL);
    ProgCtrl_Shutdown(GG_PROGCTRL);
}
