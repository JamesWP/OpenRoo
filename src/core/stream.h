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
 * rest. */
struct WaveInfo {
    IDirectSound  *pDirectsound;
    DWORD          dwFlags;
    char          *pFilename;
    int            nBuffer_seconds;
    short          wSegment_count;
};

/* A streamed buffer.  The vtable must stay at offset 0. */
struct CStreamSoundbuffer {
    void                *vtable;
    char                *filename;
    IDirectSoundBuffer  *pSoundbuffer;
    IDirectSound        *pDirectsound;
    DWORD                dwBuffer_size;
    volatile DWORD       dwThread_done;  // 0 playing, 1 finished or idle; the script player polls it
    HANDLE               watcher_thread;
    HANDLE               stop_event;
    CRITICAL_SECTION     cs;
};

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
