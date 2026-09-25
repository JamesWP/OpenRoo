#pragma once
#include <windows.h>
/* Unattended-run support.
 *
 * KAROO_SKIP_LAUNCHER=1 makes hooks_DialogBoxParamA return IDOK without
 * showing the launcher dialog, so a run needs nobody at the keyboard. It is
 * NOT headless: the game still creates its window and renders to the screen
 * exactly as usual. Nothing here suppresses output, and none of it lets the
 * game run without a display.
 *
 * The dialog is the only place the video mode is chosen, so skipping it means
 * the mode comes from Karoo.cfg alone — launch.sh installs a default one when
 * the file is missing (a fresh worktree), which is why that is not a problem.
 */
void launcher_init(void);

/* End the run now, the same way --auto-exit does: post WM_CLOSE to the main
 * window so the game's own shutdown path runs (Stage E asserts on a clean
 * shutdown, so ExitProcess is only the fallback). */
void launcher_end_run(const char *why);

/* WinMain's launcher dialog (and the device dialog's): IDOK without showing
 * it under KAROO_SKIP_LAUNCHER, else passthrough. */
extern "C" __declspec(dllexport) INT_PTR WINAPI hooks_DialogBoxParamA(
        HINSTANCE inst, LPCSTR tmpl, HWND parent, DLGPROC proc, LPARAM param);
