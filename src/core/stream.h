/* The streamed sound buffer (stream.cpp): one WAV file played once through a
 * DirectSound buffer, for the instruction-script player.  A watcher thread
 * marks the buffer done when playback ends. */

#pragma once
#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include <stddef.h>

/* What the script player hands to CStream_Prepare.  Its copy is embedded in
 * the ScriptPlayer: "initwave" writes the file name, the fixed-sound setup the
 * rest.  Packed, because the ScriptPlayer field after it follows directly. */
struct __attribute__((packed)) WaveInfo {
    IDirectSound  *pDirectsound;
    DWORD          dwFlags;
    char          *pFilename;
    int            nBuffer_seconds;
    short          wSegment_count;
};
static_assert(offsetof(WaveInfo, dwFlags) == 0x04, "WaveInfo dwFlags");
static_assert(offsetof(WaveInfo, pFilename) == 0x08, "WaveInfo pFilename");
static_assert(offsetof(WaveInfo, nBuffer_seconds) == 0x0c, "WaveInfo nBuffer_seconds");
static_assert(offsetof(WaveInfo, wSegment_count) == 0x10, "WaveInfo wSegment_count");
static_assert(sizeof(WaveInfo) == 0x12, "WaveInfo size");

/* A streamed buffer, 0xd4 bytes, the size the script player allocates.  The
 * vtable must stay at offset 0. */
#pragma pack(push, 1)
struct CStreamSoundbuffer {
    void                *vtable;
    char                *filename;
    IDirectSoundBuffer  *pSoundbuffer;
    IDirectSound        *pDirectsound;
    BYTE                 _pad0[0x0a];
    DWORD                dwBuffer_size;
    BYTE                 _pad1[0x88];
    volatile DWORD       dwThread_done;  // 0 playing, 1 finished or idle; the script player polls it
    HANDLE               watcher_thread;
    HANDLE               stop_event;
    BYTE                 _pad2[0x08];
    CRITICAL_SECTION     cs;  // 0x18 bytes
    BYTE                 _pad3[0x02];
};
#pragma pack(pop)

static_assert(offsetof(CStreamSoundbuffer, vtable)       == 0x00,  "vtable offset");
static_assert(offsetof(CStreamSoundbuffer, pSoundbuffer) == 0x08,  "pSoundbuffer offset");
static_assert(offsetof(CStreamSoundbuffer, dwBuffer_size)== 0x1a,  "dwBuffer_size offset");
static_assert(offsetof(CStreamSoundbuffer, dwThread_done)== 0xa6,  "dwThread_done offset");
static_assert(offsetof(CStreamSoundbuffer, cs)           == 0xba,  "cs offset");
static_assert(sizeof(CStreamSoundbuffer)                 == 0xD4,  "CStreamSoundbuffer size");

/* The one-slot vtable: the scalar deleting destructor. */
void *CStream_Vtable(void);

/* The deinit, then free() when bit 0 of flags is set; returns self. */
void *CStream_ScalarDeletingDtor(CStreamSoundbuffer *self, unsigned int flags);

/* Releases everything, as CStream_ReleaseResources, and deletes the lock. */
void CStream_DeinitInstance(CStreamSoundbuffer *self);

/* Zeroes the object, sets its vtable and lock, and marks it done; returns
 * self. */
CStreamSoundbuffer *CStream_Initialize(CStreamSoundbuffer *self);

/* Releases any earlier file, then loads wi's WAV file into a new buffer.
 * Returns nonzero on success. */
int CStream_Prepare(CStreamSoundbuffer *self, WaveInfo *wi);

/* Plays from the start and starts the watcher thread. */
void CStream_Play(CStreamSoundbuffer *self);

/* Stops playback and the watcher. */
void CStream_Stop(CStreamSoundbuffer *self);

/* Stops, then releases the buffer, the stop event and the file name. */
void CStream_ReleaseResources(CStreamSoundbuffer *self);
