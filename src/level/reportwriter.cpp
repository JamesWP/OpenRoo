/* The level report: for every level in the game file, load it, build its
 * objects and score it, writing one row of LevelReport.txt (the file the
 * caller names) and the level's script texts to ScriptTexts.txt; then the
 * totals, and a rewrite of the high-score file.  tools/levelreport.py runs it
 * as an all-80-levels test.
 *
 * PRESERVED:
 *   1. ScriptTexts.txt is not checked for a failed open; the report file
 *      is.
 *   2. "level report created" is logged before the files are closed, which
 *      is why the harness waits for the game to exit rather than killing
 *      it on that line.
 *   3. The high-score table is seeded with the default name for level 6
 *      and every eighth level after 5, writing backwards through the table.
 *      A side effect of the report, not part of it; the harness saves and
 *      restores highscores/ around it.
 *   4. The catch counter is a byte, so more than 255 catchers would wrap.
 *   5. The running score subtracts the level index, so the total depends on
 *      the level order, not only the levels. */

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

/* The table columns, in file order, each printed with "%d\t".  They are read
 * at fixed Game offsets because they are only ever printed; the widths differ,
 * hence the size. */
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

  void  
Report_WriteLevelReport(Game *self, const char *pathname)
{
    char buf[256];
    FILE *sink, *out;
    unsigned idx;

    sink = fopen(GS_RPT_SCRIPTTEXTS, GS_RPT_MODE_W);  // PRESERVED: not checked
    out  = fopen(pathname, GS_RPT_MODE_W);
    if (out == NULL)
        return;

    g_logger.logMessage(3, GS_RPT_LOG_CREATE);

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
        unsigned char catchByte = 0;  // PRESERVED: a byte
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
                // Kind 2 is a catcher; read signed.
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
        g_logger.logMessage(3, GS_RPT_LOG_TIME, timeBonus);
        Score_CalculateLevelScore(self, 2);

        sprintf(buf, GS_RPT_D_TAB, timeBonus);
        fputs(buf, out);

        total = self->reportScoreTotal()
              + ((self->tally()->score[TALLY_GEMS] + 0x78 + (int)timeBonus * 2
                  + self->tally()->score[TALLY_VITALITY]) - (int)idx);  // PRESERVED: minus the level index
        self->setReportScoreTotal(total);
        sprintf(buf, GS_RPT_D_TAB, (unsigned)total);
        fputs(buf, out);

        Sim_SetCurrentLevelName(self, idx);
        sprintf(buf, GS_RPT_S_TAB, self->levelNameBuffer());
        fputs(buf, out);
        fputs(GS_FMT_NEWLINE, out);  // the display name is not written; a newline is

        // PRESERVED: the default name seeds the high-score table backwards
        // from record 9, one record per eight levels.
        if ((idx % 8 == 0 && idx > 5) || idx == 6) {
            int k = (int)idx / 8;
            HighScoreRecord *rec = self->highScores()->record(9 - k);
            strcpy(rec->name, GS_RPT_DEFAULT_NAME);
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

    g_logger.logMessage(3, GS_RPT_LOG_CREATED);  // PRESERVED: logged before the files are closed

    fclose(out);
    if (sink != NULL)
        fclose(sink);

    self->highScores()->writeFile(GS_RPT_HSC_NAME, 'K');
}
