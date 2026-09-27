#pragma once
#include <windows.h>

struct IDirectDraw;

/* KAROO_HEADLESS=1 replaces DirectDraw entirely with the in-DLL null device.
 * See nullddraw.cpp for what that means and what it is built from. */
bool nulldd_enabled(void);

/* Returns the null IDirectDraw (v1) — the object create_directdraw (createdevice.cpp) hands
 * back in headless mode.  Never fails; the object is static. */
IDirectDraw *nulldd_create(void);

/* WinMain's window: a message-only window when headless, else passthrough. */
HWND WINAPI hooks_CreateWindowExA(
        DWORD exStyle, LPCSTR className, LPCSTR windowName, DWORD style,
        int x, int y, int w, int h, HWND parent, HMENU menu,
        HINSTANCE inst, LPVOID param);
