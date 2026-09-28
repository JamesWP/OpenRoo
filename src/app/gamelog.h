#pragma once

/* The game's own logger (the game calls it CProto).  It writes the log
 * files: JJ.log from WinMain, StreamSoundBuffer.log from the sound code, and a
 * few more on setup paths that rarely fire.  Each line is a local timestamp
 * and the message; a message logs only if its level is at least minLevel (1).
 */
#include <windows.h>
#include <stdio.h>
#include <stddef.h>

#pragma pack(push, 1)
class GameLogger {
public:
    /* Formats and writes one line: "HH:MM:SS : message". */
    void logMessage(int level, const char *fmt, ...);

    /* Opens (or reopens) the log file and writes the date banner.  Returns 0,
     * after a message box, if the file cannot be opened. */
    int openLogFile(const char *filename, const char *mode);

    /* Constructs and opens in one. */
    void *initialize(const char *filename, const char *mode);

    /* Writes "HH:MM:SS : File: <file>, Line: <line>: message". */
    void logSourceLocation(int level, const char *file, int line,
                           const char *fmt, ...);

    /* Construction and destruction of the global logger, driven by
     * staticinit.cpp. */
    void construct();

    void closeAndRebindVtable();

    void  *vtable() const { return pVtable_; }

    void closeLogFile();
    static GameLogger * scalarDeletingDtor(GameLogger *self, unsigned char flags);
    int logWithErrorCode(int level, const char *message, HRESULT hr);

private:
    void emit(int level, const char *line);

 

    void  *pVtable_;          // our one-slot table
    int    minLevel_;         // a message logs if level >= this
    char   fileName_[0x104];  // the path given to OpenLogFile
    FILE  *fp_;               // NULL until OpenLogFile succeeds
    UINT   notifyWParam_;     // WM_COPYDATA mirror wParam; 0 disables
    HWND   notifyHwnd_;       // WM_COPYDATA mirror target; nothing ever sets it
};
#pragma pack(pop)

 
