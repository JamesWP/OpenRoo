#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <stddef.h>

struct CDVolumeControl {
    HMIXEROBJ hmixer;
    DWORD     dwVolumeControlID;
};

/* CDM object layout (from Ghidra / disassembly) */
struct CDM {
    void            *vtable;        // +0x00
    DWORD            nummixers;     // +0x04
    CDVolumeControl  mixers[10];    // +0x08..+0x57 (10 × 8 bytes)
    HWND             windowhandle;  // +0x58
    char             mcibuff[256];  // +0x5C
    bool             repeat;        // +0x15C
    int              tracknumber;   // +0x15D (unaligned int; game-allocated, x86 handles it)

    CDM*  construct();
    void  stopAndClose();
    void  setWindowHandle(HWND hwnd);
    int   getTrackCount();
    int   getTrackLength(char **out_ptr, int track);
    void  playTrack(int tracknumber, bool repeat);
    void  stop();
    void  setMixerVolume(DWORD level);
} __attribute__((packed));

static_assert(offsetof(CDM, windowhandle) == 0x58,  "CDM layout mismatch");
static_assert(offsetof(CDM, mcibuff)      == 0x5C,  "CDM layout mismatch");
static_assert(offsetof(CDM, repeat)       == 0x15C, "CDM layout mismatch");
static_assert(offsetof(CDM, tracknumber)  == 0x15D, "CDM layout mismatch");

static const void *const CDM_VTABLE = reinterpret_cast<const void*>(0x45D2B8);

/* Exports of cdm.cpp other files call (COHESION_PLAN.md template 10). */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
CDM_GetTrackCount(CDM *self);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
CDM_GetTrackLength(CDM *self, char **out_ptr, int track);
