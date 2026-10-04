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

#include <fstream>
#include <stdio.h>
#include <string.h>
#include "logger.h"
#include "game.h"
#include "reportwriter.h"
#include "levelparse.h"
#include "levelsetup.h"
#include "levelscore.h"
#include "foe.h"
#include "player.h"
#include "gamestr.h"
#include "gameglobals.h"
#include "sysdev.h"

/* The table columns, in file order, each printed with "%d\t". */
static void column_values(Game *g, unsigned out[22])
{
    const LevelCensus *c = g->census();
    unsigned n = 0;
    out[n++] = g->field_42252();
    out[n++] = c->shadow1;
    out[n++] = (unsigned)g->gemsRequired();
    out[n++] = c->shadow7;
    out[n++] = c->kind01;
    out[n++] = c->destructibles;
    out[n++] = g->liftCount();
    out[n++] = g->slideCount();
    out[n++] = g->breakableCount();
    out[n++] = c->gluePads;
    out[n++] = c->jumpPads;
    out[n++] = c->teleports;
    out[n++] = c->climbTiles;
    out[n++] = c->conveyors;
    out[n++] = c->bridges;
    out[n++] = c->extraLives;
    out[n++] = c->effect8Items;
    out[n++] = c->transforms;
    out[n++] = c->paragliders;
    out[n++] = c->speedUps;
    out[n++] = c->grant09Items;
    out[n++] = c->timeBonuses;
}
#define COLUMN_COUNT 22

  void  
Report_WriteLevelReport(Game *self, const char *pathname)
{
    char buf[256];
    unsigned idx;

    std::ofstream sink(sysdev::nativePath(GS_RPT_SCRIPTTEXTS));  // PRESERVED: not checked
    std::ofstream out(sysdev::nativePath(pathname));
    if (!out)
        return;

    g_logger.logMessage(3, "GAME: create a level report");

    self->setReportTallyA(0);
    self->setReportLevelsWithBonus(0);
    self->setReportLevelsWithLeo(0);
    self->setReportScoreTotal(0);
    self->scriptPlayer()->setTextBlocks(0);
    self->scriptPlayer()->setSplineLines(0);

    out << GS_RPT_TITLE;
    sprintf(buf, GS_RPT_GAMEFILE, self->gameFileName());
    out << buf;
    sprintf(buf, GS_RPT_LEVELS, (unsigned)self->levelCount());
    out << buf;
    out << GS_RPT_COLHDR1;
    out << GS_RPT_COLHDR2;
    out << GS_RPT_RULE;

    for (idx = 0; idx < (unsigned)self->levelCount(); idx++) {
        unsigned catches = 0, timeBonus, n = idx + 1;
        unsigned char catchByte = 0;  // PRESERVED: a byte
        int total;

        Sim_SetCurrentLevelName(self, idx);
        Sim_OpenLevelFile(self, idx);
        Sim_SetupLevelObjects(self);
        Score_CalculateLevelScore(self, 3);

        sprintf(buf, GS_RPT_D_TAB, n);
        out << buf;
        out << self->map()->mapName();
        out << GS_FMT_TAB;

        if ((int)self->map()->bonus() == 0) {
            out << GS_RPT_BLANK_TAB;
        } else {
            out << GS_RPT_X_TAB;
            self->setReportLevelsWithBonus((unsigned short)(self->reportLevelsWithBonus() + 1));
        }

        if (self->scriptPlayer()->loaded() == 0) {
            out << GS_RPT_BLANK_TAB;
        } else {
            sprintf(buf, GS_RPT_D_TAB, (unsigned)self->scriptPlayer()->lineCount());
            out << buf;
            self->setReportLevelsWithScript((unsigned short)(self->reportLevelsWithScript() + 1));
        }

        if (self->extraObjects()->loaded() == 0) {
            out << GS_RPT_BLANK_TAB;
        } else {
            sprintf(buf, GS_RPT_D_TAB, (unsigned)self->extraObjects()->objectCount());
            out << buf;
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
        out << buf;
        sprintf(buf, GS_RPT_D_TAB, (unsigned)(G->foeCount() - catches));
        out << buf;

        unsigned columns[COLUMN_COUNT];
        column_values(self, columns);
        for (unsigned c = 0; c < COLUMN_COUNT; c++) {
            sprintf(buf, GS_RPT_D_TAB, columns[c]);
            out << buf;
        }

        self->player()->setGemsCollected(
            self->gemsRequired());
        self->setVitalityPercent(0x32);
        timeBonus = (unsigned)(self->map()->fileTimeLimit() * 0x32) / 100;
        g_logger.logMessage(3, "GAME: time:%d", timeBonus);
        Score_CalculateLevelScore(self, 2);

        sprintf(buf, GS_RPT_D_TAB, timeBonus);
        out << buf;

        total = self->reportScoreTotal()
              + ((self->tally()->score[TALLY_GEMS] + 0x78 + (int)timeBonus * 2
                  + self->tally()->score[TALLY_VITALITY]) - (int)idx);  // PRESERVED: minus the level index
        self->setReportScoreTotal(total);
        sprintf(buf, GS_RPT_D_TAB, (unsigned)total);
        out << buf;

        Sim_SetCurrentLevelName(self, idx);
        sprintf(buf, GS_RPT_S_TAB, self->levelNameBuffer());
        out << buf;
        out << GS_FMT_NEWLINE;  // the display name is not written; a newline is

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

            sink << GS_RPT_STARS;
            sprintf(buf, GS_RPT_LVL_FILE, n, self->levelNameBuffer());
            sink << buf;
            sprintf(buf, GS_RPT_LVL_NAME, self->map()->title());
            sink << buf;

            if (self->scriptPlayer()->loaded() != 0)
                self->scriptPlayer()->readTextsForReport(scriptPath, sink);

            sink << GS_FMT_NEWLINE;
            sink << GS_FMT_NEWLINE;
        }
    }

    out << GS_RPT_RULE;
    sprintf(buf, GS_RPT_TALLY,
            (unsigned)self->reportLevelsWithBonus(),
            (unsigned)self->reportLevelsWithScript(),
            (unsigned)self->reportLevelsWithLeo());
    out << buf;
    sprintf(buf, GS_RPT_TESTSCORES, self->reportScoreTotal());
    out << buf;
    sprintf(buf, GS_RPT_TEXTS_IN, (unsigned)self->scriptPlayer()->textBlocks());
    out << buf;
    sprintf(buf, GS_RPT_SPLINES_IN, (unsigned)self->scriptPlayer()->splineLines());
    out << buf;

    g_logger.logMessage(3, "GAME: level report created");  // PRESERVED: logged before the files are closed

    out.close();
    sink.close();

    self->highScores()->writeFile(GS_RPT_HSC_NAME, 'K');
}
