/* Direct3D device creation -- see createdevice.cpp. */
#pragma once
#include <windows.h>

struct Direct3D;

/* Only the low byte is the result: nonzero on success.  WinMain tries it
 * in three configurations, stopping at the first that succeeds. */
extern "C" __declspec(dllexport) unsigned __attribute__((thiscall))
Direct3D_CreateD3DDevice(Direct3D *self, HWND hWnd, GUID *pDriverGuid,
                         int nModeIndex, bool bHardware);
