/* launcherdialogs.h -- the "Jumpin' John Starter" launcher and its device
 * dialog, the TU 0x0043ce10..0x0043dde0 (launcherdialogs.cpp).  Not to be
 * confused with launcher.h, the unattended-run hooks that skip them. */
#pragma once
#include <windows.h>

/* 0x0043d650, dialog 0x68.  Opened by WinMain; EndDialog(1) = play,
 * EndDialog(0) = quit. */
extern "C" __declspec(dllexport) INT_PTR CALLBACK
LauncherDlg_Proc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

/* 0x0043cfa0, dialog 0x6e.  Opened by the launcher's setup button; on OK it
 * writes the adapter GUID and mode index into Game's Config. */
extern "C" __declspec(dllexport) INT_PTR CALLBACK
LauncherDlg_DeviceSelectProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
