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
    /* CDM::DestructAndFree 0x402c40 -- vtable slot 0. */
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

/* CDM's vtable: THREE slots at 0x45D2B8, verified from the raw bytes —
     +0x00  0x402C40  scalar deleting dtor  (VTABLE_PATCHES, file off 0x5D2B8)
     +0x04  0x402F50  GetTrackCount
     +0x08  0x402FA0  TrackLength
   The dword at 0x45D2C4 is NOT slot 3 and CDM has no base class: it is the
   start of CdThemes' own one-slot table (cdthemes.h), which its ctor 0x403000
   stores.  Read the ctor's store, not the table's shape — reading the shape
   is how this became "a base-class vtable swapped in during teardown" in
   HOOKS.md, which it never was. */
static const void *const CDM_VTABLE = reinterpret_cast<const void*>(0x45D2B8);

/* Exports of cdm.cpp other files call (COHESION_PLAN.md template 10). */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
CDM_GetTrackCount(CDM *self);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
CDM_GetTrackLength(CDM *self, char **out_ptr, int track);
/* GetMixerDetails returns 0 on any mixer error -- see cdm.cpp. */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
CDM_GetMixerDetails(CDM *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CDM_SetMixerVolume(CDM *self, DWORD level);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CDM_StopTrack(CDM *self);
extern "C" __declspec(dllexport) CDM * __attribute__((thiscall))
CDM_ScalarDeletingDtor(CDM *self, unsigned int flags);
