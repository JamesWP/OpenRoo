/* The game's launcher (the "Jumpin' John Starter" dialog) and its display
 * device dialog.  launcher.h is the unattended-run support that skips them. */
#pragma once
#include <windows.h>

/* The launcher.  Ends with 1 to play, 0 to quit. */
  INT_PTR CALLBACK
LauncherDlg_Proc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

/* The device dialog, opened by the launcher's setup button.  On OK it writes
 * the adapter GUID and mode index into the Config. */
  INT_PTR CALLBACK
LauncherDlg_DeviceSelectProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
