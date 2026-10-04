#pragma once

/* Unattended runs.  KAROO_SKIP_LAUNCHER=1 answers the launcher dialog with OK
 * without showing it, and KAROO_AUTO_EXIT_SECS ends the run after that many
 * seconds.  Neither makes the game headless: the window is still created and
 * drawn to.  With the dialog skipped the video mode comes from openroo.ini
 * alone. */
void launcher_init(void);

/* Ends the run the way --auto-exit does: posts WM_CLOSE to the main window so
 * the game's own shutdown runs, or calls ExitProcess if there is no window.
 * Only the first call acts. */
void launcher_end_run(const char *why);

/* True under KAROO_SKIP_LAUNCHER: the launcher dialog is answered "play"
 * without being shown. */
bool launcher_skipped(void);
