/* ScriptPlayer -- the instruction-script (.jjs) player embedded in Game at
 * +0x195735 (COHESION_PLAN.md Band 3, the fourth sub-object).
 *
 * It holds a level's script -- up to 1000 lines of 1000 bytes at +0x11b4 --
 * and plays it: the flythrough camera (a spline, an eye and a distance the
 * checkpoint restore copies into Game) and the script's streamed sounds.
 *
 * 0xf53f4 bytes: the line table is the last field, and 0x11b4 + 1000*1000
 * ends exactly at Game's cameraDistance (+0x28ab29).  KAROO_LAYOUT_SIZE
 * asserts it.
 *
 *   BuildScriptPlayer          0x41d5e0  ctor: vtable 0x45d418, the embedded
 *                                        stream (+0x83b), the SplinePath
 *                                        (+0x93d), zeroes cursor, splineActive,
 *                                        soundManager and the 255 stream slots
 *   TeardownScriptPlayer       0x41d680  dtor
 *   ReadInstructionScriptForLevel 0x41d720  ours: readForLevel       (scriptplayer.cpp)
 *   ReadInstructionScriptTexts 0x41e8b0  ours: readTextsForReport (scriptplayer.cpp)
 *   FUN_0041d920               the per-frame tick -- PLACEHOLDER, tick()
 *   ReleaseScriptStreamBuffers 0x41e840  ours: releaseStreams     (scriptplayer.cpp)
 *
 * The ctor and dtor are still the game's, run by Game's Load/Destruct.
 * Field meanings come from those functions' decompiles and from the Game
 * code that reads them (checkpoint.cpp, gametick.cpp); fields only cleared
 * are field_<offset>.
 */
#pragma once

#include <stdio.h>
#include "layout.h"
#include "stream.h"     /* WaveInfo, CStreamSoundbuffer */

class SoundManager;

class __attribute__((packed)) ScriptPlayer {
public:
    static const int ORIGIN = 0;

    enum { LINE_SIZE = 1000, LINE_MAX = 1000 };

    /* Parse <path>.jjs into the line table. */
    int  readForLevel(const char *path);
    /* The level report's reader: writes the script's texts to `sink` and
     * counts spline lines and text blocks. */
    int  readTextsForReport(const char *path, FILE *sink);

    /* PLACEHOLDER: FUN_0041d920, __thiscall, the per-frame tick and the
     * entry into the whole interpreter (PlayScript, the camera spline).
     * (now, dt) are two doubles by value, the 16 stack bytes the original
     * pushes as four dwords. */
    void tick(double now, double dt)
    {
        typedef void (__attribute__((thiscall)) *fn)(ScriptPlayer *, double, double);
        ((fn)0x0041d920)(this, now, dt);
    }
    /* ReleaseScriptStreamBuffers 0x0041e840: stops,
     * releases and deletes each of the 255 stream slots. */
    void releaseStreams();

    /* Set to 1 by the reader once a script has loaded; OpenLevelFile
     * zeroes it before the read.  Every gate on "this level has a script"
     * reads it. */
    int            loaded() const                  { return loaded_; }
    void           setLoaded(int l)                { loaded_ = l; }
    /* The script is running: set when a level is loaded (the flythrough is
     * armed), cleared by ENTER and by the tick once the last line has been
     * fetched.  The checkpoint restore needs it and loaded() both. */
    int            running() const                 { return running_; }
    void           setRunning(int r)               { running_ = r; }
    /* The camera spline is being evaluated (the tick drives it while this
     * and cameraMode() are set). */
    void           setSplineActive(int a)          { splineActive_ = a; }

    /* What the checkpoint restore copies into Game: the camera mode, the
     * distance, the eye and the point the spline last produced. */
    unsigned char  cameraMode() const              { return cameraMode_; }
    float          cameraDistance() const          { return cameraDistance_; }
    float          eye(int i) const                { return eye_[i]; }
    float          splinePoint(int i) const        { return splinePoint_[i]; }

    /* Lines in the table (a WORD). */
    unsigned short lineCount() const               { return lineCount_; }

    /* The level report's two counters (reportwriter.cpp prints them). */
    unsigned short splineLines() const             { return splineLines_; }
    void           setSplineLines(unsigned short n) { splineLines_ = n; }
    unsigned short textBlocks() const              { return textBlocks_; }
    void           setTextBlocks(unsigned short n) { textBlocks_ = n; }

    /* The SoundManager the script's sounds play through (the fixed-sound
     * setup stores it). */
    void           setSoundManager(SoundManager *sm) { soundManager_ = sm; }
    /* The WaveInfo every "initwave" stream is prepared from; the fixed-sound
     * setup fills all but the file name. */
    WaveInfo      *streamWave()                    { return &streamWave_; }

private:
    ScriptPlayer() = delete;   /* game-owned; only ever reached by pointer */
    KAROO_LAYOUT_REGISTER(ScriptPlayer);

    const void    *vtable_;                        /* +0x000  0x45d418 */
    unsigned short splineLines_;                   /* +0x004 */
    unsigned short textBlocks_;                    /* +0x006 */
    unsigned short againLine_;                     /* +0x008  "fromhere" sets cursor+1; "again" jumps back */
    unsigned char  gap_00a[0x40a - 0x00a];
    SoundManager  *soundManager_;                  /* +0x40a */
    unsigned char  waitStream_;                    /* +0x40e  the stream waitingOnStream_ waits on */
    CStreamSoundbuffer *streams_[255];             /* +0x40f */
    unsigned char  gap_80b[0x833 - 0x80b];
    unsigned int   waitingOnStream_;               /* +0x833  "playwave <id> <wait>"; the tick stalls until its thread is done */
    unsigned int   streamReady_;                   /* +0x837  "initwave"'s PrepareStreamBuffer result */
    unsigned char  gap_83b[0x90f - 0x83b];         /* the embedded stream, 0xd4 */
    WaveInfo       streamWave_;                    /* +0x90f */
    unsigned char  gap_921[0x92d - 0x921];
    unsigned int   field_92d_;                     /* +0x92d */
    float          splinePoint_[3];                /* +0x931 */
    unsigned char  gap_93d[0x951 - 0x93d];         /* the SplinePath */
    int            splineActive_;                  /* +0x951 */
    unsigned int   field_955_;                     /* +0x955 */
    unsigned char  gap_959[0x9a5 - 0x959];
    unsigned int   moving_;                        /* +0x9a5  "movetoxyz" in progress */
    unsigned char  gap_9a9[0x9b1 - 0x9a9];
    int            loaded_;                        /* +0x9b1 */
    float          cameraDistance_;                /* +0x9b5 */
    char           currentLine_[LINE_SIZE];        /* +0x9b9 */
    float          eye_[3];                        /* +0xda1 */
    unsigned char  cameraMode_;                    /* +0xdad */
    int            running_;                       /* +0xdae */
    unsigned char  gap_db2[0xdba - 0xdb2];
    unsigned int   waiting_;                       /* +0xdba  a timed wait in progress */
    unsigned char  gap_dbe[0xdc6 - 0xdbe];
    unsigned short cursor_;                        /* +0xdc6 */
    unsigned short lineCount_;                     /* +0xdc8 */
    char           scratch_[0x11b4 - 0xdca];       /* +0xdca */
    char           lines_[LINE_MAX][LINE_SIZE];    /* +0x11b4 */
};

KAROO_LAYOUT_CHECKS(ScriptPlayer)
{
    KAROO_LAYOUT_AT(splineLines_,    0x004);
    KAROO_LAYOUT_AT(textBlocks_,     0x006);
    KAROO_LAYOUT_AT(againLine_,      0x008);
    KAROO_LAYOUT_AT(soundManager_,   0x40a);
    KAROO_LAYOUT_AT(waitStream_,     0x40e);
    KAROO_LAYOUT_AT(streams_,        0x40f);
    KAROO_LAYOUT_AT(waitingOnStream_, 0x833);
    KAROO_LAYOUT_AT(streamReady_,    0x837);
    KAROO_LAYOUT_AT(streamWave_,     0x90f);
    KAROO_LAYOUT_AT(field_92d_,      0x92d);
    KAROO_LAYOUT_AT(splinePoint_,    0x931);
    KAROO_LAYOUT_AT(splineActive_,   0x951);
    KAROO_LAYOUT_AT(field_955_,      0x955);
    KAROO_LAYOUT_AT(moving_,         0x9a5);
    KAROO_LAYOUT_AT(loaded_,         0x9b1);
    KAROO_LAYOUT_AT(cameraDistance_, 0x9b5);
    KAROO_LAYOUT_AT(currentLine_,    0x9b9);
    KAROO_LAYOUT_AT(eye_,            0xda1);
    KAROO_LAYOUT_AT(cameraMode_,     0xdad);
    KAROO_LAYOUT_AT(running_,        0xdae);
    KAROO_LAYOUT_AT(waiting_,        0xdba);
    KAROO_LAYOUT_AT(cursor_,         0xdc6);
    KAROO_LAYOUT_AT(lineCount_,      0xdc8);
    KAROO_LAYOUT_AT(scratch_,        0xdca);
    KAROO_LAYOUT_AT(lines_,          0x11b4);
    KAROO_LAYOUT_SIZE(0xf53f4);
}

/* The exports patch.py binds; shims onto the methods. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
JJScript_ReadForLevel(ScriptPlayer *self, const char *path);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
JJScript_ReleaseScriptStreamBuffers(ScriptPlayer *self);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
JJScript_ReadTextsForReport(ScriptPlayer *self, const char *path, FILE *sink);
