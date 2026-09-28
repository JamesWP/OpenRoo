/* Static sound buffers: one whole .wav file loaded into a DirectSound buffer,
 * optionally with a 3D interface.  The building block of every effect the game
 * plays; voice pools (voicepool.h) are arrays of them. */

#pragma once
#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include <stddef.h>
 

/* One buffer.  It remembers its file, flags and logger so it can be reloaded
 * (on a lost buffer, or a 2D/3D switch) and duplicated. */
class __attribute__((packed)) CStaticSoundbuffer {
public:
     

    /* The scalar/vector deleting destructor, the one vtable slot: bit 1 means
     * an array, bit 0 frees the block.  Returns the block destroyed: this, or
     * for an array the count header four bytes below the first element. */
    static void * 
    scalarVectorDtor(CStaticSoundbuffer *self, unsigned int flags);

    CStaticSoundbuffer *init();  // returns this
    void reinitBuffer();         // sets the vtable, then reset
    void reset();                // releases the buffers and frees the file name
    int  createAndLoad3DSoundFile(IDirectSound *pDS, DWORD dwDsFlags,
                                  const char *filename, void *logger);

    /* Loads filename into a fresh buffer; returns 1 or 0.  The 3D form adds
     * DSBCAPS_CTRL3D and queries the 3D interface. */
    int  createAndLoadFile(IDirectSound *pDS, DWORD dwDsFlags,
                           const char *filename, void *logger);

    /* Reloads this buffer's own file under a new 3D mode, if it differs. */
    int  createAndLoad(IDirectSound *pDS, DWORD set3D);

    /* Turns the 3D processing on or off on a loaded 3D buffer. */
    int  apply3DMode(int enable3D);

    /* Duplicates other into this.  Returns other on success and NULL on
     * failure, which is what makes the callers' `== src` test a success test.
     * A non-zero flag suppresses the reload-from-file fallback when
     * duplication fails. */
    void *copy(IDirectSound *pDS, CStaticSoundbuffer *other, int flag);
    /* Restores a lost buffer and refills it from the file. */
    int  restoreBuffer();
    int  triggerPlayback(DWORD dwLoopFlags);
    void haltPlayback();
    void set3DPosition(float x, float y, float z, DWORD dwApply);

    void                 *vtable() const       { return vtable_; }
    void                 *logger() const       { return logger_; }
    char                 *filename() const     { return filename_; }
    DWORD                 dsFlags() const      { return dwDsFlags_; }
    IDirectSoundBuffer   *soundbuffer() const  { return soundbuffer_; }
    IDirectSound3DBuffer *threeDBuffer() const { return threeDBuffer_; }

private:
/* COM out-parameters need the field's address; both are 4-aligned, so the
 * packed-member warning is moot. */
 
 
    IDirectSoundBuffer   **soundbufferSlot() { return &soundbuffer_; }
    IDirectSound3DBuffer **threeDBufferSlot() { return &threeDBuffer_; }
 

    void                  *vtable_;
    void                  *logger_;     // stored, never used here
    char                  *filename_;   // heap copy of the path
    DWORD                  dwDsFlags_;  // the flags it was loaded with
    IDirectSoundBuffer    *soundbuffer_;
    IDirectSound3DBuffer  *threeDBuffer_;  // NULL for a 2D buffer

     
};

/* KAROO_SOUND_DIAG=1: logs each caller's first call.  Shared with the
 * streaming buffer (stream.cpp). */
  void CStatic_SoundFirstCall(const char *who, unsigned long *seen);
