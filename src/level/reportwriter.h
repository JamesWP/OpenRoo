/* The level report (reportwriter.cpp): the export patch.py binds, typed on
 * the Game it takes in ECX. */
#pragma once

class Game;

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Report_WriteLevelReport(Game *self, const char *pathname);
