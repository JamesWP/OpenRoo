/* Direct3D::CreateD3DDevice 0x412740 -- see createdevice.cpp's header. */
#pragma once
#include <windows.h>

struct Direct3D;

/* Nonzero on success -- WinMain's three-rung ladder tests AL. */
extern "C" __declspec(dllexport) unsigned __attribute__((thiscall))
Direct3D_CreateD3DDevice(Direct3D *self, HWND hWnd, GUID *pDriverGuid,
                         int nModeIndex, char bHardware);
