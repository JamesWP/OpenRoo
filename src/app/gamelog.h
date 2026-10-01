#pragma once

/* The game's own logger (the game calls it CProto).  It writes the log
 * files: JJ.log from WinMain, StreamSoundBuffer.log from the sound code, and a
 * few more on setup paths that rarely fire.  Each line is a local timestamp
 * and the message; a message logs only if its level is at least minLevel (1).
 */
#include <windows.h>
#include <stdio.h>
#include <stddef.h>

class GameLogger {
public:
    /* Formats and writes one line: "HH:MM:SS : message". */
    void logMessage(int level, const char *fmt, ...);

    /* Opens (or reopens) the log file and writes the date banner.  Returns 0,
     * after a message box, if the file cannot be opened. */
    int openLogFile(const char *filename, const char *mode);

    /* A closed logger. */
    GameLogger();
    /* Constructs and opens in one; the file stays closed if it cannot be
     * opened. */
    GameLogger(const char *filename, const char *mode);
    /* Closes the file. */
    virtual ~GameLogger();
    GameLogger(const GameLogger &) = delete;
    GameLogger &operator=(const GameLogger &) = delete;

    /* Writes "HH:MM:SS : File: <file>, Line: <line>: message". */
    void logSourceLocation(int level, const char *file, int line,
                           const char *fmt, ...);

    void closeLogFile();
    int logWithErrorCode(int level, const char *message, HRESULT hr);

private:
    void emit(int level, const char *line);

 

    int    minLevel_;         // a message logs if level >= this
    char   fileName_[0x104];  // the path given to OpenLogFile
    FILE  *fp_;               // NULL until OpenLogFile succeeds
};

 
