/* Everything below the logger is CRT stdio and Win32: the class is the whole
 * of the game's logging.
 *
 * PRESERVED:
 *   1. Both variadic writers vsprintf the caller's format into a fixed
 *      3000-byte stack buffer with no bound.
 *   2. The file gets strlen(line) bytes, the WM_COPYDATA mirror
 *      strlen(line) + 1.
 *   3. OpenLogFile formats the banner, and so reads the date, before it
 *      opens the file.
 *   4. The default fopen mode is "wc", MSVC's commit-on-flush extension.
 *   5. Initialize builds and tears down a throwaway logger on the stack.
 *
 * The WM_COPYDATA mirror is dead in the game (its target is never set) and the
 * "Could not open Protofile" box and most of the DirectSound error table have
 * never been seen to fire.
 *
 * KAROO_GAMELOG_FX is a negative control: "mark" prefixes every line with
 * "KHOOK "; "off" suppresses every write but the banner.  At first use the
 * format strings are compared with the game's (gamestr.h), and any mismatch is
 * logged to karoo_hooks.log. */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stddef.h>
#include "gamelog.h"
#include "log.h"
#include <stdlib.h>
#include "gamestr.h"

/* The one-slot vtable: the scalar deleting destructor. */
static void *const game_logger_vtable_slots[1] = { (void *)&GameLogger::scalarDeletingDtor };
#define GAME_LOGGER_VTABLE ((void *)game_logger_vtable_slots)

/* FORMAT: the line formats, as the game's; checked against gamestr.h at first
 * use.  Kept as literals so the control's prefix can splice in. */
#define FMT_BANNER  "\n***************** Log started on %s ***********************\n"
#define FMT_LINE    "%s : %s\r\n"
#define FMT_SRCLINE "%s : File: %s, Line: %d: %s \r\n"
#define FMT_ERRLINE "%s : Error %s: %s \r\n"
#define MODE_WC     "wc"

#define LOG_BUF      3000  // the game's buffer size
#define WM_COPYDATA_ 0x4a

static void verify_one(const char *ours, const char *theirs, const char *what)
{
    if (strcmp(ours, theirs) != 0)
        log_write("gamelog: FORMAT MISMATCH (%s): ours=%s theirs=%s\n",
                  what, ours, theirs);
}

static void verify_format_strings(void)
{
    static int done = 0;
    if (done)
        return;
    done = 1;
    verify_one(FMT_BANNER,  GS_LOG_BANNER,  "banner");
    verify_one(FMT_LINE,    GS_LOG_LINE,    "line");
    verify_one(FMT_SRCLINE, GS_LOG_SRCLINE, "srcline");
    verify_one(FMT_ERRLINE, GS_LOG_ERRLINE, "errline");
    verify_one(MODE_WC,     GS_LOG_MODE_WC, "mode");
}

enum GameLogFx { FX_NONE = 0, FX_MARK, FX_OFF };

static GameLogFx gamelog_fx(void)
{
    static int cached = -1;
    if (cached < 0) {
        char v[32];
        // By value, never by presence: an empty and an unset variable look
        // alike.
        DWORD n = GetEnvironmentVariableA("KAROO_GAMELOG_FX", v, sizeof(v));
        cached = FX_NONE;
        if (n > 0 && n < sizeof(v)) {
            if (strcmp(v, "mark") == 0)      cached = FX_MARK;
            else if (strcmp(v, "off") == 0)  cached = FX_OFF;
        }
    }
    return (GameLogFx)cached;
}

static const char *fx_prefix(void)
{
    return gamelog_fx() == FX_MARK ? "KHOOK " : "";
}

/* "HH:MM:SS", digit by digit. */
static char *format_time(char *out)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    out[8] = '\0';
    out[5] = ':';
    out[2] = ':';
    out[0] = (char)(st.wHour   / 10) + '0';
    out[1] = (char)(st.wHour   % 10) + '0';
    out[3] = (char)(st.wMinute / 10) + '0';
    out[4] = (char)(st.wMinute % 10) + '0';
    out[6] = (char)(st.wSecond / 10) + '0';
    out[7] = (char)(st.wSecond % 10) + '0';
    return out;
}

/* "MM/DD/YY": US order, two-digit year. */
static char *format_date(char *out)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    out[8] = '\0';
    out[5] = '/';
    out[2] = '/';
    out[0] = (char)(st.wMonth / 10) + '0';
    out[1] = (char)(st.wMonth % 10) + '0';
    out[3] = (char)(st.wDay   / 10) + '0';
    out[4] = (char)(st.wDay   % 10) + '0';
    out[6] = (char)((st.wYear % 100) / 10) + '0';
    out[7] = (char)((st.wYear % 100) % 10) + '0';
    return out;
}

/* What every writer ends with: write and flush the line, then mirror it to the
 * notify window. */
void GameLogger::emit(int level, const char *line)
{
    if (fp_ && gamelog_fx() != FX_OFF) {
        fwrite(line, 1, strlen(line), fp_);
        fflush(fp_);
    }
    // Dead in the game: nothing sets these two fields.
    if (notifyHwnd_ && notifyWParam_) {
        COPYDATASTRUCT cds;
        cds.dwData = (ULONG_PTR)level;
        cds.cbData = (DWORD)(strlen(line) + 1);  // PRESERVED: +1 here only
        cds.lpData = (PVOID)line;
        SendMessageA(notifyHwnd_, WM_COPYDATA_, (WPARAM)notifyWParam_,
                     (LPARAM)&cds);
    }
}

void GameLogger::closeAndRebindVtable()
{
    pVtable_ = GAME_LOGGER_VTABLE;
    if (fp_)
        fclose(fp_);

/* PRESERVED: fp is left dangling.  The only reuse, OpenLogFile, goes through
 * CloseLogFile, which clears it. */
}

/* Closes the file and clears the sink fields. */
void GameLogger::closeLogFile()
{
    if (fp_)
        fclose(fp_);
    fp_           = NULL;
    notifyWParam_ = 0;
    notifyHwnd_   = NULL;
}

void GameLogger::construct()
{
    verify_format_strings();
    pVtable_      = GAME_LOGGER_VTABLE;
    minLevel_     = 1;     // level-0 messages are off by default
    fileName_[0]  = '\0';  // PRESERVED: only byte 0; the rest stays uninitialised
    fp_           = NULL;
    notifyWParam_ = 0;
    notifyHwnd_   = NULL;
}

void *__attribute__((thiscall))
GameLogger::scalarDeletingDtor(GameLogger *self, unsigned char flags)
{
    self->closeAndRebindVtable();
    if (flags & 1)
        free(self);
    return self;
}

int GameLogger::openLogFile(const char *filename, const char *mode)
{
    char date[16];
    char banner[256];

    verify_format_strings();
    closeLogFile();

    if (mode == NULL)
        mode = MODE_WC;  // PRESERVED: MSVC's "wc"

    minLevel_ = 1;

    // PRESERVED: the banner is built before the open.
    format_date(date);
    sprintf(banner, FMT_BANNER, date);

    strcpy(fileName_, filename);

    fp_ = fopen(fileName_, mode);
    if (fp_ == NULL) {
        log_write("gamelog: could not open %s (mode %s)\n", fileName_, mode);
        MessageBoxA(NULL, GS_LOG_MB_TEXT, GS_LOG_MB_CAPT, 0);
        return 0;
    }

    fwrite(banner, 1, strlen(banner), fp_);
    fflush(fp_);
    log_write("gamelog: opened %s (mode %s)\n", fileName_, mode);
    return 1;
}

void *GameLogger::initialize(const char *filename, const char *mode)
{
    GameLogger scratch;

    pVtable_ = GAME_LOGGER_VTABLE;

    // PRESERVED: the throwaway stack logger, built and torn down for nothing.
    scratch.construct();
    scratch.closeAndRebindVtable();

    if (openLogFile(filename, mode) == 0)
        closeLogFile();

    return this;
}

void GameLogger::logMessage(int level, const char *fmt, ...)
{
    char timebuf[0x80];
    char msg[LOG_BUF];
    char out[LOG_BUF];
    va_list ap;

    if (level < minLevel_)
        return;

    format_time(timebuf);

    va_start(ap, fmt);
    vsprintf(msg, fmt, ap);  // PRESERVED: unbounded
    va_end(ap);

    sprintf(out, "%s" FMT_LINE, fx_prefix(), timebuf, msg);
    emit(level, out);
}

/* The file comes before the line: (time, file, line, message). */
void GameLogger::logSourceLocation(int level, const char *file,
                          int line, const char *fmt, ...)
{
    char timebuf[0x80];
    char msg[LOG_BUF];
    char out[LOG_BUF];
    va_list ap;

    if (level < minLevel_)
        return;

    format_time(timebuf);

    va_start(ap, fmt);
    vsprintf(msg, fmt, ap);  // PRESERVED: unbounded
    va_end(ap);

    sprintf(out, "%s" FMT_SRCLINE, fx_prefix(), timebuf, file, line, msg);
    emit(level, out);
}

/* HRESULT to DirectSound error name, as the game's strings. */
extern "C" __declspec(dllexport) const char * __cdecl
GameLog_DSErrorToString(HRESULT hr)
{
    switch ((unsigned)hr) {
    case 0x80004001u: return GS_LOG_DSERR_UNSUPPORTED;
    case 0x80004002u: return GS_LOG_DSERR_NOINTERFACE;
    case 0x80004005u: return GS_LOG_DSERR_GENERIC;
    case 0x80040110u: return GS_LOG_DSERR_NOAGGREGATION;
    case 0x8007000Eu: return GS_LOG_DSERR_OUTOFMEMORY;
    case 0x80070057u: return GS_LOG_DSERR_INVALIDPARAM;
    case 0x8878000Au: return GS_LOG_DSERR_ALLOCATED;
    case 0x8878001Eu: return GS_LOG_DSERR_CONTROLUNAVAIL;
    case 0x88780032u: return GS_LOG_DSERR_INVALIDCALL;
    case 0x88780046u: return GS_LOG_DSERR_PRIOLEVELNEEDED;
    case 0x88780064u: return GS_LOG_DSERR_BADFORMAT;
    case 0x88780078u: return GS_LOG_DSERR_NODRIVER;
    case 0x88780082u: return GS_LOG_DSERR_ALREADYINITIALIZED;
    case 0x88780096u: return GS_LOG_DSERR_BUFFERLOST;
    case 0x887800A0u: return GS_LOG_DSERR_OTHERAPPHASPRIO;
    case 0x887800AAu: return GS_LOG_DSERR_UNINITIALIZED;
    default:          return GS_LOG_HR_UNKNOWN;
    }
}

/* Logs "HH:MM:SS : Error <name>: message".  Returns level, which no caller
 * reads. */
int GameLogger::logWithErrorCode(int level, const char *message,
                         HRESULT hr)
{
    char timebuf[0x80];
    const char *err;
    char *out;

    if (level < minLevel_)
        return level;

    format_time(timebuf);

    // PRESERVED: sized strlen(message) + 0x31, which a long error name and a
    // long message can overrun.  The control's prefix is added on top, so the
    // control cannot be what overflows it.
    out = (char *)malloc(strlen(message) + 0x31 + strlen(fx_prefix()));
    if (out == NULL)
        return level;

    err = GameLog_DSErrorToString(hr);
    sprintf(out, "%s" FMT_ERRLINE, fx_prefix(), timebuf, err, message);

    emit(level, out);

    free(out);
    return level;
}
