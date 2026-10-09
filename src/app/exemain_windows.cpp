/* The executable's entry point: process setup, the game's WinMain, process
 * teardown.  Our diagnostic log is karoo_hooks.log, the name every test tool
 * reads. */
#include <windows.h>
#include "process.h"
#include "main.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR lpCmdLine, int)
{
    Process_Attach("karoo_hooks.log");
    int r = Main_WinMain(lpCmdLine);
    Process_Detach();
    return r;
}
