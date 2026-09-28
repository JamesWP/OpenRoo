/* The streamed sound buffer (stream.cpp): one WAV file played once through a
 * DirectSound buffer, for the instruction-script player.  A watcher thread
 * marks the buffer done when playback ends. */

#pragma once
#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include <stddef.h>

/* What the script player hands to CStreamSoundbuffer::prepare.  Its copy is
 * embedded in the ScriptPlayer: "initwave" writes the file name, the
 * fixed-sound setup the rest.  Packed, because the ScriptPlayer field after it
 * follows directly. */
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
    static void checkLayout();
    static DWORD WINAPI watcherProc(LPVOID param);

    void                *vtable_;
    char                *filename_;
    IDirectSoundBuffer  *pSoundbuffer_;
    IDirectSound        *pDirectsound_;
    BYTE                 _pad0_[0x0a];
    DWORD                dwBuffer_size_;
    BYTE                 _pad1_[0x88];
    volatile DWORD       dwThread_done_;  // 0 playing, 1 finished or idle; the script player polls it
    HANDLE               watcher_thread_;
    HANDLE               stop_event_;
    BYTE                 _pad2_[0x08];
    CRITICAL_SECTION     cs_;  // 0x18 bytes
    BYTE                 _pad3_[0x02];
};
#pragma pack(pop)

inline void CStreamSoundbuffer::checkLayout()
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(CStreamSoundbuffer, vtable_)       == 0x00,  "vtable offset");
    static_assert(offsetof(CStreamSoundbuffer, pSoundbuffer_) == 0x08,  "pSoundbuffer offset");
    static_assert(offsetof(CStreamSoundbuffer, dwBuffer_size_)== 0x1a,  "dwBuffer_size offset");
    static_assert(offsetof(CStreamSoundbuffer, dwThread_done_)== 0xa6,  "dwThread_done offset");
    static_assert(offsetof(CStreamSoundbuffer, cs_)           == 0xba,  "cs offset");
#pragma GCC diagnostic pop
}

static_assert(sizeof(CStreamSoundbuffer)                 == 0xD4,  "CStreamSoundbuffer size");

/* The one-slot vtable: the scalar deleting destructor. */
extern "C" __declspec(dllexport) void *CStream_Vtable(void);

