#pragma once
#include <windows.h>

/* Level-report trigger — the game's own all-levels dump, driven from a harness.
 *
 * Game::LoadSounds (0x0041A280) polls GetAsyncKeyState(VK_L) three times just
 * before it acquires the fixed sound buffers, and if L is down it calls
 * WriteLevelReport (0x0041B991).  That routine walks every level in the game
 * file: SetCurrentLevelName, OpenLevelFile, SetupLevelObjects,
 * CalculateLevelScore — and writes two text files into the game directory:
 * LevelReport.txt (per-level object counts, par time, cumulative score) and
 * ScriptTexts.txt (every instruction-script text, level by level).
 *
 * That is far broader coverage than any replay: it exercises level parsing and
 * object construction across all 80 levels rather than the handful a recording
 * visits.  KAROO_LEVEL_REPORT=1 answers the poll so a harness can trigger it
 * with nobody at the keyboard.
 *
 * The answer is deliberately one-shot: exactly the three consecutive queries
 * LoadSounds makes are answered "down", and every later query for VK_L reads
 * the real keyboard.  A blanket override would hold L down for the rest of the
 * run, and L is a live game key.
 */

/* True when KAROO_LEVEL_REPORT is set. */
bool levelreport_enabled(void);

/* GetAsyncKeyState interception.  Returns true and fills `out` while the
 * trigger is being synthesised; otherwise the real keyboard answers. */
bool levelreport_async_override(int vkey, SHORT *out);

/* Called once per frame from the clock.  WriteLevelReport runs to completion
 * inside LoadSounds and then simply returns, leaving the game sitting on the
 * main menu waiting for a person — so under KAROO_LEVEL_REPORT the run drives
 * the menu's Quit node itself.  Quitting rather than killing the process is
 * what closes the two report files: the "level report created" log line is
 * emitted *before* fclose, so a kill on that line can truncate the output. */
void levelreport_tick(void);
