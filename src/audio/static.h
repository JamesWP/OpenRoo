#pragma once
#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include <stddef.h>
#include "layout.h"

/*
 * CStaticSoundbuffer — 24 bytes (0x18).
 * Layout verified against game allocation and field accesses.
 */
struct __attribute__((packed)) CStaticSoundbuffer {
    static const int ORIGIN = 0;

    void                  *vtable;       // +0x00
    void                  *logger;       // +0x04  opaque Logger*; stored but not dereferenced here
    char                  *filename;     // +0x08
    DWORD                  dwDsFlags;    // +0x0C
    IDirectSoundBuffer    *soundbuffer;  // +0x10
    IDirectSound3DBuffer  *threeDBuffer; // +0x14

    /* COM out-parameters need the field's address; the one
     * -Waddress-of-packed-member suppression lives here rather than at every
     * use (the textrenderer.h / game.h idiom).  0x10 and 0x14 are 4-aligned,
     * so nothing is actually under-aligned. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    IDirectSoundBuffer   **soundbufferSlot() { return &soundbuffer; }
    IDirectSound3DBuffer **threeDBufferSlot() { return &threeDBuffer; }
#pragma GCC diagnostic pop

private:
    KAROO_LAYOUT_REGISTER(CStaticSoundbuffer);
};

/* The size is relied on as well as the offsets: the game allocates these
 * with operator new(0x18), and VoicePool indexes an array of them with a
 * hard-coded stride of 0x18. */
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

/*
 * Original vtable at 0x45ef9c — one slot: ScalarVectorDtor @ 0x442a80.
 * We leave the vtable untouched: ScalarVectorDtor calls ReinitBuffer, which
 * patch.py redirects to our CStatic_ReinitBuffer, so virtual dtor cleanup
 * reaches our code without a vtable patch.
 */
static const void *const STATIC_VTABLE = reinterpret_cast<const void*>(0x45ef9c);

/* ─── Our reimplementations, defined in static.cpp ───────────────────────
 *
 * Declared here, by the file that owns them, so callers include this header
 * instead of redeclaring the exports.  Only the ones some caller outside
 * static.cpp uses are listed; add others as callers are converted.
 */
extern "C" {
__declspec(dllexport) CStaticSoundbuffer * __attribute__((thiscall))
CStatic_Init(CStaticSoundbuffer *self);   /* a ctor: returns `this` */
__declspec(dllexport) void __attribute__((thiscall))
CStatic_ReinitBuffer(CStaticSoundbuffer *self);  /* set vtable, then Reset */
__declspec(dllexport) void __attribute__((thiscall))
CStatic_Reset(CStaticSoundbuffer *self);  /* release COM refs, free filename */
__declspec(dllexport) int  __attribute__((thiscall))
CStatic_CreateAndLoad3DSoundFile(CStaticSoundbuffer *self,
                                 IDirectSound *pDS, DWORD dwDsFlags,
                                 const char *filename, void *logger);
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
