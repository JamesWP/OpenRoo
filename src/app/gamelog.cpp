/* ═══════════════════════════════════════════════════════════════════════════
 * gamelog.cpp -- the game's own logger, reimplemented.
 *
 * The game class is `CProto` -- the name survives in an error-box caption at
 * 0x0046737c, "CProto::CProto(...)"; Ghidra calls it `Logger`.  It writes the
 * German-titled "Protokoll" files found in the game directory: JJ.log and
 * StreamSoundBuffer.log in a normal run, plus SoundManager.log, CFaktSound.log,
 * ProgableControl.log and FaktMovie.log on paths that do not fire in our
 * configuration.
 *
 * ─── Why the class is the right seam ───────────────────────────────────────
 *
 * ABOVE it are ~230 call sites spread across the whole game -- level parsing,
 * sound, movie, input, the level report -- all of which stay the game's.
 * BELOW it there is no game code at all: every callee is CRT (fopen, fwrite,
 * fflush, fclose, vsprintf, sprintf, strlen, strcpy) or Win32 (GetLocalTime,
 * SendMessageA, MessageBoxA).  Taking the class whole therefore replaces every
 * line of logging logic AND leaves nothing of it behind, and the replacement
 * calls back into Karoo.exe in exactly one place (see the dtor).
 *
 * Going lower -- hooking the CRT stdio the logger sits on -- would have been
 * wrong twice over: it would leave the message formatting, the level gate, the
 * banner and the WM_COPYDATA mirror in the game binary, and it would catch
 * every other fwrite in the program as collateral.  Going higher is not
 * possible; above this class there is no logging code, only callers.
 *
 * ─── The ten functions ─────────────────────────────────────────────────────
 *
 *   0x00441810  Logger()                  default ctor                 3 refs
 *   0x00441840  ~Logger(byte)             scalar-deleting dtor   vtable slot 0
 *   0x00441860  Initialize(name, mode)    ctor + open                  4 refs
 *   0x004418b0  OpenLogFile(name, mode)                                3 refs
 *   0x004419c0  CloseLogFile()                            internal callers only
 *   0x00441a00  CloseAndRebindVtable()    dtor body                    4 refs
 *   0x00441b10  LogMessage(log, lvl, fmt, ...)                      ~180 refs
 *   0x00441d20  LogSourceLocation(log, lvl, file, line, fmt, ...)     28 refs
 *   0x00441e30  DSErrorToString(HRESULT)                  internal callers only
 *   0x00441f90  LogWithErrorCode(lvl, msg, hr)                        14 refs
 *
 * ─── Layout, confirmed arithmetically ──────────────────────────────────────
 *
 * `operator new(0x118)` at 0x00443219 is ground truth, and the fields tile it
 * exactly: vtable 4 + minLevel 4 + fileName 0x104 + fp 4 + wParam 4 + hwnd 4
 * = 0x118.  Every offset is static_assert'd in gamelog.h.
 *
 * The two variadic writers share a 0x17fc stack frame that also tiles exactly,
 * which is how the buffer sizes below were derived rather than guessed:
 *
 *   +0x0000  0x00c  COPYDATASTRUCT for the WM_COPYDATA mirror
 *   +0x000c  0x080  the "HH:MM:SS" timestamp (FormatTime writes 9 bytes)
 *   +0x008c  0xbb8  the assembled output line          (3000 bytes)
 *   +0x0c44  0xbb8  the vsprintf'd caller message      (3000 bytes)
 *                                                       = 0x17fc
 *
 * ─── Preserved defects ─────────────────────────────────────────────────────
 *
 * 1. Both variadic writers vsprintf a caller-controlled format into a fixed
 *    3000-byte stack buffer with no bound.  Reproduced, including the buffer
 *    size, so an overflow lands the way it always did.
 *
 * 2. The fwrite length is strlen(out), but the WM_COPYDATA cbData is
 *    strlen(out) + 1 -- the NUL is counted for the window mirror and not for
 *    the file.  They differ by one in the original; they differ by one here.
 *
 * 3. OpenLogFile formats the banner -- and so samples the date -- BEFORE it
 *    opens the file, so a failed open still burned a GetLocalTime.  Harmless,
 *    kept because reordering it would be a change without a reason.
 *
 * 4. The default fopen mode is "wc": MSVC's commit-on-flush extension, not
 *    standard C.  Passed through verbatim; msvcrt accepts it and the byte
 *    content is identical either way.
 *
 * 5. Logger::Initialize builds and immediately destroys a throwaway logger on
 *    the stack -- a base subobject whose ctor/dtor pair the compiler did not
 *    elide.  It has no effect.  Kept so the shape matches.
 *
 * ─── Deliberate deviations ─────────────────────────────────────────────────
 *
 * LogWithErrorCode's scratch buffer comes from malloc/free here rather than
 * the game's operator new / FactAlloc::Free2.  The buffer is allocated and
 * freed inside the one function and never escapes, so the heap it lives on is
 * unobservable -- and using ours removes two callbacks into the binary.  The
 * SIZE arithmetic (strlen + 0x31) is the original's and is NOT widened.
 *
 * DSErrorToString is a flat switch; the original is a compiled binary search.
 * The mapping is what is observable, and the returned pointers are the game's
 * own .rdata strings either way.
 *
 * ─── Coverage ──────────────────────────────────────────────────────────────
 *
 * Exercised by any run: the ctor, OpenLogFile, LogMessage, both close paths.
 * NOT exercised, and transcribed from the decompile rather than observed:
 *   - the WM_COPYDATA mirror in all three writers.  notifyHwnd is never set --
 *     nothing in the binary writes +0x110 or +0x114 outside the ctor's zeroing,
 *     so this is dead code in the shipped game and is kept for fidelity only.
 *   - the MessageBoxA "Could not open Protofile" failure path.
 *   - most of DSErrorToString's table, beyond what DirectSound actually returns.
 *   - LogSourceLocation and LogWithErrorCode fire only from sound/movie setup.
 *
 * ─── Proof ─────────────────────────────────────────────────────────────────
 *
 * The proof is in the log rather than on screen, because that is where this
 * code's entire effect lives.
 *
 *   KAROO_GAMELOG_FX=mark   prefixes every line this file writes with "KHOOK ".
 *                           Diff JJ.log: every line is marked => every line is
 *                           ours, and none is coming from the original.
 *   KAROO_GAMELOG_FX=off    suppresses the file writes only.  JJ.log keeps its
 *                           banner and gains nothing else -- that proves the
 *                           gate rather than the fill, and distinguishes "our
 *                           writer runs" from "our opener runs".
 *
 * On top of that, verify_format_strings() below compares our format literals
 * against the game's own .rdata at first use and complains to karoo_hooks.log
 * on any mismatch, so a typo in a format string cannot silently reshape the
 * log.
 * ═══════════════════════════════════════════════════════════════════════════ */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stddef.h>
#include "gamelog.h"
#include "log.h"

/* The game's LoggerVtable at 0x0045ef98.  One slot -- the scalar-deleting
 * destructor -- confirmed by reading the four dwords there: 0x00441840 is
 * followed immediately by 0x00442a80, the next class's vtable.
 *
 * Objects we construct keep pointing at the GAME's vtable rather than a clone
 * of our own: patch.py overwrites that single slot to reach us anyway, and an
 * object our ctor builds must compare equal to one the game already holds. */
#define GAME_LOGGER_VTABLE ((void *)0x0045ef98)

/* FactAlloc::Free2 -- __cdecl(void *).  THE one callback into the game binary
 * in this file, and unavoidable: the object being deleted came from the game's
 * operator new at its construction site, so it must go back to that heap.
 * Named here as CLAUDE.md's no-callback rule requires. */
typedef void (__cdecl *free2_fn)(void *);
#define ORIG_FACT_FREE2 ((free2_fn)0x004504c0)

/* The game's format strings, for the first-use audit below. */
#define STR_BANNER  ((const char *)0x00467390)
#define STR_LINE    ((const char *)0x004673fc)
#define STR_SRCLINE ((const char *)0x00467438)
#define STR_ERRLINE ((const char *)0x004675ac)
#define STR_MODE_WC ((const char *)0x004673e0)
#define STR_MB_TEXT ((const char *)0x00467360)
#define STR_MB_CAPT ((const char *)0x0046737c)

/* Our copies.  These are what actually get used; the audit proves them equal
 * to the game's.  Kept as literals rather than read from .rdata because the FX
 * prefix has to splice into them. */
#define FMT_BANNER  "\n***************** Protokollierung gestartet am : %s ***********************\n"
#define FMT_LINE    "%s : %s\r\n"
#define FMT_SRCLINE "%s : File: %s, Line: %d: %s \r\n"
#define FMT_ERRLINE "%s : Error %s: %s \r\n"
#define MODE_WC     "wc"

#define LOG_BUF      3000    /* both 0xbb8 buffers in the 0x17fc frame */
#define WM_COPYDATA_ 0x4a

/* ─── First-use audit ─────────────────────────────────────────────────────── */

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
    verify_one(FMT_BANNER,  STR_BANNER,  "banner");
    verify_one(FMT_LINE,    STR_LINE,    "line");
    verify_one(FMT_SRCLINE, STR_SRCLINE, "srcline");
    verify_one(FMT_ERRLINE, STR_ERRLINE, "errline");
    verify_one(MODE_WC,     STR_MODE_WC, "mode");
}

/* ─── KAROO_GAMELOG_FX ────────────────────────────────────────────────────── */

enum GameLogFx { FX_NONE = 0, FX_MARK, FX_OFF };

static GameLogFx gamelog_fx(void)
{
    static int cached = -1;
    if (cached < 0) {
        char v[32];
        /* By value, never by presence -- GetEnvironmentVariableA returns 0 for
         * empty and unset alike.  See CLAUDE.md, "Read flags by value". */
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

/* ─── Time helpers (the game's FormatTime 0x452259 / date 0x4520c8) ───────── */

/* "HH:MM:SS" + NUL, digit by digit exactly as the original does it. */
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

/* "MM/DD/YY" + NUL -- US order, two-digit year, as shipped. */
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

/* The tail every writer shares: fwrite + fflush, then the window mirror.
 * In the original it is inlined three times, identically. */
static void emit(GameLogger *self, int level, const char *line)
{
    if (self->fp && gamelog_fx() != FX_OFF) {
        fwrite(line, 1, strlen(line), self->fp);
        fflush(self->fp);
    }
    /* Dead in the shipped game -- nothing ever sets these two fields. */
    if (self->notifyHwnd && self->notifyWParam) {
        COPYDATASTRUCT cds;
        cds.dwData = (ULONG_PTR)level;
        cds.cbData = (DWORD)(strlen(line) + 1);   /* defect 2: +1 here only */
        cds.lpData = (PVOID)line;
        SendMessageA(self->notifyHwnd, WM_COPYDATA_, (WPARAM)self->notifyWParam,
                     (LPARAM)&cds);
    }
}

/* ═══ 0x00441a00 -- close the file and (re)bind the vtable ═════════════════ */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
GameLog_CloseAndRebindVtable(GameLogger *self)
{
    self->pVtable = GAME_LOGGER_VTABLE;
    if (self->fp)
        fclose(self->fp);
    /* fp is deliberately NOT cleared: the original leaves it dangling here.
     * The only path that reuses the object afterwards (OpenLogFile) goes
     * through GameLog_CloseLogFile below, which does clear it. */
}

/* ═══ 0x004419c0 -- close the file and clear the sink fields ═══════════════ */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
GameLog_CloseLogFile(GameLogger *self)
{
    if (self->fp)
        fclose(self->fp);
    self->fp           = NULL;
    self->notifyWParam = 0;
    self->notifyHwnd   = NULL;
}

/* ═══ 0x00441810 -- default constructor ════════════════════════════════════ */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
GameLog_Construct(GameLogger *self)
{
    verify_format_strings();
    self->pVtable      = GAME_LOGGER_VTABLE;
    self->minLevel     = 1;      /* so level-0 messages are off by default */
    self->fileName[0]  = '\0';   /* byte 0 only; the rest stays uninitialised */
    self->fp           = NULL;
    self->notifyWParam = 0;
    self->notifyHwnd   = NULL;
}

/* ═══ 0x00441840 -- scalar-deleting destructor (vtable slot 0) ═════════════ */
extern "C" __declspec(dllexport) void * __attribute__((thiscall))
GameLog_ScalarDeletingDtor(GameLogger *self, unsigned char flags)
{
    GameLog_CloseAndRebindVtable(self);
    if (flags & 1)
        ORIG_FACT_FREE2(self);
    return self;
}

/* ═══ 0x004418b0 -- open (or reopen) the log file ══════════════════════════ */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
GameLog_OpenLogFile(GameLogger *self, const char *filename, const char *mode)
{
    char date[16];
    char banner[256];

    verify_format_strings();
    GameLog_CloseLogFile(self);

    if (mode == NULL)
        mode = MODE_WC;            /* defect 4: MSVC's "wc" */

    self->minLevel = 1;

    /* defect 3: the banner is built before the open, not after. */
    format_date(date);
    sprintf(banner, FMT_BANNER, date);

    strcpy(self->fileName, filename);

    self->fp = fopen(self->fileName, mode);
    if (self->fp == NULL) {
        log_write("gamelog: could not open %s (mode %s)\n", self->fileName, mode);
        MessageBoxA(NULL, STR_MB_TEXT, STR_MB_CAPT, 0);
        return 0;
    }

    fwrite(banner, 1, strlen(banner), self->fp);
    fflush(self->fp);
    log_write("gamelog: opened %s (mode %s)\n", self->fileName, mode);
    return 1;
}

/* ═══ 0x00441860 -- construct and open in one ══════════════════════════════ */
extern "C" __declspec(dllexport) void * __attribute__((thiscall))
GameLog_Initialize(GameLogger *self, const char *filename, const char *mode)
{
    GameLogger scratch;

    self->pVtable = GAME_LOGGER_VTABLE;

    /* defect 5: the throwaway stack logger, built and torn down for nothing. */
    GameLog_Construct(&scratch);
    GameLog_CloseAndRebindVtable(&scratch);

    if (GameLog_OpenLogFile(self, filename, mode) == 0)
        GameLog_CloseLogFile(self);

    return self;
}

/* ═══ 0x00441b10 -- the main variadic writer ═══════════════════════════════ */
extern "C" __declspec(dllexport) void __cdecl
GameLog_LogMessage(GameLogger *self, int level, const char *fmt, ...)
{
    char timebuf[0x80];
    char msg[LOG_BUF];
    char out[LOG_BUF];
    va_list ap;

    if (level < self->minLevel)
        return;

    format_time(timebuf);

    va_start(ap, fmt);
    vsprintf(msg, fmt, ap);          /* defect 1: unbounded, as shipped */
    va_end(ap);

    sprintf(out, "%s" FMT_LINE, fx_prefix(), timebuf, msg);
    emit(self, level, out);
}

/* ═══ 0x00441d20 -- the file/line variadic writer ══════════════════════════
 * Argument order was taken from the disassembly, not from the decompiler's
 * reconstruction, which mislabels this frame: the sprintf pushes at
 * 0x00441d80..0x00441d8f give (time, arg3, arg4, msg) against
 * "%s : File: %s, Line: %d: %s \r\n", so arg3 is the file and arg4 the line. */
extern "C" __declspec(dllexport) void __cdecl
GameLog_LogSourceLocation(GameLogger *self, int level, const char *file,
                          int line, const char *fmt, ...)
{
    char timebuf[0x80];
    char msg[LOG_BUF];
    char out[LOG_BUF];
    va_list ap;

    if (level < self->minLevel)
        return;

    format_time(timebuf);

    va_start(ap, fmt);
    vsprintf(msg, fmt, ap);          /* defect 1 again */
    va_end(ap);

    sprintf(out, "%s" FMT_SRCLINE, fx_prefix(), timebuf, file, line, msg);
    emit(self, level, out);
}

/* ═══ 0x00441e30 -- HRESULT -> DirectSound error name ══════════════════════
 * The pointers returned are the game's own .rdata strings, so the bytes that
 * reach the log are identical to the original's regardless of the search
 * shape.  Codes are the decompile's negative constants, written here as the
 * DSERR values they are. */
extern "C" __declspec(dllexport) const char * __cdecl
GameLog_DSErrorToString(HRESULT hr)
{
    switch ((unsigned)hr) {
    case 0x80004001u: return (const char *)0x00467584; /* DSERR_UNSUPPORTED        */
    case 0x80004002u: return (const char *)0x00467598; /* DSERR_NOINTERFACE        */
    case 0x80004005u: return (const char *)0x00467574; /* DSERR_GENERIC            */
    case 0x80040110u: return (const char *)0x00467560; /* DSERR_NOAGGREGATION      */
    case 0x8007000Eu: return (const char *)0x0046754c; /* DSERR_OUTOFMEMORY        */
    case 0x80070057u: return (const char *)0x00467510; /* DSERR_INVALIDPARAM       */
    case 0x8878000Au: return (const char *)0x00467524; /* DSERR_ALLOCATED          */
    case 0x8878001Eu: return (const char *)0x00467534; /* DSERR_CONTROLUNAVAIL     */
    case 0x88780032u: return (const char *)0x004674fc; /* DSERR_INVALIDCALL        */
    case 0x88780046u: return (const char *)0x004674e4; /* DSERR_PRIOLEVELNEEDED    */
    case 0x88780064u: return (const char *)0x004674d4; /* DSERR_BADFORMAT          */
    case 0x88780078u: return (const char *)0x004674c4; /* DSERR_NODRIVER           */
    case 0x88780082u: return (const char *)0x004674a8; /* DSERR_ALREADYINITIALIZED */
    case 0x88780096u: return (const char *)0x00467494; /* DSERR_BUFFERLOST         */
    case 0x887800A0u: return (const char *)0x0046747c; /* DSERR_OTHERAPPHASPRIO    */
    case 0x887800AAu: return (const char *)0x00467468; /* DSERR_UNINITIALIZED      */
    default:          return (const char *)0x00467458; /* "Unknown HRESULT"        */
    }
}

/* ═══ 0x00441f90 -- log a message with a DirectSound error code ════════════
 * Returns its `level` argument.  That is not a designed return value: the
 * original's EAX simply still holds the parameter (it is reused as the scratch
 * for FactAlloc::Free2's result).  No caller reads it.  Reproduced anyway. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
GameLog_LogWithErrorCode(GameLogger *self, int level, const char *message,
                         HRESULT hr)
{
    char timebuf[0x80];
    const char *err;
    char *out;

    if (level < self->minLevel)
        return level;

    format_time(timebuf);

    /* The original sizes this strlen(message) + 0x31 -- room for the
     * timestamp, " : Error ", the code name and CRLF.  A long DSERR name plus
     * a long message can still overrun it; that is the original's arithmetic
     * and it is reproduced rather than widened.  The FX prefix is added on top
     * so that the diagnostic mode cannot be what overflows it. */
    out = (char *)malloc(strlen(message) + 0x31 + strlen(fx_prefix()));
    if (out == NULL)
        return level;

    err = GameLog_DSErrorToString(hr);
    sprintf(out, "%s" FMT_ERRLINE, fx_prefix(), timebuf, err, message);

    emit(self, level, out);

    free(out);
    return level;
}
