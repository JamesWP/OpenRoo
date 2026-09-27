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
struct CStaticSoundbuffer {

    void                  *vtable;
    void                  *logger;     // stored, never used here
    char                  *filename;   // heap copy of the path
    DWORD                  dwDsFlags;  // the flags it was loaded with
    IDirectSoundBuffer    *soundbuffer;
    IDirectSound3DBuffer  *threeDBuffer;  // NULL for a 2D buffer

    // COM out-parameters need the field's address.
    IDirectSoundBuffer   **soundbufferSlot() { return &soundbuffer; }
    IDirectSound3DBuffer **threeDBufferSlot() { return &threeDBuffer; }

private:
};

/* The size matters as well as the offsets: voice pools index arrays of these
 * with a stride of 0x18. */

/* The one-slot vtable every buffer carries. */
void *CStatic_Vtable(void);

/* The scalar/vector deleting destructor: bit 1 means an array, bit 0 frees the
 * block.  Returns the block destroyed: this, or for an array the count header
 * four bytes below the first element. */
void *CStatic_ScalarVectorDtor(CStaticSoundbuffer *self, unsigned int flags);

/* KAROO_SOUND_DIAG=1: logs each caller's first call.  Shared with the
 * streaming buffer (stream.cpp). */
void CStatic_SoundFirstCall(const char *who,
                            unsigned long *seen);

CStaticSoundbuffer *CStatic_Init(CStaticSoundbuffer *self);  // returns this
void
CStatic_ReinitBuffer(CStaticSoundbuffer *self);  // sets the vtable, then Reset
void
CStatic_Reset(CStaticSoundbuffer *self);  // releases the buffers and frees the file name
int CStatic_CreateAndLoad3DSoundFile(CStaticSoundbuffer *self,
                                     IDirectSound *pDS, DWORD dwDsFlags,
                                     const char *filename, void *logger);

/* Loads filename into a fresh buffer; returns 1 or 0.  The 3D form adds
 * DSBCAPS_CTRL3D and queries the 3D interface. */
int CStatic_CreateAndLoadFile(CStaticSoundbuffer *self,
                              IDirectSound *pDS, DWORD dwDsFlags,
                              const char *filename, void *logger);

/* Reloads this buffer's own file under a new 3D mode, if it differs. */
int
CStatic_CreateAndLoad(CStaticSoundbuffer *self, IDirectSound *pDS, DWORD set3D);

/* Turns the 3D processing on or off on a loaded 3D buffer. */
int CStatic_Apply3DMode(CStaticSoundbuffer *self, int enable3D);

/* Duplicates other into self.  Returns other on success and NULL on failure,
 * which is what makes the callers' `== src` test a success test.  A non-zero
 * flag suppresses the reload-from-file fallback when duplication fails. */
void *CStatic_Copy(CStaticSoundbuffer *self,
                   IDirectSound *pDS, CStaticSoundbuffer *other, int flag);
int CStatic_TriggerPlayback(CStaticSoundbuffer *self, DWORD dwLoopFlags);
void CStatic_HaltPlayback(CStaticSoundbuffer *self);
void CStatic_Set3DPosition(CStaticSoundbuffer *self,
                           float x, float y, float z, DWORD dwApply);
