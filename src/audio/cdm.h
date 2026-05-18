#pragma once
#include <windows.h>
#include <stddef.h>

/* CDM object layout (from disassembly) */
struct CDM {
    void   *vtable;                         // +0x00
    int     track_count;                    // +0x04
    struct  Track { int start; int length; }
            tracks[10];                     // +0x08..+0x57 (10 × 8 bytes)
    DWORD   notify_hwnd;                    // +0x58 — game window HWND for MM_MCINOTIFY
    char    mci_buf[256];                   // +0x5C
    BYTE    track_to;                       // +0x15C
    BYTE    current_track;                  // +0x15D — range 1-9

    CDM*  construct();
    void  destruct();
    int   getTrackCount();
    int   getTrackLength(char **out_ptr, int track);
    void  playTrack(int from, int to);
    void  stopTrack();
    void  setMixerVolume(DWORD level);
};

static_assert(offsetof(CDM, notify_hwnd)   == 0x58,  "CDM layout mismatch");
static_assert(offsetof(CDM, mci_buf)       == 0x5C,  "CDM layout mismatch");
static_assert(offsetof(CDM, track_to)      == 0x15C, "CDM layout mismatch");
static_assert(offsetof(CDM, current_track) == 0x15D, "CDM layout mismatch");

static const void *CDM_VTABLE = reinterpret_cast<const void*>(0x45D2B8);
