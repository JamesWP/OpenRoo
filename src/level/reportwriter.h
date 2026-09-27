/* The level report: holding L while the fixed sounds load writes it. */
#pragma once

class Game;

/* Loads every level in turn and writes LevelReport.txt (pathname: one row of
 * object counts, time bonus and cumulative score per level) and
 * ScriptTexts.txt (every script's texts), then seeds and saves the high-score
 * table. */
void Report_WriteLevelReport(Game *self, const char *pathname);
