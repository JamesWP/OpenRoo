#pragma once

/* The game's own logger (the game calls it CProto).  It writes the "Protokoll"
 * files: JJ.log from WinMain, StreamSoundBuffer.log from the sound code, and a
 * few more on setup paths that rarely fire.  Each line is a local timestamp
 * and the message; a message logs only if its level is at least minLevel (1).
 */
#include <windows.h>
#include <stdio.h>
#include <stddef.h>

#pragma pack(push, 1)
struct GameLogger {
    void  *pVtable;          // our one-slot table
    int    minLevel;         // a message logs if level >= this
    char   fileName[0x104];  // the path given to OpenLogFile
    FILE  *fp;               // NULL until OpenLogFile succeeds
    UINT   notifyWParam;     // WM_COPYDATA mirror wParam; 0 disables
    HWND   notifyHwnd;       // WM_COPYDATA mirror target; nothing ever sets it
};
#pragma pack(pop)

static_assert(sizeof(GameLogger) == 0x118, "must match operator new(0x118)");
static_assert(offsetof(GameLogger, minLevel)     == 0x004, "minLevel");
static_assert(offsetof(GameLogger, fileName)     == 0x008, "fileName");
static_assert(offsetof(GameLogger, fp)           == 0x10c, "fp");
static_assert(offsetof(GameLogger, notifyWParam) == 0x110, "notifyWParam");
static_assert(offsetof(GameLogger, notifyHwnd)   == 0x114, "notifyHwnd");

/* Formats and writes one line: "HH:MM:SS : message". */
extern "C" __declspec(dllexport) void __cdecl
GameLog_LogMessage(GameLogger *self, int level, const char *fmt, ...);

/* Opens (or reopens) the log file and writes the date banner.  Returns 0,
 * after a message box, if the file cannot be opened. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
GameLog_OpenLogFile(GameLogger *self, const char *filename, const char *mode);

/* Constructs and opens in one. */
extern "C" __declspec(dllexport) void * __attribute__((thiscall))
GameLog_Initialize(GameLogger *self, const char *filename, const char *mode);

/* Writes "HH:MM:SS : File: <file>, Line: <line>: message". */
extern "C" __declspec(dllexport) void __cdecl
GameLog_LogSourceLocation(GameLogger *self, int level, const char *file,
                          int line, const char *fmt, ...);

/* Construction and destruction of the global logger, driven by staticinit.cpp.
 */
extern "C" __declspec(dllexport) void __attribute__((thiscall)) GameLog_Construct(GameLogger *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall)) GameLog_CloseAndRebindVtable(GameLogger *self);
