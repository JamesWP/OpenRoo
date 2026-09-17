/* gameglobals.h -- the game's well-known singletons, named once.
 *
 * gamestr.h found that a *string* address re-#defined per file drifts into
 * two names.  Every other well-known address has the same disease, and it
 * had never been counted.  Before this header:
 *
 *   0x0046c4c0  the game logger        11 definitions, 3 names
 *                                      (GAMELOGGER x6, GAME_LOGGER x3,
 *                                       GAME_LOGGER_VA x2)
 *   0x00469cf8  the CRT log stream      4 definitions, 2 names, 2 types
 *                                      (GAME_LOG_FILE `int *` x3,
 *                                       G_LOGSTREAM `void *` x1)
 *   0x004dc640  the CD device           4 definitions, 1 name (CDAUDIO)
 *   0x0046c298  the progammable control 2 definitions, 1 name (PROGCTRL)
 *
 * One name per address, the provenance beside it, and typed constants
 * rather than macros so a misuse is a type error.  The types are declared
 * incomplete here on purpose: a file that only *passes* a singleton needs
 * no layout, and one that dereferences it includes the owning header
 * (gamelog.h, cdm.h, progctrl.h) as it already did.
 *
 * Not in scope, and deliberately: the `ORIG_*` function pointers and the
 * `IID_*` constants.  Those are per-file by nature -- an original the file
 * still calls is part of that file's story -- and Band 7c already ruled on
 * the ones whose names lied.
 *
 * COHESION_PLAN.md Band 8a.  Addresses only -- no expression and no
 * behaviour change, so patch.py and Karoo.exe are untouched.
 */
#ifndef KAROO_GAMEGLOBALS_H
#define KAROO_GAMEGLOBALS_H

#include <stdio.h>

struct GameLogger;
struct CDM;
struct ProgableControl;

/* The game's own logger instance ("CProto"), constructed at startup and
 * never replaced.  Written through GameLog_LogMessage /
 * GameLog_LogSourceLocation (gamelog.h), which are ours. */
static GameLogger *const GG_LOGGER = (GameLogger *)0x0046c4c0;

/* The static CRT's log stream -- a FILE object in the game's own CRT data,
 * not a pointer to one, which is why it is passed straight to fwrite.
 *
 * It was called a "sink" and typed `int *`/`void *` because the function
 * that takes it, 0x004513c7, was once read as `ImageLogger::Log`.  It is
 * the statically linked CRT `fwrite`: the texture loaders log by calling
 * fwrite(msg, strlen(msg), 1, stream), so the "(message, len, 1, sink)"
 * shape reconstructed from those two call sites was fwrite's own
 * (buffer, size, count, stream) with size=strlen and count=1.  Settled in
 * Ghidra 2026-09-04 (ASSET_PLAN.md Phase 2) by the call sites that are not
 * log messages at all: SaveConfig (0x41d490) calls it as
 * fwrite(blob, 0x144e, 1, fp), and the save-slot and high-score writers
 * call it a byte at a time.  The four dwords the decompile indexes are
 * _iobuf's: [3] _flag, [4] _file, [6] _bufsiz.
 *
 * No other fwrite can write to this stream -- it belongs to the game's CRT
 * copy, not ours -- which is why the calls to 0x004513c7 stay callbacks. */
static FILE *const GG_LOG_STREAM = (FILE *)0x00469cf8;

/* The CD audio device (`CdAudioGlobal`); cdm.cpp owns its methods. */
static CDM *const GG_CDAUDIO = (CDM *)0x004dc640;

/* The programmable-control singleton (`ProgableControlGlobal`); see
 * progctrl.h. */
static ProgableControl *const GG_PROGCTRL = (ProgableControl *)0x0046c298;

#endif /* KAROO_GAMEGLOBALS_H */
