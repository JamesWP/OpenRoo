/* Static sound buffers: one whole .wav file loaded into a DirectSound buffer,
 * optionally with a 3D interface.  The building block of every effect the game
 * plays; voice pools (voicepool.h) are arrays of them. */

#pragma once
#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include <stddef.h>
#include "layout.h"

/* One buffer.  It remembers its file, flags and logger so it can be reloaded
 * (on a lost buffer, or a 2D/3D switch) and duplicated. */
struct __attribute__((packed)) CStaticSoundbuffer {
    static const int ORIGIN = 0;

    void                  *vtable;
    void                  *logger;     // stored, never used here
    char                  *filename;   // heap copy of the path
    DWORD                  dwDsFlags;  // the flags it was loaded with
    IDirectSoundBuffer    *soundbuffer;
    IDirectSound3DBuffer  *threeDBuffer;  // NULL for a 2D buffer

/* COM out-parameters need the field's address; both are 4-aligned, so the
 * packed-member warning is moot. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    IDirectSoundBuffer   **soundbufferSlot() { return &soundbuffer; }
    IDirectSound3DBuffer **threeDBufferSlot() { return &threeDBuffer; }
#pragma GCC diagnostic pop

private:
    KAROO_LAYOUT_REGISTER(CStaticSoundbuffer);
};

/* The size matters as well as the offsets: voice pools index arrays of these
 * with a stride of 0x18. */
KAROO_LAYOUT_CHECKS(CStaticSoundbuffer)
{
    KAROO_LAYOUT_AT(vtable,       0x00);
    KAROO_LAYOUT_AT(logger,       0x04);
    KAROO_LAYOUT_AT(filename,     0x08);
    KAROO_LAYOUT_AT(dwDsFlags,    0x0C);
    KAROO_LAYOUT_AT(soundbuffer,  0x10);
    KAROO_LAYOUT_AT(threeDBuffer, 0x14);
    KAROO_LAYOUT_SIZE(0x18);
}

/* The one-slot vtable every buffer carries. */
extern "C" __declspec(dllexport) void *CStatic_Vtable(void);

extern "C" {
/* The scalar/vector deleting destructor: bit 1 means an array, bit 0 frees the
 * block.  Returns the block destroyed: this, or for an array the count header
 * four bytes below the first element. */
__declspec(dllexport) void * __attribute__((thiscall))
CStatic_ScalarVectorDtor(CStaticSoundbuffer *self, unsigned int flags);

/* KAROO_SOUND_DIAG=1: logs each caller's first call.  Shared with the
 * streaming buffer (stream.cpp). */
__declspec(dllexport) void CStatic_SoundFirstCall(const char *who,
                                                  unsigned long *seen);

__declspec(dllexport) CStaticSoundbuffer * __attribute__((thiscall))
CStatic_Init(CStaticSoundbuffer *self);  // returns this
__declspec(dllexport) void __attribute__((thiscall))
CStatic_ReinitBuffer(CStaticSoundbuffer *self);  // sets the vtable, then Reset
__declspec(dllexport) void __attribute__((thiscall))
CStatic_Reset(CStaticSoundbuffer *self);  // releases the buffers and frees the file name
__declspec(dllexport) int  __attribute__((thiscall))
CStatic_CreateAndLoad3DSoundFile(CStaticSoundbuffer *self,
                                 IDirectSound *pDS, DWORD dwDsFlags,
                                 const char *filename, void *logger);

/* Loads filename into a fresh buffer; returns 1 or 0.  The 3D form adds
 * DSBCAPS_CTRL3D and queries the 3D interface. */
__declspec(dllexport) int  __attribute__((thiscall))
CStatic_CreateAndLoadFile(CStaticSoundbuffer *self,
                          IDirectSound *pDS, DWORD dwDsFlags,
                          const char *filename, void *logger);

/* Reloads this buffer's own file under a new 3D mode, if it differs. */
__declspec(dllexport) int  __attribute__((thiscall))
CStatic_CreateAndLoad(CStaticSoundbuffer *self, IDirectSound *pDS, DWORD set3D);

/* Turns the 3D processing on or off on a loaded 3D buffer. */
__declspec(dllexport) int  __attribute__((thiscall))
CStatic_Apply3DMode(CStaticSoundbuffer *self, int enable3D);

/* Duplicates other into self.  Returns other on success and NULL on failure,
 * which is what makes the callers' `== src` test a success test.  A non-zero
 * flag suppresses the reload-from-file fallback when duplication fails. */
__declspec(dllexport) void * __attribute__((thiscall))
CStatic_Copy(CStaticSoundbuffer *self,
             IDirectSound *pDS, CStaticSoundbuffer *other, int flag);
__declspec(dllexport) int  __attribute__((thiscall))
CStatic_TriggerPlayback(CStaticSoundbuffer *self, DWORD dwLoopFlags);
__declspec(dllexport) void __attribute__((thiscall))
CStatic_HaltPlayback(CStaticSoundbuffer *self);
__declspec(dllexport) void __attribute__((thiscall))
CStatic_Set3DPosition(CStaticSoundbuffer *self,
                      float x, float y, float z, DWORD dwApply);
}
