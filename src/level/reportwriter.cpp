/* WriteLevelReport (0x41b980) — replaced so that no game-owned FILE * remains.
 *
 *   void __thiscall WriteLevelReport(Game *this, LPCSTR pathname)
 *   1 E8 call site (0x41a606, in AcquireFixedSoundBuffersAndMaybeReport);
 *   no E9, no PUSH, no vtable slot.  UD2-stubbed.
 *
 * ─── Why this function, and not the reader that had the problem ───────────
 *
 * Phase 4's report-side .jjs reader (karoo-hooks/scriptplayer.cpp) had to call
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

/* ─── Game data (DATA references, not calls) ─────────────────────────────── */

#define GAME_LOGGER   ((void *)0x0046c4c0)
#define GAME_DIR      ((const char *)0x004e01c4)

#define STR_SCRIPTTEXTS ((const char *)0x0046601c)  /* "ScriptTexts.txt"      */
#define STR_MODE_W      ((const char *)0x0046602c)  /* "w+t"                 */
#define STR_LOG_CREATE  ((const char *)0x00466000)  /* "GAME: create a ..."  */
#define STR_LOG_CREATED ((const char *)0x00465ce4)  /* "GAME: level report created" */
#define STR_LOG_TIME    ((const char *)0x00465de8)  /* "GAME: time:%d"       */
#define STR_TITLE       ((const char *)0x00465fe4)  /* "***** level report ******\n\n" */
#define STR_GAMEFILE    ((const char *)0x00465fd4)  /* "gamefile:%s\n"       */
#define STR_LEVELS      ((const char *)0x00465fc8)  /* "Levels:%d\n\n"       */
#define STR_COLHDR1     ((const char *)0x00465f14)  /* "level-\tworld\t..."  */
#define STR_COLHDR2     ((const char *)0x00465f04)  /* "nr\t \tlevel \n"     */
#define STR_RULE        ((const char *)0x00465e08)  /* 249 dashes + "\n"     */
#define STR_D_TAB       ((const char *)0x00465e04)  /* "%d\t"                */
#define STR_TAB         ((const char *)0x00465e00)  /* "\t"                  */
#define STR_BLANK_TAB   ((const char *)0x00465df8)  /* " \t"                 */
#define STR_X_TAB       ((const char *)0x00465dfc)  /* "X\t"                 */
#define STR_S_TAB       ((const char *)0x00465de4)  /* "%s\t"                */
#define STR_NEWLINE     ((const char *)0x00465160)  /* "\n"                  */
#define STR_BERNIE      ((const char *)0x00465dd4)  /* "Bernie Boulder"      */
#define STR_STARS       ((const char *)0x00465d8c)  /* "****...****\n"       */
#define STR_LVL_FILE    ((const char *)0x00465d70)  /* "** Level %d  Filename:%s \n" */
#define STR_LVL_NAME    ((const char *)0x00465d5c)  /* "** Levelname: %s\n"  */
#define STR_TALLY       ((const char *)0x00465d40)  /* "\t\t%d\t%d\t%d\n"    */
#define STR_TESTSCORES  ((const char *)0x00465d30)  /* "\nTestscores:%d"     */
#define STR_TEXTS_IN    ((const char *)0x00465d18)  /* "\nTexts in Scripts:%d" */
#define STR_SPLINES_IN  ((const char *)0x00465d00)  /* "\nSplines in Scripts:%d" */
#define STR_HSC_NAME    ((const char *)0x00465cdc)  /* "jj.hsc"              */
#define STR_IS_PATH     ((const char *)0x00465878)  /* "%s\InstructionScripts\%s" */

/* ─── Game logic, deliberately still the game's ──────────────────────────── */

/* Was ((setname_fn) 0x004186b0) / ((openlvl_fn) 0x004186f0) -- the game's
 * Game::SetCurrentLevelName and Game::OpenLevelFile.  levelparse.cpp owns
 * both now (GAMETICK_PLAN.md Band B) and the originals are UD2-stubbed, so
 * these go to ours -- the same move the score call below already made, and
 * for the same reason.
 *
 * These two calls are why the level report is an 80-level acceptance test for
 * the loader replacements.  They are also the second instance of the hazard
 * the comment below names: the cycle that replaced them patched all 20 E8
 * sites in the EXE, and the replay suite passed 16/16, because the ONLY
 * caller left was in our own DLL.  `levelreport.py` failed with nine
 * c000001d, and the UD2 stub named the address. */
/* Both through levelparse.h. */
#define ORIG_SET_LEVEL_NAME(s, n) Sim_SetCurrentLevelName((Game *)(s), (n))
#define ORIG_OPEN_LEVEL(s, n)     Sim_OpenLevelFile((Game *)(s), (n))
/* Was ((setup_fn) 0x00416420) -- the game's Game::SetupLevelObjects.
 * levelsetup.cpp owns it now (GAMETICK_PLAN.md Band B) and the original is
 * UD2-stubbed, so this goes to ours.  Third instance of the DLL-caller
 * hazard the comment below names; checked BEFORE stubbing this time. */
#define ORIG_SETUP_OBJECTS(s)     Sim_SetupLevelObjects((Game *)(s))   /* levelsetup.h */
/* Was a ((score_fn) 0x0041a760) call -- the game's Game::CalculateLevelScore.
 * levelscore.cpp owns it now (GAMETICK_PLAN.md Band A) and the original is
 * UD2-stubbed, so this goes to ours.  This call is why the level report is an
 * 80-level acceptance test for that replacement.
 *
 * The absolute-address call here is exactly the kind an `xref.py` scan of the
 * EXE cannot see: it lives in our DLL, so the exe holds no reference to
 * 0x0041a760 at all -- not an E8, not a 68 imm32, not even the raw four bytes.
 * Stubbing the original trapped at `call eax` with eax = 0041a760 and nothing
 * in the binary to explain it.  When replacing anything, grep karoo-hooks/ for
 * its address as well as running xref.py over the exe. */
/* Through levelscore.h, typed -- no cast, so a signature change here is a
 * compile error rather than a crash. */
#define ORIG_CALC_SCORE(s, m)     Score_CalculateLevelScore((s), (m))
/* Was ((logmsg_fn) 0x00441b10) -- the game's Logger::LogMessage.  gamelog.cpp
 * owns that class now and the original is UD2-stubbed, so this goes to ours. */
#define ORIG_LOG_MESSAGE \
    ((void (__cdecl *)(void *, int, const char *, ...))GameLog_LogMessage)

/* Score_CalculateLevelScore: ours since GAMETICK_PLAN.md Band A, declared
 * in levelscore.h. */



/* ─── Game field offsets ─────────────────────────────────────────────────── */

#define OFF_TALLY_A       0x42243   /* WORD, zeroed, never written after      */
#define OFF_TALLY_BONUS   0x42245   /* WORD, levels with a bonus              */
#define OFF_TALLY_LEO     0x42247   /* WORD, levels with a .leo               */
#define OFF_TALLY_IS      0x4220c   /* WORD, levels with an instruction script*/
#define OFF_SCORE_TOTAL   0x42212   /* DWORD, running total                   */
/* The game file name (was OFF_GAMEFILE 0x4215f) is Game::gameFileName(). */
/* The map name (was OFF_LEVEL_WORLD 0x2ab69d), bonus flag (OFF_BONUS_FLAG
 * 0x2ab599), file time limit (OFF_PAR_TIME_SRC 0x2ab71f, the par time before
 * the 50% scaling) and title (OFF_LEVEL_TITLE 0x2ab61d) are the LevelMap's
 * (levelmap.h), through Game::map(). */
/* OFF_PAR_COPY was Player +0x23d (player.h), the crystals count. */
#define OFF_LEVEL_PATH    0x173483  /* char[], <World>\<Level>                */

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

static unsigned read_field(const unsigned char *g, unsigned off, unsigned char size)
{
    if (size == 1) return *(const unsigned char *)(g + off);
    if (size == 2) return *(const unsigned short *)(g + off);
    return *(const unsigned int *)(g + off);
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Report_WriteLevelReport(Game *self, const char *pathname)
{
    unsigned char *g = (unsigned char *)self;
    char buf[256];
    FILE *sink, *out;
    unsigned idx;

    sink = fopen(STR_SCRIPTTEXTS, STR_MODE_W);   /* defect 1: not checked */
    out  = fopen(pathname, STR_MODE_W);
    if (out == NULL)
        return;

    ORIG_LOG_MESSAGE(GAME_LOGGER, 3, STR_LOG_CREATE);

    *(WORD  *)(g + OFF_TALLY_A)     = 0;
    *(WORD  *)(g + OFF_TALLY_BONUS) = 0;
    *(WORD  *)(g + OFF_TALLY_LEO)   = 0;
    *(DWORD *)(g + OFF_SCORE_TOTAL) = 0;
    ((Game *)g)->scriptPlayer()->setTextBlocks(0);
    ((Game *)g)->scriptPlayer()->setSplineLines(0);

    fputs(STR_TITLE, out);
    sprintf(buf, STR_GAMEFILE, ((Game *)g)->gameFileName());
    fputs(buf, out);
    sprintf(buf, STR_LEVELS, (unsigned)((const Game *)g)->levelCount());
    fputs(buf, out);
    fputs(STR_COLHDR1, out);
    fputs(STR_COLHDR2, out);
    fputs(STR_RULE, out);

    for (idx = 0; idx < (unsigned)((const Game *)g)->levelCount(); idx++) {
        unsigned catches = 0, timeBonus, n = idx + 1;
        unsigned char catchByte = 0;   /* defect 5: a byte */
        int total;

        ORIG_SET_LEVEL_NAME(self, idx);
        ORIG_OPEN_LEVEL(self, idx);
        ORIG_SETUP_OBJECTS(self);
        ORIG_CALC_SCORE(self, 3);

        sprintf(buf, STR_D_TAB, n);
        fputs(buf, out);
        fputs(((Game *)g)->map()->mapName(), out);
        fputs(STR_TAB, out);

        if ((int)((Game *)g)->map()->bonus() == 0) {
            fputs(STR_BLANK_TAB, out);
        } else {
            fputs(STR_X_TAB, out);
            *(short *)(g + OFF_TALLY_BONUS) += 1;
        }

        if (((Game *)g)->scriptPlayer()->loaded() == 0) {
            fputs(STR_BLANK_TAB, out);
        } else {
            sprintf(buf, STR_D_TAB, (unsigned)((Game *)g)->scriptPlayer()->lineCount());
            fputs(buf, out);
            *(short *)(g + OFF_TALLY_IS) += 1;
        }

        if (((Game *)g)->extraObjects()->loaded() == 0) {
            fputs(STR_BLANK_TAB, out);
        } else {
            sprintf(buf, STR_D_TAB, (unsigned)((Game *)g)->extraObjects()->objectCount());
            fputs(buf, out);
            *(short *)(g + OFF_TALLY_LEO) += 1;
        }

        Game *G = (Game *)g;
        if (G->foeCount() != 0) {
            for (int i = 0; i < (int)G->foeCount(); i++) {
                /* kind 2 == "catch"; read signed, as the original does. */
                if ((signed char)G->foeSlot(G->foeId(i))->kind() == 2)
                    catchByte++;
            }
            catches = catchByte;
        }
        sprintf(buf, STR_D_TAB, catches);
        fputs(buf, out);
        sprintf(buf, STR_D_TAB, (unsigned)(G->foeCount() - catches));
        fputs(buf, out);

        for (unsigned c = 0; c < COLUMN_COUNT; c++) {
            sprintf(buf, STR_D_TAB, read_field(g, COLUMNS[c].off, COLUMNS[c].size));
            fputs(buf, out);
        }

        ((Game *)g)->player()->setGemsCollected(
            ((Game *)g)->gemsRequired());
        ((Game *)g)->setVitalityPercent(0x32);
        timeBonus = (unsigned)(((Game *)g)->map()->fileTimeLimit() * 0x32) / 100;
        ORIG_LOG_MESSAGE(GAME_LOGGER, 3, STR_LOG_TIME, timeBonus);
        ORIG_CALC_SCORE(self, 2);

        sprintf(buf, STR_D_TAB, timeBonus);
        fputs(buf, out);

        total = *(int *)(g + OFF_SCORE_TOTAL)
              + ((((Game *)g)->tally()->score[TALLY_GEMS] + 0x78 + (int)timeBonus * 2
                  + ((Game *)g)->tally()->score[TALLY_VITALITY]) - (int)idx);   /* defect 6 */
        *(int *)(g + OFF_SCORE_TOTAL) = total;
        sprintf(buf, STR_D_TAB, (unsigned)total);
        fputs(buf, out);

        ORIG_SET_LEVEL_NAME(self, idx);
        sprintf(buf, STR_S_TAB, (char *)(g + OFF_LEVEL_PATH));
        fputs(buf, out);
        fputs(STR_NEWLINE, out);   /* defect 3: the title's sprintf is dead */

        /* defect 4: seed the high-score table BACKWARDS from record 9 (the
         * original steps its pointer down by one record per 8 levels) */
        if ((idx % 8 == 0 && idx > 5) || idx == 6) {
            int k = (int)idx / 8;
            HighScoreRecord *rec = ((Game *)g)->highScores()->record(9 - k);
            strcpy(rec->name, STR_BERNIE);
            rec->level = (unsigned char)(idx + 1);
            rec->score = (unsigned int)*(int *)(g + OFF_SCORE_TOTAL);
        }

        {
            char scriptPath[256];
            sprintf(scriptPath, STR_IS_PATH, GAME_DIR,
                    (char *)(g + OFF_LEVEL_PATH));

            fputs(STR_STARS, sink);
            sprintf(buf, STR_LVL_FILE, n, (char *)(g + OFF_LEVEL_PATH));
            fputs(buf, sink);
            sprintf(buf, STR_LVL_NAME, ((Game *)g)->map()->title());
            fputs(buf, sink);

            if (((Game *)g)->scriptPlayer()->loaded() != 0)
                ((Game *)g)->scriptPlayer()->readTextsForReport(scriptPath, sink);

            fputs(STR_NEWLINE, sink);
            fputs(STR_NEWLINE, sink);
        }
    }

    fputs(STR_RULE, out);
    sprintf(buf, STR_TALLY,
            (unsigned)*(WORD *)(g + OFF_TALLY_BONUS),
            (unsigned)*(WORD *)(g + OFF_TALLY_IS),
            (unsigned)*(WORD *)(g + OFF_TALLY_LEO));
    fputs(buf, out);
    sprintf(buf, STR_TESTSCORES, *(int *)(g + OFF_SCORE_TOTAL));
    fputs(buf, out);
    sprintf(buf, STR_TEXTS_IN, (unsigned)((Game *)g)->scriptPlayer()->textBlocks());
    fputs(buf, out);
    sprintf(buf, STR_SPLINES_IN, (unsigned)((Game *)g)->scriptPlayer()->splineLines());
    fputs(buf, out);

    ORIG_LOG_MESSAGE(GAME_LOGGER, 3, STR_LOG_CREATED);   /* defect 2 */

    fclose(out);
    if (sink != NULL)
        fclose(sink);

    ((Game *)g)->highScores()->writeFile(STR_HSC_NAME, 'K');
}
