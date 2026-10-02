#pragma once

/* Triggering the game's level report from a harness.  The report (see
 * reportwriter.h) runs when L reads down in all three of the polls made as the
 * fixed sounds load.  KAROO_LEVEL_REPORT=1 answers exactly those three polls
 * "down"; later polls of L read the real keyboard, because L is a live game
 * key. */

/* True when KAROO_LEVEL_REPORT is set. */
bool levelreport_enabled(void);

/* The key-poll override: true, with the answer in out, while the trigger is
 * being answered. */
bool levelreport_async_override(int vkey, short *out);

/* Called once per frame.  After the report the game waits on the main menu, so
 * under KAROO_LEVEL_REPORT this drives the menu to Quit.  Quitting rather than
 * killing the process is what closes the report files: the "level report
 * created" log line comes before they are closed. */
void levelreport_tick(void);
