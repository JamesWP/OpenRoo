/* The game's WinMain. */
#pragma once

/* Builds the globals, runs the launcher dialog, creates the window and runs
 * the message loop.  Returns WM_QUIT's wParam, or 0 or 1 from an early exit.
 */
int Main_WinMain(const char *cmdLine);
