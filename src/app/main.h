/* main.h -- WinMain 0x0042d100 (main.cpp); the original's source file was
 * E:\WORK\VC++\JumpinJohn\JumpinJohn\main.cpp, per its own log line. */
#pragma once
#include <windows.h>

/* 0x0042d100, __stdcall (RET 0x10).  One caller: the CRT's entry, E8 at
 * 0x00452052.  Returns WM_QUIT's wParam, or 0/1 from the early exits. */
extern "C" __declspec(dllexport) int WINAPI
Main_WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine,
             int nCmdShow);
