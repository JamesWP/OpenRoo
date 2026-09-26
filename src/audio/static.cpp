#define DIRECTSOUND_VERSION 0x0800
#define INITGUID
#include <windows.h>
#include <dsound.h>
#include <string.h>
#include <stdio.h>
#include "static.h"
#include <stdlib.h>
#include "log.h"

/* ── WAV file parser ────────────────────────────────────────────────────── */

#define FOURCC(a,b,c,d) \
    ((DWORD)(BYTE)(a) | ((DWORD)(BYTE)(b) << 8) | \
     ((DWORD)(BYTE)(c) << 16) | ((DWORD)(BYTE)(d) << 24))

static const DWORD ID_RIFF = FOURCC('R','I','F','F');
static const DWORD ID_WAVE = FOURCC('W','A','V','E');
static const DWORD ID_FMT  = FOURCC('f','m','t',' ');
static const DWORD ID_DATA = FOURCC('d','a','t','a');

static bool wav_read_dword(HANDLE f, DWORD *out) {
    DWORD n;
    return ReadFile(f, out, 4, &n, NULL) && n == 4;
}

/*
 * Parses a WAV file.  Returns true on success; caller frees *fmt_out and
 * *pcm_out with HeapFree(GetProcessHeap(), ...).
 */
static bool parse_wav(const char *path,
                      WAVEFORMATEX **fmt_out, BYTE **pcm_out, DWORD *pcm_sz_out)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        log_write("CStatic: parse_wav: can't open '%s' err=%lu\n",
                  path, GetLastError());
        return false;
    }

    WAVEFORMATEX *fmt = NULL;
    BYTE         *pcm = NULL;
    DWORD         pcm_sz = 0;

    DWORD id, size, wave_id;
    if (!wav_read_dword(f, &id)   || id      != ID_RIFF) goto fail;
    if (!wav_read_dword(f, &size))                        goto fail;
    if (!wav_read_dword(f, &wave_id) || wave_id != ID_WAVE) goto fail;

    while (!fmt || !pcm) {
        DWORD chunk_id, chunk_size, n;
        if (!wav_read_dword(f, &chunk_id) || !wav_read_dword(f, &chunk_size))
            break;
        if (chunk_id == ID_FMT) {
            DWORD to_read = chunk_size < sizeof(WAVEFORMATEX)
                          ? chunk_size : sizeof(WAVEFORMATEX);
            fmt = (WAVEFORMATEX*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                           sizeof(WAVEFORMATEX));
            if (!fmt) goto fail;
            if (!ReadFile(f, fmt, to_read, &n, NULL) || n != to_read) goto fail;
            DWORD skip = chunk_size - to_read;
            if (skip) SetFilePointer(f, (LONG)(skip + (skip & 1)), NULL, FILE_CURRENT);
        } else if (chunk_id == ID_DATA) {
            pcm_sz = chunk_size;
            pcm    = (BYTE*)HeapAlloc(GetProcessHeap(), 0, pcm_sz);
            if (!pcm) goto fail;
            if (!ReadFile(f, pcm, pcm_sz, &n, NULL) || n != pcm_sz) goto fail;
        } else {
            DWORD skip = chunk_size + (chunk_size & 1);
            SetFilePointer(f, (LONG)skip, NULL, FILE_CURRENT);
        }
    }
    CloseHandle(f);
    if (!fmt || !pcm) goto free_and_fail;
    *fmt_out   = fmt;
    *pcm_out   = pcm;
    *pcm_sz_out = pcm_sz;
    return true;

fail:
    CloseHandle(f);
free_and_fail:
    HeapFree(GetProcessHeap(), 0, fmt);
    HeapFree(GetProcessHeap(), 0, pcm);
    return false;
}

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static char *heap_strdup(const char *s)
{
    if (!s) return NULL;
    DWORD len = (DWORD)strlen(s) + 1;
    char *out = (char*)HeapAlloc(GetProcessHeap(), 0, len);
    if (out) memcpy(out, s, len);
    return out;
}

/*
 * Creates a DirectSound buffer, loads PCM from parse_wav output, and returns
 * the buffer. On failure, returns NULL and frees fmt/pcm.  On success, fmt
 * and pcm are freed by this function.
 */
static IDirectSoundBuffer *create_ds_buffer(IDirectSound *pDS,
                                            DWORD dwFlags,
                                            WAVEFORMATEX *fmt,
                                            BYTE *pcm, DWORD pcm_sz)
{
    DSBUFFERDESC desc = {};
    desc.dwSize        = sizeof(DSBUFFERDESC);
    desc.dwFlags       = dwFlags;
    desc.dwBufferBytes = pcm_sz;
    desc.lpwfxFormat   = fmt;

    IDirectSoundBuffer *pBuf = NULL;
    HRESULT hr = pDS->CreateSoundBuffer(&desc, &pBuf, NULL);
    if (FAILED(hr)) {
        log_write("CStatic: CreateSoundBuffer failed hr=0x%lx\n", (unsigned long)hr);
        HeapFree(GetProcessHeap(), 0, fmt);
        HeapFree(GetProcessHeap(), 0, pcm);
        return NULL;
    }

    void  *ptr1 = NULL, *ptr2 = NULL;
    DWORD  bytes1 = 0,   bytes2 = 0;
    hr = pBuf->Lock(0, pcm_sz, &ptr1, &bytes1, &ptr2, &bytes2, 0);
    if (SUCCEEDED(hr)) {
        memcpy(ptr1, pcm, bytes1);
        if (ptr2 && bytes2) memcpy(ptr2, (BYTE*)pcm + bytes1, bytes2);
        pBuf->Unlock(ptr1, bytes1, ptr2, bytes2);
    } else {
        log_write("CStatic: Lock failed hr=0x%lx\n", (unsigned long)hr);
        pBuf->Release();
        pBuf = NULL;
    }

    HeapFree(GetProcessHeap(), 0, fmt);
    HeapFree(GetProcessHeap(), 0, pcm);
    return pBuf;
}

static void CStatic_ReinitBuffer_impl(CStaticSoundbuffer *self);

/* ── KAROO_SOUND_FX / KAROO_SOUND_DIAG ──────────────────────────────────
 *
 * Both sound classes own their vtable now, and neither writes anything that
 * can be seen; the pointer they install is the only thing they own outright,
 * so it was what the control moved (retired with the game's tables,
 * ENDGAME_PLAN.md "Direction") — the KAROO_IMAGE_FX=gamevtbl measurement,
 * applied to the sound objects.  `gamevtbl` installs the game's tables
 * (0x45ef9c / 0x45efa4), whose one slot each points at a UD2-stubbed
 * original, so any dispatch through the table faults as c000001d.  A clean
 * run says nothing read the table; a fault names the reader.
 *
 * Blast radius: one pointer field per object, no geometry, so levelreport.py
 * is safe (CLAUDE.md's rule about which gate may host a control).
 *
 * KAROO_SOUND_DIAG=1 is the census that tells "ran and agreed" from "never
 * ran": each destructor entry point announces its first call.  Read by VALUE.
 */
static bool sound_diag(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = (GetEnvironmentVariableA("KAROO_SOUND_DIAG", buf, sizeof(buf))
                  && buf[0] == '1') ? 1 : 0;
    }
    return cached != 0;
}

extern "C" {

/* First call, then a running tally — never a purely periodic sample, which
 * reads zero forever for anything first reached late (linkedlist.cpp). */
__declspec(dllexport) void CStatic_SoundFirstCall(const char *who,
                                                  unsigned long *seen)
{
    if (sound_diag() && (*seen)++ == 0)
        log_write("sound: DIAG first call -- %s\n", who);
}

/* Forward declaration: the table below needs the slot's address. */
__declspec(dllexport) void * __attribute__((thiscall))
CStatic_ScalarVectorDtor(CStaticSoundbuffer *self, unsigned int flags);

/* Our own one-slot table. */
static void *const g_CStaticVtable[1] = { (void *)&CStatic_ScalarVectorDtor };

__declspec(dllexport) void *CStatic_Vtable(void)
{
    return (void *)g_CStaticVtable;
}

/* ─── CStaticSoundbuffer::ScalarVectorDtor (0x00442a80) ──────────────────
 *
 * MSVC's combined scalar/vector deleting destructor, and the last function of
 * the VoicePool TU.  Two shapes, chosen by bit 1 of `flags`:
 *
 *   flags & 2   an ARRAY.  The block VoicePool::Fill3D allocated is
 *               `count*0x18 + 4`; the count sits in the leading dword and
 *               pBufs is base+4, so the header is at `self[-1].threeDBuffer`
 *               — four bytes below the first element.  The original hands
 *               (self, 0x18, count, ReinitBuffer) to the CRT's vector-dtor
 *               iterator 0x00451e37, which walks the elements in REVERSE
 *               (`ptr += size*count`, then `--count; js out; ptr -= size`).
 *               The loop is written out here instead of calling that helper:
 *               it would be a new CRT callback, which E1 exists to remove,
 *               and the helper's SEH frame exists only for a destructor that
 *               throws — ReinitBuffer cannot.  The return is the block base,
 *               not `self`.
 *   otherwise   one object: ReinitBuffer, then free.
 *
 * Bit 0 frees the block.  The free is `FactAlloc::Free2` through alloc.h and
 * stays there: the array came from the game's `operator new` in Fill3D and
 * Clone, so the game's heap owns the other side (alloc.h's own rule).  Note
 * the 3-flag call in VoicePoolWipe passes both bits, so the array path is
 * the one the game actually takes.
 */
__declspec(dllexport) void * __attribute__((thiscall))
CStatic_ScalarVectorDtor(CStaticSoundbuffer *self, unsigned int flags)
{
    static unsigned long seen;
    CStatic_SoundFirstCall("CStaticSoundbuffer::ScalarVectorDtor", &seen);

    if (flags & 2) {
        /* The count header, four bytes below the first element. */
        void *base  = (char *)self - 4;
        int   count = *(int *)base;

        /* Reverse order, exactly as the iterator walks it. */
        for (int i = count - 1; i >= 0; --i)
            CStatic_ReinitBuffer_impl(&self[i]);

        if (flags & 1)
            free(base);
        return base;
    }

    CStatic_ReinitBuffer_impl(self);
    if (flags & 1)
        free(self);
    return self;
}

} // extern "C"

/* ── Forward declarations ──────────────────────────────────────────────── */

static void CStatic_Reset_impl(CStaticSoundbuffer *self);
static int  CStatic_CreateAndLoadFile_impl(CStaticSoundbuffer *self,
                                           IDirectSound *pDS, DWORD dwDsFlags,
                                           const char *filename, void *logger);
static int  CStatic_CreateAndLoad3DSoundFile_impl(CStaticSoundbuffer *self,
                                                  IDirectSound *pDS, DWORD dwDsFlags,
                                                  const char *filename, void *logger);

/* ── Method implementations ─────────────────────────────────────────────── */

static CStaticSoundbuffer* CStatic_Init_impl(CStaticSoundbuffer *self)
{
    memset(self, 0, sizeof(*self));
    self->vtable = CStatic_Vtable();
    return self;
}

static void CStatic_ReinitBuffer_impl(CStaticSoundbuffer *self)
{
    self->vtable = CStatic_Vtable();
    CStatic_Reset_impl(self);
}

static void CStatic_Reset_impl(CStaticSoundbuffer *self)
{
    if (self->threeDBuffer) {
        self->threeDBuffer->Release();
        self->threeDBuffer = NULL;
    }
    if (self->soundbuffer) {
        self->soundbuffer->Release();
        self->soundbuffer = NULL;
    }
    if (self->filename) {
        HeapFree(GetProcessHeap(), 0, self->filename);
        self->filename = NULL;
    }
    self->logger   = NULL;
    self->dwDsFlags = 0;
}

static int CStatic_CreateAndLoadFile_impl(CStaticSoundbuffer *self,
                                          IDirectSound *pDS, DWORD dwDsFlags,
                                          const char *filename, void *logger)
{
    //log_write("CStatic::CreateAndLoadFile(this=%p, file='%s')\n", self, filename ? filename : "<null>");

    CStatic_Reset_impl(self);

    if (!pDS || !filename) {
        log_write("CStatic::CreateAndLoadFile: null pDS or filename\n");
        return 0;
    }

    self->logger    = logger;
    self->dwDsFlags = dwDsFlags;
    self->filename  = heap_strdup(filename);
    if (!self->filename) return 0;

    WAVEFORMATEX *fmt  = NULL;
    BYTE         *pcm  = NULL;
    DWORD         pcm_sz = 0;
    if (!parse_wav(filename, &fmt, &pcm, &pcm_sz)) {
        log_write("CStatic::CreateAndLoadFile: parse_wav failed for '%s'\n", filename);
        CStatic_Reset_impl(self);
        return 0;
    }

    /* Build buffer flags: always GETCURRENTPOSITION2 + GLOBALFOCUS + STATIC.
     * If caller requested CTRL3D (bit 4), add it and drop CTRLPAN (mutually exclusive). */
    DWORD bufFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS | DSBCAPS_STATIC;
    if (dwDsFlags & DSBCAPS_CTRL3D)
        bufFlags |= DSBCAPS_CTRL3D;

    self->soundbuffer = create_ds_buffer(pDS, bufFlags, fmt, pcm, pcm_sz);
    if (!self->soundbuffer) {
        log_write("CStatic::CreateAndLoadFile: create_ds_buffer failed for '%s'\n", filename);
        CStatic_Reset_impl(self);
        return 0;
    }

    //log_write("CStatic::CreateAndLoadFile: OK '%s' dsflags=0x%lx\n",
    //          filename, (unsigned long)bufFlags);
    return 1;
}

static int CStatic_CreateAndLoad3DSoundFile_impl(CStaticSoundbuffer *self,
                                                 IDirectSound *pDS, DWORD dwDsFlags,
                                                 const char *filename, void *logger)
{
    //log_write("CStatic::CreateAndLoad3DSoundFile(this=%p, file='%s')\n",
    //          self, filename ? filename : "<null>");

    /* Force CTRL3D flag then create the base buffer. */
    int ok = CStatic_CreateAndLoadFile_impl(self, pDS, dwDsFlags | DSBCAPS_CTRL3D,
                                            filename, logger);
    if (!ok) return 0;

    /* Obtain the 3D interface. */
    GUID iid3D = IID_IDirectSound3DBuffer;
    HRESULT hr = self->soundbuffer->QueryInterface(iid3D, (void**)self->threeDBufferSlot());
    if (FAILED(hr)) {
        log_write("CStatic::CreateAndLoad3DSoundFile: QueryInterface IID_IDirectSound3DBuffer hr=0x%lx\n",
                  (unsigned long)hr);
        CStatic_Reset_impl(self);
        return 0;
    }

    //log_write("CStatic::CreateAndLoad3DSoundFile: OK '%s'\n", filename);
    return 1;
}

/*
 * Copy(other, pDS, flag)
 *   flag != 0 → no file-based fallback if DuplicateSoundBuffer fails.
 *   flag == 0 → fallback to CreateAndLoad*File.
 */
static void* CStatic_Copy_impl(CStaticSoundbuffer *self,
                                CStaticSoundbuffer *other,
                                IDirectSound *pDS,
                                int flag)
{
    //log_write("CStatic::Copy(this=%p, other=%p, flag=%d)\n", self, other, flag);

    CStatic_Reset_impl(self);

    if (!other || !other->soundbuffer) return NULL;

    /* Attempt to duplicate the DirectSound buffer. */
    HRESULT hr = pDS->DuplicateSoundBuffer(other->soundbuffer, self->soundbufferSlot());
    if (SUCCEEDED(hr)) {
        /* Duplicate succeeded: copy metadata and re-acquire 3D interface if needed. */
        self->filename  = heap_strdup(other->filename);
        self->logger    = other->logger;
        self->dwDsFlags = other->dwDsFlags;

        if (other->threeDBuffer) {
            GUID iid3D = IID_IDirectSound3DBuffer;
            hr = self->soundbuffer->QueryInterface(iid3D, (void**)self->threeDBufferSlot());
            if (FAILED(hr)) {
                log_write("CStatic::Copy: QueryInterface 3D failed hr=0x%lx\n",
                          (unsigned long)hr);
                CStatic_Reset_impl(self);
                return NULL;
            }
        }
        return other; /* matches original return semantics */
    }

    /* DuplicateSoundBuffer failed. */
    log_write("CStatic::Copy: DuplicateSoundBuffer failed hr=0x%lx\n", (unsigned long)hr);
    if (flag != 0) {
        CStatic_Reset_impl(self);
        return NULL;
    }

    /* Fallback: reload from file. */
    int ok;
    if (other->threeDBuffer) {
        ok = CStatic_CreateAndLoad3DSoundFile_impl(self, pDS,
                 other->dwDsFlags | DSBCAPS_CTRL3D, other->filename, other->logger);
    } else {
        ok = CStatic_CreateAndLoadFile_impl(self, pDS,
                 other->dwDsFlags & ~DSBCAPS_CTRL3D, other->filename, other->logger);
    }
    if (!ok) {
        CStatic_Reset_impl(self);
        return NULL;
    }
    return self;
}

/*
 * CreateAndLoad(pDS, set3D)
 *   set3D == 0 → ensure buffer is 2D (release 3D interface if present)
 *   set3D != 0 → ensure buffer is 3D (acquire 3D interface if absent)
 */
static int CStatic_CreateAndLoad_impl(CStaticSoundbuffer *self,
                                      IDirectSound *pDS, DWORD set3D)
{
    if (!self->soundbuffer) return 0;

    if (set3D == 0) {
        /* Want 2D — already 2D? */
        if (!self->threeDBuffer) return 1;
        /* Reload without CTRL3D. */
        char *fn     = heap_strdup(self->filename);
        void *logger = self->logger;
        DWORD flags  = self->dwDsFlags & ~DSBCAPS_CTRL3D;
        CStatic_Reset_impl(self);
        int ok = CStatic_CreateAndLoadFile_impl(self, pDS, flags, fn, logger);
        HeapFree(GetProcessHeap(), 0, fn);
        return ok;
    } else {
        /* Want 3D — already 3D? */
        if (self->threeDBuffer) return 1;
        /* Reload with CTRL3D. */
        char *fn     = heap_strdup(self->filename);
        void *logger = self->logger;
        DWORD flags  = self->dwDsFlags;
        CStatic_Reset_impl(self);
        int ok = CStatic_CreateAndLoad3DSoundFile_impl(self, pDS, flags, fn, logger);
        HeapFree(GetProcessHeap(), 0, fn);
        return ok;
    }
}

static int CStatic_Apply3DMode_impl(CStaticSoundbuffer *self, int enable3D)
{
    if (!self->threeDBuffer) return 0;
    DWORD mode = enable3D ? DS3DMODE_NORMAL : DS3DMODE_DISABLE;
    self->threeDBuffer->SetMode(mode, 0 /* DS3DAPPLY_NOW */);
    return 1;
}

static int CStatic_RestoreBuffer_impl(CStaticSoundbuffer *self)
{
    if (!self->soundbuffer) return 0;

    HRESULT hr = self->soundbuffer->Restore();
    if (FAILED(hr)) {
        log_write("CStatic::RestoreBuffer: Restore failed hr=0x%lx\n", (unsigned long)hr);
        return 0;
    }

    WAVEFORMATEX *fmt  = NULL;
    BYTE         *pcm  = NULL;
    DWORD         pcm_sz = 0;
    if (!parse_wav(self->filename, &fmt, &pcm, &pcm_sz)) {
        log_write("CStatic::RestoreBuffer: parse_wav failed for '%s'\n",
                  self->filename ? self->filename : "<null>");
        return 0;
    }

    void  *ptr1 = NULL, *ptr2 = NULL;
    DWORD  bytes1 = 0,   bytes2 = 0;
    hr = self->soundbuffer->Lock(0, pcm_sz, &ptr1, &bytes1, &ptr2, &bytes2, 0);
    if (FAILED(hr)) {
        log_write("CStatic::RestoreBuffer: Lock failed hr=0x%lx\n", (unsigned long)hr);
        HeapFree(GetProcessHeap(), 0, fmt);
        HeapFree(GetProcessHeap(), 0, pcm);
        return 0;
    }
    memcpy(ptr1, pcm, bytes1);
    if (ptr2 && bytes2) memcpy(ptr2, (BYTE*)pcm + bytes1, bytes2);
    self->soundbuffer->Unlock(ptr1, bytes1, ptr2, bytes2);

    HeapFree(GetProcessHeap(), 0, fmt);
    HeapFree(GetProcessHeap(), 0, pcm);
    //log_write("CStatic::RestoreBuffer: OK '%s'\n", self->filename);
    return 1;
}

static int CStatic_TriggerPlayback_impl(CStaticSoundbuffer *self, DWORD dwLoopFlags)
{
    if (!self->soundbuffer) return 0;

    //log_write("CStatic::TriggerPlayback(this=%p, file='%s', loop=%lu)\n",
    //          self, self->filename ? self->filename : "<null>",
    //          (unsigned long)dwLoopFlags);
    HRESULT hr = self->soundbuffer->Play(0, 0, dwLoopFlags);
    if (hr == DSERR_BUFFERLOST) {
        log_write("CStatic::TriggerPlayback: buffer lost, restoring '%s'\n",
                  self->filename ? self->filename : "<null>");
        if (!CStatic_RestoreBuffer_impl(self)) return -1;
        hr = self->soundbuffer->Play(0, 0, dwLoopFlags);
    }
    if (FAILED(hr)) {
        log_write("CStatic::TriggerPlayback: Play failed hr=0x%lx\n", (unsigned long)hr);
    }
    return (int)hr;
}

static void CStatic_HaltPlayback_impl(CStaticSoundbuffer *self)
{
    if (self->soundbuffer)
        self->soundbuffer->Stop();
}

static void CStatic_Set3DPosition_impl(CStaticSoundbuffer *self,
                                       float x, float y, float z, DWORD dwApply)
{
    if (self->threeDBuffer)
        self->threeDBuffer->SetPosition(x, y, z, dwApply);
}

/* ── Exports — extern "C" thiscall wrappers ─────────────────────────────── */
extern "C" {

__declspec(dllexport) CStaticSoundbuffer* __attribute__((thiscall))
CStatic_Init(CStaticSoundbuffer *self)
    { return CStatic_Init_impl(self); }

__declspec(dllexport) void __attribute__((thiscall))
CStatic_ReinitBuffer(CStaticSoundbuffer *self)
    { CStatic_ReinitBuffer_impl(self); }

__declspec(dllexport) void __attribute__((thiscall))
CStatic_Reset(CStaticSoundbuffer *self)
    { CStatic_Reset_impl(self); }

__declspec(dllexport) int __attribute__((thiscall))
CStatic_CreateAndLoadFile(CStaticSoundbuffer *self,
                          IDirectSound *pDS, DWORD dwDsFlags,
                          const char *filename, void *logger)
    { return CStatic_CreateAndLoadFile_impl(self, pDS, dwDsFlags, filename, logger); }

__declspec(dllexport) int __attribute__((thiscall))
CStatic_CreateAndLoad3DSoundFile(CStaticSoundbuffer *self,
                                 IDirectSound *pDS, DWORD dwDsFlags,
                                 const char *filename, void *logger)
    { return CStatic_CreateAndLoad3DSoundFile_impl(self, pDS, dwDsFlags, filename, logger); }

__declspec(dllexport) void* __attribute__((thiscall))
CStatic_Copy(CStaticSoundbuffer *self,
             IDirectSound *pDS, CStaticSoundbuffer *other, int flag)
    { return CStatic_Copy_impl(self, other, pDS, flag); }

__declspec(dllexport) int __attribute__((thiscall))
CStatic_CreateAndLoad(CStaticSoundbuffer *self,
                      IDirectSound *pDS, DWORD set3D)
    { return CStatic_CreateAndLoad_impl(self, pDS, set3D); }

__declspec(dllexport) int __attribute__((thiscall))
CStatic_Apply3DMode(CStaticSoundbuffer *self, int enable3D)
    { return CStatic_Apply3DMode_impl(self, enable3D); }

__declspec(dllexport) int __attribute__((thiscall))
CStatic_RestoreBuffer(CStaticSoundbuffer *self)
    { return CStatic_RestoreBuffer_impl(self); }

__declspec(dllexport) int __attribute__((thiscall))
CStatic_TriggerPlayback(CStaticSoundbuffer *self, DWORD dwLoopFlags)
    { return CStatic_TriggerPlayback_impl(self, dwLoopFlags); }

__declspec(dllexport) void __attribute__((thiscall))
CStatic_HaltPlayback(CStaticSoundbuffer *self)
    { CStatic_HaltPlayback_impl(self); }

__declspec(dllexport) void __attribute__((thiscall))
CStatic_Set3DPosition(CStaticSoundbuffer *self,
                      float x, float y, float z, DWORD dwApply)
    { CStatic_Set3DPosition_impl(self, x, y, z, dwApply); }

} // extern "C"
