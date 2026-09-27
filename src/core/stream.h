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

/* A streamed buffer, 0xd4 bytes, the size the script player allocates.  The
 * vtable must stay at offset 0. */
class __attribute__((packed)) CStreamSoundbuffer {
public:
    /* The deinit, then free() when bit 0 of flags is set; returns self. */
    static void * 
    scalarDeletingDtor(CStreamSoundbuffer *self, unsigned int flags);

    /* Releases everything, as releaseResources does, and deletes the lock. */
    void deinitInstance();

    /* Zeroes the object, sets its vtable and lock, and marks it done; returns
     * self. */
    CStreamSoundbuffer *initialize();

    /* Releases any earlier file, then loads wi's WAV file into a new buffer.
     * Returns nonzero on success. */
    int prepare(WaveInfo *wi);

    /* Plays from the start and starts the watcher thread. */
    void play();

    /* Stops playback and the watcher. */
    void stop();

    /* Stops, then releases the buffer, the stop event and the file name. */
    void releaseResources();

    void                *vtable() const { return vtable_; }
    char                *filename() const { return filename_; }
    IDirectSoundBuffer  *soundbuffer() const { return pSoundbuffer_; }
    IDirectSound        *directsound() const { return pDirectsound_; }
    // 0 playing, 1 finished or idle; the script player polls it.
    DWORD                thread_done() const { return dwThread_done_; }

private:
 
    static DWORD WINAPI watcherProc(LPVOID param);

    void                *vtable_;
    char                *filename_;
    IDirectSoundBuffer  *pSoundbuffer_;
    IDirectSound        *pDirectsound_;
    DWORD                dwBuffer_size_;
    volatile DWORD       dwThread_done_;  // 0 playing, 1 finished or idle; the script player polls it
    HANDLE               watcher_thread_;
    HANDLE               stop_event_;
    CRITICAL_SECTION     cs_;
};
 
/* The one-slot vtable: the scalar deleting destructor. */
  void *CStream_Vtable(void);

