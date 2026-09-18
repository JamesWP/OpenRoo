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

/* 0x00469cf8 was GG_LOG_STREAM, "the static CRT's log stream".  It is
 * `stderr`, and naming it settled CRT_PLAN.md Stage B outright.
 *
 * The CRT's `_ioinit` (0x00451524) fills `__piob[i] = &_iob[i]` by walking
 * EAX from 0x00469cb8 in steps of 0x20 while EAX < 0x00469f38 -- so _iob is
 * 20 entries of 32 bytes based at 0x00469cb8, and 0x00469cf8 is entry
 * **2**.  The static initialisers agree: entries 0/1/2 carry _file = 0/1/2
 * with _flag 0x101 (_IOREAD) on the first and 0x2 (_IOWRT) on the other
 * two.  Two independent reads, which is why this is stated and not guessed.
 *
 * (The walk's end sentinel, 0x00469f38, is the CRT rand seed from
 * crtrand.h: _iob ends exactly where the seed begins.)
 *
 * The consequence is that the old comment here was wrong in the way that
 * mattered.  "No other fwrite can write to this stream" is false: stderr is
 * not a shared object needing the game's CRT to touch it, it is fd 2.  Our
 * CRT's `stderr` reaches the same OS handle, so our log writers just call
 * our own fwrite and hand nothing across the boundary.  What the old
 * comment got right -- and what still holds -- is the *shape*: 0x004513c7
 * is fwrite, called as fwrite(msg, strlen(msg), 1, stream), settled by the
 * call sites that are not log messages at all (SaveConfig 0x41d490 writes a
 * 0x144e-byte blob; the save-slot and high-score writers go a byte at a
 * time).  Those save-file callers are Stage D and still use GC_FWRITE. */

/* The CD audio device (`CdAudioGlobal`); cdm.cpp owns its methods. */
static CDM *const GG_CDAUDIO = (CDM *)0x004dc640;

/* The programmable-control singleton (`ProgableControlGlobal`); see
 * progctrl.h. */
static ProgableControl *const GG_PROGCTRL = (ProgableControl *)0x0046c298;

#endif /* KAROO_GAMEGLOBALS_H */
