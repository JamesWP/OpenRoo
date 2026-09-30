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
 * vptr is at offset 0. */
class __attribute__((packed)) CStreamSoundbuffer {
public:
    /* An idle stream: every field cleared, the lock created, marked done. */
    CStreamSoundbuffer();
    /* Releases everything, as releaseResources does, and deletes the lock. */
    virtual ~CStreamSoundbuffer();
    CStreamSoundbuffer(const CStreamSoundbuffer &) = delete;
    CStreamSoundbuffer &operator=(const CStreamSoundbuffer &) = delete;

    /* Releases any earlier file, then loads wi's WAV file into a new buffer.
     * Returns nonzero on success. */
    int prepare(WaveInfo *wi);

    /* Plays from the start and starts the watcher thread. */
    void play();

    /* Stops playback and the watcher. */
    void stop();

    /* Stops, then releases the buffer, the stop event and the file name. */
    void releaseResources();

    char                *filename() const { return filename_; }
    IDirectSoundBuffer  *soundbuffer() const { return pSoundbuffer_; }
    IDirectSound        *directsound() const { return pDirectsound_; }
    // 0 playing, 1 finished or idle; the script player polls it.
    DWORD                thread_done() const { return dwThread_done_; }

private:
 
    static DWORD WINAPI watcherProc(LPVOID param);

    char                *filename_;
    IDirectSoundBuffer  *pSoundbuffer_;
    IDirectSound        *pDirectsound_;
    DWORD                dwBuffer_size_;
    volatile DWORD       dwThread_done_;  // 0 playing, 1 finished or idle; the script player polls it
    HANDLE               watcher_thread_;
    HANDLE               stop_event_;
    CRITICAL_SECTION     cs_;
};
 

