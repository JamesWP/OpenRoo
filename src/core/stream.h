#pragma once
#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include <stddef.h>

/* WaveInfo passed to PrepareStreamBuffer by the game's script player. */
struct WaveInfo {
    IDirectSound  *pDirectsound;  // +0x00
    DWORD          dwFlags;       // +0x04
    char          *pFilename;     // +0x08
    int            nBuffer_seconds; // +0x0c
    short          wSegment_count;  // +0x10
};
static_assert(offsetof(WaveInfo, pFilename) == 0x08, "WaveInfo pFilename");

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

static const void *STREAM_VTABLE = reinterpret_cast<const void*>(0x45efa4);
