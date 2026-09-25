#pragma once
/* The game's own logger ("CProto") -- see gamelog.cpp for the derivation. */
#include <windows.h>
#include <stdio.h>
#include <stddef.h>

#pragma pack(push, 1)
struct GameLogger {
    void  *pVtable;          /* +0x000  LoggerVtable (0x0045ef98)            */
    int    minLevel;         /* +0x004  a message logs iff level >= this     */
    char   fileName[0x104];  /* +0x008  the path handed to OpenLogFile       */
    FILE  *fp;               /* +0x10c  NULL until OpenLogFile succeeds      */
    UINT   notifyWParam;     /* +0x110  WM_COPYDATA wParam; 0 disables       */
    HWND   notifyHwnd;       /* +0x114  WM_COPYDATA target; NULL disables    */
};
#pragma pack(pop)

static_assert(sizeof(GameLogger) == 0x118, "must match operator new(0x118)");
static_assert(offsetof(GameLogger, minLevel)     == 0x004, "minLevel");
static_assert(offsetof(GameLogger, fileName)     == 0x008, "fileName");
static_assert(offsetof(GameLogger, fp)           == 0x10c, "fp");
static_assert(offsetof(GameLogger, notifyWParam) == 0x110, "notifyWParam");
static_assert(offsetof(GameLogger, notifyHwnd)   == 0x114, "notifyHwnd");

/* The replacement writer, for other hooks sources that used to reach into the
 * game binary at 0x00441b10.  Same __cdecl variadic shape as the original. */
extern "C" __declspec(dllexport) void __cdecl
GameLog_LogMessage(GameLogger *self, int level, const char *fmt, ...);

/* 0x004418b0 -- open (or reopen) the log file; WinMain opens "JJ.log". */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
GameLog_OpenLogFile(GameLogger *self, const char *filename, const char *mode);

/* 0x00441860 -- construct and open in one (SoundManager's own log). */
extern "C" __declspec(dllexport) void * __attribute__((thiscall))
GameLog_Initialize(GameLogger *self, const char *filename, const char *mode);

/* The file/line writer (0x00441d20), same shape.  The particle Save/Load slots
 * report failures through it, so particles.cpp calls it here rather than
 * reaching into the game image. */
extern "C" __declspec(dllexport) void __cdecl
GameLog_LogSourceLocation(GameLogger *self, int level, const char *file,
                          int line, const char *fmt, ...);

/* Constructor and destructor body, driven by staticinit.cpp for the one
 * global instance (the original's static-init/atexit thunks). */
extern "C" __declspec(dllexport) void __attribute__((thiscall)) GameLog_Construct(GameLogger *self);   /* 0x00441810 */
extern "C" __declspec(dllexport) void __attribute__((thiscall)) GameLog_CloseAndRebindVtable(GameLogger *self);   /* 0x00441a00 */
