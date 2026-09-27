#pragma once

/* The game's own logger (the game calls it CProto).  It writes the log
 * files: JJ.log from WinMain, StreamSoundBuffer.log from the sound code, and a
 * few more on setup paths that rarely fire.  Each line is a local timestamp
 * and the message; a message logs only if its level is at least minLevel (1).
 */
#include <windows.h>
#include <stdio.h>
#include <stddef.h>

struct GameLogger {
    void  *pVtable;          // our one-slot table
    int    minLevel;         // a message logs if level >= this
    char   fileName[0x104];  // the path given to OpenLogFile
    FILE  *fp;               // NULL until OpenLogFile succeeds
    UINT   notifyWParam;     // WM_COPYDATA mirror wParam; 0 disables
    HWND   notifyHwnd;       // WM_COPYDATA mirror target; nothing ever sets it
};

/* Formats and writes one line: "HH:MM:SS : message". */
void GameLog_LogMessage(GameLogger *self, int level, const char *fmt, ...);

/* Opens (or reopens) the log file and writes the date banner.  Returns 0,
 * after a message box, if the file cannot be opened. */
int
GameLog_OpenLogFile(GameLogger *self, const char *filename, const char *mode);

/* Constructs and opens in one. */
void *
GameLog_Initialize(GameLogger *self, const char *filename, const char *mode);

/* Writes "HH:MM:SS : File: <file>, Line: <line>: message". */
void GameLog_LogSourceLocation(GameLogger *self, int level, const char *file,
                               int line, const char *fmt, ...);

/* Construction and destruction of the global logger, driven by staticinit.cpp.
 */
void GameLog_Construct(GameLogger *self);
void GameLog_CloseAndRebindVtable(GameLogger *self);
