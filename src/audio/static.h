#pragma once
#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include <stddef.h>

/*
 * CStaticSoundbuffer — 24 bytes (0x18).
 * Layout verified against game allocation and field accesses.
 */
#pragma pack(push, 1)
struct CStaticSoundbuffer {
    void                  *vtable;       // +0x00
    void                  *logger;       // +0x04  opaque Logger*; stored but not dereferenced here
    char                  *filename;     // +0x08
    DWORD                  dwDsFlags;    // +0x0C
    IDirectSoundBuffer    *soundbuffer;  // +0x10
    IDirectSound3DBuffer  *threeDBuffer; // +0x14
};
#pragma pack(pop)

static_assert(offsetof(CStaticSoundbuffer, vtable)       == 0x00, "vtable offset");
static_assert(offsetof(CStaticSoundbuffer, logger)       == 0x04, "logger offset");
static_assert(offsetof(CStaticSoundbuffer, filename)     == 0x08, "filename offset");
static_assert(offsetof(CStaticSoundbuffer, dwDsFlags)    == 0x0C, "dwDsFlags offset");
static_assert(offsetof(CStaticSoundbuffer, soundbuffer)  == 0x10, "soundbuffer offset");
static_assert(offsetof(CStaticSoundbuffer, threeDBuffer) == 0x14, "threeDBuffer offset");
static_assert(sizeof(CStaticSoundbuffer)                 == 0x18, "CStaticSoundbuffer size");

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
