#pragma once
#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include <stddef.h>

/* WaveInfo passed to PrepareStreamBuffer by the game's script player (Ghidra
 * struct WaveInfo, 0x12 bytes).  The one it passes is embedded in the
 * ScriptPlayer at +0x90f: "initwave" writes the file name, the fixed-sound
 * setup the rest.  Packed, since the next ScriptPlayer field is at +0x921. */
struct __attribute__((packed)) WaveInfo {
    IDirectSound  *pDirectsound;  // +0x00
    DWORD          dwFlags;       // +0x04
    char          *pFilename;     // +0x08
    int            nBuffer_seconds; // +0x0c
    short          wSegment_count;  // +0x10
};
static_assert(offsetof(WaveInfo, dwFlags) == 0x04, "WaveInfo dwFlags");
static_assert(offsetof(WaveInfo, pFilename) == 0x08, "WaveInfo pFilename");
static_assert(offsetof(WaveInfo, nBuffer_seconds) == 0x0c, "WaveInfo nBuffer_seconds");
static_assert(offsetof(WaveInfo, wSegment_count) == 0x10, "WaveInfo wSegment_count");
static_assert(sizeof(WaveInfo) == 0x12, "WaveInfo size");

/*
 * CStreamSoundbuffer — 212-byte (0xD4) packed struct.
 *
 * Hard constraints (read by code outside this class):
 *   dwThread_done @ 0xa6 — 0=playing, 1=finished/idle
 *     Read by FUN_0041d920 (per-frame script updater) and TeardownScriptPlayer.
 *
 * The game allocates this object (operator_new(0xD4)) so we must not exceed 0xD4
 * and must keep the vtable pointer at offset 0.
 */
#pragma pack(push, 1)
struct CStreamSoundbuffer {
    void                *vtable;          // +0x00
    char                *filename;        // +0x04
    IDirectSoundBuffer  *pSoundbuffer;    // +0x08
    IDirectSound        *pDirectsound;    // +0x0c
    BYTE                 _pad0[0x0a];     // +0x10 .. +0x19
    DWORD                dwBuffer_size;   // +0x1a
    BYTE                 _pad1[0x88];     // +0x1e .. +0xa5
    volatile DWORD       dwThread_done;   // +0xa6  — 0=playing, 1=idle
    HANDLE               watcher_thread; // +0xaa
    HANDLE               stop_event;     // +0xae
    BYTE                 _pad2[0x08];    // +0xb2 .. +0xb9
    CRITICAL_SECTION     cs;             // +0xba  (24 bytes = 0x18)
    BYTE                 _pad3[0x02];    // +0xd2 .. +0xd3
};
#pragma pack(pop)

static_assert(offsetof(CStreamSoundbuffer, vtable)       == 0x00,  "vtable offset");
static_assert(offsetof(CStreamSoundbuffer, pSoundbuffer) == 0x08,  "pSoundbuffer offset");
static_assert(offsetof(CStreamSoundbuffer, dwBuffer_size)== 0x1a,  "dwBuffer_size offset");
static_assert(offsetof(CStreamSoundbuffer, dwThread_done)== 0xa6,  "dwThread_done offset");
static_assert(offsetof(CStreamSoundbuffer, cs)           == 0xba,  "cs offset");
static_assert(sizeof(CStreamSoundbuffer)                 == 0xD4,  "CStreamSoundbuffer size");

/* The vtable is ours (ENDGAME_PLAN.md, "The vtable address of our objects may
 * be our own").  The game's table at 0x45efa4 has one slot, ScalarDeletingDtor
 * 0x443da0, and that slot is now ours.  The game's table is left pointing at
 * the UD2 stub, so a reader we failed to find faults rather than quietly
 * working. */
extern "C" __declspec(dllexport) void *CStream_Vtable(void);

/* 0x00443da0 vtable slot 0: MSVC's scalar deleting destructor -- the dtor
 * body, then free when bit 0 of `flags` is set.  Returns `this`. */
extern "C" __declspec(dllexport) void * __attribute__((thiscall))
CStream_ScalarDeletingDtor(CStreamSoundbuffer *self, unsigned int flags);

/* 0x00443dc0 -- the dtor body, called by the slot above. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CStream_DeinitInstance(CStreamSoundbuffer *self);

/* Exports of stream.cpp other files call (COHESION_PLAN.md template 10). */
extern "C" __declspec(dllexport) CStreamSoundbuffer * __attribute__((thiscall))
CStream_Initialize(CStreamSoundbuffer *self);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
CStream_Prepare(CStreamSoundbuffer *self, WaveInfo *wi);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CStream_Play(CStreamSoundbuffer *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CStream_Stop(CStreamSoundbuffer *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CStream_ReleaseResources(CStreamSoundbuffer *self);
