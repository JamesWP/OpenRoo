/* The game's WinMain and its main window procedure. */
#pragma once
#include <windows.h>

/* Builds the globals, runs the launcher dialog, creates the window and runs
 * the message loop.  Returns WM_QUIT's wParam, or 0 or 1 from an early exit.
 */
  int WINAPI
Main_WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine,
             int nCmdShow);

/* The main window's class procedure. */
  LRESULT CALLBACK
Main_WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
