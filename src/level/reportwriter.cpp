/* WriteLevelReport (0x41b980) — replaced so that no game-owned FILE * remains.
 *
 *   void __thiscall WriteLevelReport(Game *this, LPCSTR pathname)
 *   1 E8 call site (0x41a606, in AcquireFixedSoundBuffersAndMaybeReport);
 *   no E9, no PUSH, no vtable slot.  UD2-stubbed.
 *
 * ─── Why this function, and not the reader that had the problem ───────────
 *
 * Phase 4's report-side .jjs reader (src/ui/scriptplayer.cpp) had to call
 * the GAME's fputs, because the FILE * it writes to was opened by this
 * function with the game's fopen.  An MSVC FILE cannot be written by this
 * DLL's mingw CRT -- attempting it hung the level report with no crash and no
 * UD2.  That was a real constraint, not a preference, and it could not be
 * fixed inside the reader: whoever OPENS the file decides which CRT owns it.
 *
 * So this replaces the opener.  Both output streams -- ScriptTexts.txt and
 * the report file the caller names -- are now ours, and scriptplayer.cpp drops
 * ORIG_FPUTS and uses plain fputs again.  The game's stdio is out of the
 * report path entirely: our fopen, our sprintf, our fputs, our fclose.
 *
 * ─── What still calls into the game, and why ──────────────────────────────
 *
 * This function is a REPORT GENERATOR wrapped around the game's level
 * machinery.  The file handling is ours; the level machinery is not, and
 * taking it over is not an I/O question:
 *
 *   SetCurrentLevelName  0x4186b0   pick level N
 *   OpenLevelFile        0x4186f0   load it (which calls our .jjm and .jjs
 *                                   readers, and the .leo reader under them)
 *   SetupLevelObjects    0x416420   build its objects
 *   CalculateLevelScore  0x41a760   score it
 *   Log_Message          0x441b10   the two log lines the harness greps for
 *
 * Those five are named here and in the commit message, per ASSET_PLAN.md's
 * no-callback rule.  The high-score rewrite at the end is NOT a callback any
 * more: WriteHighScoreFile is ours (Phase 2), so it is a direct call to our
 * own export.
 *
 * The game's format strings are referenced by ADDRESS rather than copied.
 * They are pure .rdata, one of them is a 249-character rule of dashes, and a
 * transcription typo would be both easy to make and tedious to find.  A data
 * reference is exact by construction; it is the same kind of reference as the
 * game-directory buffer used elsewhere in this plan.
 *
 * ─── Defects and oddities preserved deliberately ──────────────────────────
 *
 * 1. ScriptTexts.txt IS NOT CHECKED FOR SUCCESS.  Only the report file's
 *    handle is tested; if ScriptTexts.txt fails to open, the original passes
 *    the NULL to fputs anyway.  Reproduced by the same NULL-tolerant guard the
 *    original has at close time and nowhere else -- see s_sink below.
 * 2. THE LOG LINE COMES BEFORE THE fcloses.  "GAME: level report created" is
 *    written while both files are still open and unflushed, which is why
 *    tools/levelreport.py must not kill the process the moment it sees that
 *    line.  Order preserved exactly.
 * 3. SEVERAL sprintf RESULTS ARE NEVER USED: the "%s.gam" formats into the
 *    scratch buffer before each SetCurrentLevelName, the "%s\t" format of the
 *    level display name (a newline is written instead), and the trailing
 *    "level:%d %s".  They are dead stores into a local buffer with no
 *    observable effect, so they are simply not performed here; nothing the
 *    game can see distinguishes the two.
 * 4. The "Bernie Boulder" seeding of the high-score table runs for level 6 and
 *    every eighth level after 5, writing backwards through the table at a
 *    stride of -0x37.  It is a side effect of generating the report, not part
 *    of it, and tools/levelreport.py snapshots highscores/ because of it.
 * 5. The catch-count accumulator is a BYTE, so a level with more than 255
 *    type-2 objects would wrap.  Kept as a byte.
 * 6. The running score subtracts the level INDEX, which makes the total
 *    depend on level order rather than only on level content.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "gamelog.h"
#include "game.h"
#include "reportwriter.h"
#include "levelparse.h"
#include "levelsetup.h"
#include "levelscore.h"
#include "foe.h"
#include "player.h"
#include "gamestr.h"
#include "gameglobals.h"

/* ─── Game data (DATA references, not calls) ─────────────────────────────── */



/* ─── Game logic this file drives — all of it ours now ───────────────────── */

/* This section used to be "deliberately still the game's", behind five
 * ORIG_* macros.  Every one of those five has since been replaced, so the
 * macros were calling our own exports under a name that said "original"
 * (COHESION_PLAN.md Band 7c).  They are gone; the calls below name the
 * function they reach:
 *
 *   Sim_SetCurrentLevelName  0x004186b0  levelparse.h   GAMETICK_PLAN.md B
 *   Sim_OpenLevelFile        0x004186f0  levelparse.h   GAMETICK_PLAN.md B
 *   Sim_SetupLevelObjects    0x00416420  levelsetup.h   GAMETICK_PLAN.md B
 *   Score_CalculateLevelScore 0x0041a760 levelscore.h   GAMETICK_PLAN.md A
 *   GameLog_LogMessage       0x00441b10  gamelog.h
 *
 * Each also dropped a cast.  Three carried a `(Game *)` on an argument that
 * is already a `Game *`, and the logger carried a whole function-pointer
 * cast to `(void *, int, const char *, ...)` — needed only because
 * GG_LOGGER above was typed `void *`, which it no longer is.  A cast over
 * a typed export is the trap described below in its worst form: it converts
 * a signature change from a compile error into a crash.
 *
 * ── Why this file is an 80-level acceptance test ──────────────────────────
 *
 * These calls are why `levelreport.py` gates the loader and scoring
 * replacements, and they are where the DLL-caller hazard was found twice.
 *
 * An absolute-address call from our own DLL is exactly the kind an `xref.py`
 * scan of the EXE cannot see: the exe holds no reference to the address at
 * all — not an E8, not a `68 imm32`, not even the raw four bytes.
 *
 *  - Score_CalculateLevelScore: stubbing 0x0041a760 trapped at `call eax`
 *    with eax = 0041a760 and nothing in the binary to explain it.
 *  - SetCurrentLevelName / OpenLevelFile: the cycle that replaced them
 *    patched all 20 E8 sites in the EXE and the replay suite passed 16/16,
 *    because the ONLY caller left was here. `levelreport.py` failed with
 *    nine c000001d and the UD2 stub named the address.
 *  - SetupLevelObjects was the third instance — checked BEFORE stubbing.
 *
 * When replacing anything, grep src/ for its address as well as
 * running xref.py over the exe. */

/* ─── Game field offsets ─────────────────────────────────────────────────── */

/* The five report tallies (was OFF_TALLY_A 0x42243, OFF_TALLY_BONUS 0x42245,
 * OFF_TALLY_LEO 0x42247, OFF_TALLY_IS 0x4220c, OFF_SCORE_TOTAL 0x42212) are
 * Game's; this file is their only writer.  See game.h. */
/* The game file name (was OFF_GAMEFILE 0x4215f) is Game::gameFileName(). */
/* The map name (was OFF_LEVEL_WORLD 0x2ab69d), bonus flag (OFF_BONUS_FLAG
 * 0x2ab599), file time limit (OFF_PAR_TIME_SRC 0x2ab71f, the par time before
 * the 50% scaling) and title (OFF_LEVEL_TITLE 0x2ab61d) are the LevelMap's
 * (levelmap.h), through Game::map(). */
/* OFF_PAR_COPY was Player +0x23d (player.h), the crystals count. */
/* The level path (was OFF_LEVEL_PATH 0x173483) is Game::levelNameBuffer(). */

/* The per-column field list, in the original's emission order.  Each is
 * printed with "%d\t"; the widths differ, hence the size tag. */
static const struct { unsigned off; unsigned char size; } COLUMNS[] = {
    { 0x42252, 2 }, { 0x421fb, 2 }, { 0x2ab723, 4 }, { 0x421fd, 2 },
    { 0x421e3, 2 }, { 0x421e1, 2 }, { 0x173b19, 1 }, { 0x173718, 1 },
    { 0x173e3e, 1 }, { 0x421ed, 2 }, { 0x42209, 2 }, { 0x421df, 2 },
    { 0x421ef, 2 }, { 0x421f1, 2 }, { 0x421e9, 2 }, { 0x421f7, 2 },
    { 0x421f3, 2 }, { 0x421f9, 2 }, { 0x421ff, 2 }, { 0x42201, 2 },
    { 0x42203, 2 }, { 0x421f5, 2 },
};
#define COLUMN_COUNT (sizeof(COLUMNS) / sizeof(COLUMNS[0]))

/* The one place in this file that really is byte-addressed: COLUMNS is a
 * table of (offset, width) pairs, so there is no field expression to call.
 * Growing game.h twenty-two more times to name columns the report only
 * prints would not buy a reader anything. */
static unsigned read_field(const unsigned char *g, unsigned off, unsigned char size)
{
    if (size == 1) return *(const unsigned char *)(g + off);
    if (size == 2) return *(const unsigned short *)(g + off);
    return *(const unsigned int *)(g + off);
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Report_WriteLevelReport(Game *self, const char *pathname)
{
    char buf[256];
    FILE *sink, *out;
    unsigned idx;

    sink = fopen(GS_RPT_SCRIPTTEXTS, GS_RPT_MODE_W);   /* defect 1: not checked */
    out  = fopen(pathname, GS_RPT_MODE_W);
    if (out == NULL)
        return;

    GameLog_LogMessage(&g_logger, 3, GS_RPT_LOG_CREATE);

    self->setReportTallyA(0);
    self->setReportLevelsWithBonus(0);
    self->setReportLevelsWithLeo(0);
    self->setReportScoreTotal(0);
    self->scriptPlayer()->setTextBlocks(0);
    self->scriptPlayer()->setSplineLines(0);

    fputs(GS_RPT_TITLE, out);
    sprintf(buf, GS_RPT_GAMEFILE, self->gameFileName());
    fputs(buf, out);
    sprintf(buf, GS_RPT_LEVELS, (unsigned)self->levelCount());
    fputs(buf, out);
    fputs(GS_RPT_COLHDR1, out);
    fputs(GS_RPT_COLHDR2, out);
    fputs(GS_RPT_RULE, out);

    for (idx = 0; idx < (unsigned)self->levelCount(); idx++) {
        unsigned catches = 0, timeBonus, n = idx + 1;
        unsigned char catchByte = 0;   /* defect 5: a byte */
        int total;

        Sim_SetCurrentLevelName(self, idx);
        Sim_OpenLevelFile(self, idx);
        Sim_SetupLevelObjects(self);
        Score_CalculateLevelScore(self, 3);

        sprintf(buf, GS_RPT_D_TAB, n);
        fputs(buf, out);
        fputs(self->map()->mapName(), out);
        fputs(GS_FMT_TAB, out);

        if ((int)self->map()->bonus() == 0) {
            fputs(GS_RPT_BLANK_TAB, out);
        } else {
            fputs(GS_RPT_X_TAB, out);
            self->setReportLevelsWithBonus((unsigned short)(self->reportLevelsWithBonus() + 1));
        }

        if (self->scriptPlayer()->loaded() == 0) {
            fputs(GS_RPT_BLANK_TAB, out);
        } else {
            sprintf(buf, GS_RPT_D_TAB, (unsigned)self->scriptPlayer()->lineCount());
            fputs(buf, out);
            self->setReportLevelsWithScript((unsigned short)(self->reportLevelsWithScript() + 1));
        }

        if (self->extraObjects()->loaded() == 0) {
            fputs(GS_RPT_BLANK_TAB, out);
        } else {
            sprintf(buf, GS_RPT_D_TAB, (unsigned)self->extraObjects()->objectCount());
            fputs(buf, out);
            self->setReportLevelsWithLeo((unsigned short)(self->reportLevelsWithLeo() + 1));
        }

        Game *G = self;
        if (G->foeCount() != 0) {
            for (int i = 0; i < (int)G->foeCount(); i++) {
                /* kind 2 == "catch"; read signed, as the original does. */
                if ((signed char)G->foeSlot(G->foeId(i))->kind() == 2)
                    catchByte++;
            }
            catches = catchByte;
        }
        sprintf(buf, GS_RPT_D_TAB, catches);
        fputs(buf, out);
        sprintf(buf, GS_RPT_D_TAB, (unsigned)(G->foeCount() - catches));
        fputs(buf, out);

        for (unsigned c = 0; c < COLUMN_COUNT; c++) {
            sprintf(buf, GS_RPT_D_TAB,
                    read_field((const unsigned char *)self,
                               COLUMNS[c].off, COLUMNS[c].size));
            fputs(buf, out);
        }

        self->player()->setGemsCollected(
            self->gemsRequired());
        self->setVitalityPercent(0x32);
        timeBonus = (unsigned)(self->map()->fileTimeLimit() * 0x32) / 100;
        GameLog_LogMessage(&g_logger, 3, GS_RPT_LOG_TIME, timeBonus);
        Score_CalculateLevelScore(self, 2);

        sprintf(buf, GS_RPT_D_TAB, timeBonus);
        fputs(buf, out);

        total = self->reportScoreTotal()
              + ((self->tally()->score[TALLY_GEMS] + 0x78 + (int)timeBonus * 2
                  + self->tally()->score[TALLY_VITALITY]) - (int)idx);   /* defect 6 */
        self->setReportScoreTotal(total);
        sprintf(buf, GS_RPT_D_TAB, (unsigned)total);
        fputs(buf, out);

        Sim_SetCurrentLevelName(self, idx);
        sprintf(buf, GS_RPT_S_TAB, self->levelNameBuffer());
        fputs(buf, out);
        fputs(GS_FMT_NEWLINE, out);   /* defect 3: the title's sprintf is dead */

        /* defect 4: seed the high-score table BACKWARDS from record 9 (the
         * original steps its pointer down by one record per 8 levels) */
        if ((idx % 8 == 0 && idx > 5) || idx == 6) {
            int k = (int)idx / 8;
            HighScoreRecord *rec = self->highScores()->record(9 - k);
            strcpy(rec->name, GS_RPT_BERNIE);
            rec->level = (unsigned char)(idx + 1);
            rec->score = (unsigned int)self->reportScoreTotal();
        }

        {
            char scriptPath[256];
            sprintf(scriptPath, GS_OPEN_FMT_SCRIPTS, g_gameDir,
                    self->levelNameBuffer());

            fputs(GS_RPT_STARS, sink);
            sprintf(buf, GS_RPT_LVL_FILE, n, self->levelNameBuffer());
            fputs(buf, sink);
            sprintf(buf, GS_RPT_LVL_NAME, self->map()->title());
            fputs(buf, sink);

            if (self->scriptPlayer()->loaded() != 0)
                self->scriptPlayer()->readTextsForReport(scriptPath, sink);

            fputs(GS_FMT_NEWLINE, sink);
            fputs(GS_FMT_NEWLINE, sink);
        }
    }

    fputs(GS_RPT_RULE, out);
    sprintf(buf, GS_RPT_TALLY,
            (unsigned)self->reportLevelsWithBonus(),
            (unsigned)self->reportLevelsWithScript(),
            (unsigned)self->reportLevelsWithLeo());
    fputs(buf, out);
    sprintf(buf, GS_RPT_TESTSCORES, self->reportScoreTotal());
    fputs(buf, out);
    sprintf(buf, GS_RPT_TEXTS_IN, (unsigned)self->scriptPlayer()->textBlocks());
    fputs(buf, out);
    sprintf(buf, GS_RPT_SPLINES_IN, (unsigned)self->scriptPlayer()->splineLines());
    fputs(buf, out);

    GameLog_LogMessage(&g_logger, 3, GS_RPT_LOG_CREATED);   /* defect 2 */

    fclose(out);
    if (sink != NULL)
        fclose(sink);

    self->highScores()->writeFile(GS_RPT_HSC_NAME, 'K');
}
