/* exemain.cpp -- the entry point of our own executable, KarooOwn.exe.
 *
 * ENDGAME_PLAN.md "Direction": the game with no Karoo.exe.orig at all.
 * MinGW's CRT starts the process and calls this WinMain; it does what
 * karoo_hooks.dll's DllMain did on attach, runs the reimplemented WinMain
 * (main.cpp, which builds and destroys the game's globals itself --
 * staticinit.h), then what DllMain did on detach.
 *
 * The log keeps its name, karoo_hooks.log, so every tool that reads it
 * works unchanged on either build.
 */
#include <windows.h>
#include "process.h"
#include "main.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmdLine, int nShow)
{
    Process_Attach("karoo_hooks.log");
    int r = Main_WinMain(hInstance, hPrev, lpCmdLine, nShow);
    Process_Detach();
    return r;
}
