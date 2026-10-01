/* The game's side of the launcher (the "Jumpin' John Starter" dialog) and its
 * display device dialog: what they list and where the choices go.  The dialogs
 * themselves are the windowing layer's (windev.h); launcher.h is the
 * unattended-run support that skips them. */
#pragma once

/* Shows the launcher over the given window.  True to play, false to quit. */
bool LauncherDlg_Show(void *parent);
