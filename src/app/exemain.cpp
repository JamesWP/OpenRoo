/* The executable's entry point: process setup, the game's WinMain, process
 * teardown.  Our diagnostic log is karoo_hooks.log, the name every test tool
 * reads. */
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
