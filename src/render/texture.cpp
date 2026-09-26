/* SceneTexture / LoadedImage release path reimplementation.
 *
 *   0x43ebb0 LoadedImage::ReleaseTextureSurfaces  __thiscall(this), ret 0
 *            — 3 E8 call sites, all-CALL refs
 *   0x440050 SceneTexture::ReleaseD3DTexture      __thiscall(this), ret 0
 *            — 8 E8 call sites, all-CALL refs
 *   0x43eb00 LoadedImage::Load                    __thiscall(this), ret 0
 *            — 24 E8 call sites, all-CALL refs (surface-lost reload)
 *
 * Signature note: HOOKS.md lists ReleaseTextureSurfaces as `__fastcall`.  The
 * original is `mov ecx,esi` at entry and a plain `ret` — this in ECX, zero
 * stack args, i.e. __thiscall.  For a one-argument function the two are
 * indistinguishable at the call site, but the entry is what matters when
 * declaring the replacement, and `__fastcall` would be misleading to anyone
 * adding a second parameter later.
 *
 * ReleaseTextureSurfaces is shared: it is called on both the 24-byte base
 * LoadedImage instances (from the error paths in CreateSurfaceDIB /
 * BlitDIBToSurface) and, via ReleaseD3DTexture, on the 28-byte SceneTexture
 * ones.  It therefore takes LoadedImage* and must not touch +0x18 — a base
 * instance has no such field (the globals at 0x46c798 and 0x4dc7a8 are
 * followed by unrelated data exactly 24 bytes later).
 *
 * Preserved deliberately: pTextureSurface and pTexturePalette are NULLed
 * *unconditionally*, outside their null checks, while pImageName is NULLed
 * only inside its.  The originals are inconsistent about this and it is
 * reproduced rather than tidied.
 *
 * The call from ReleaseD3DTexture to ReleaseTextureSurfaces (0x440069) sits
 * inside a function this patch UD2-stubs, so rewriting it is harmless; the
 * replacement calls its own copy directly.
 *
 * ─── The LoadedImage ctor/dtor family (2026-09-19, ENDGAME_PLAN E1/E2) ────
 *
 *   0x43dde0 LoadedImage::Ctor                __thiscall(this) -> this
 *   0x43de20 LoadedImage::DtorBody            __thiscall(this), ret 0
 *   0x43de00 LoadedImage::ScalarDeletingDtor  __thiscall(this, flags), ret 4
 *
 * They come here rather than into texturedib.cpp because texture.h owns the
 * two structs and texture.cpp owns their release path; `make check-homes`
 * then has one answer for the whole family.  They are replaced in the same
 * cycle as SceneTexture's three (scenetexture.cpp) for the reason the .fon
 * loader recorded: SceneTexture::Constructor CALLs 0x43dde0 and
 * SceneTexture::DtorBody tail-JMPs to 0x43de20, so taking the derived class
 * alone would have created two callbacks rather than retired any.
 *
 * ─── The vtable is ours, and the game's is left as a tripwire ─────────────
 *
 * LoadedImage's table at 0x0045d708 is exactly ONE slot.  Slot count is not
 * the usual gap/4 here: the next four data items (0x45d70c, 0x45d710) are
 * float constants — 0x3b808081 = 1/255 and 0x41039ce7 — and 0x45d714 is
 * BridgeObject's two-slot table.  Ghidra's xrefs settle it: the only writers
 * of 0x45d708 are LoadedImage::Ctor and LoadedImage::DtorBody, and the only
 * writers of 0x45d71c (SceneTexture's) are that class's own two.
 *
 * Since that one slot is ours, ENDGAME_PLAN's licence applies in full: our
 * ctor and dtor install g_LoadedImageVtable from this DLL, and the game's
 * 0x0045d708 keeps pointing at the UD2-stubbed 0x43de00.  A reader we failed
 * to find therefore faults as c000001d instead of quietly working, and
 * patch.py needs no VTABLE_PATCHES entry.
 *
 * Preserved deliberately:
 *
 *  1. The ctor writes +0x14 BEFORE +0x10, which is neither field order nor
 *     declaration order.  Harmless, and reproduced.
 *  2. DtorBody frees ImageName and does NOT null it — unlike
 *     ReleaseTextureSurfaces above, which nulls it inside the check.  So
 *     running DtorBody twice on the same object is a double free in the
 *     original, and still is here.
 *  3. Both scalar deleting dtors return `this` and free with FactAlloc::Free2
 *     only when bit 0 of the flags is set.  The object itself is NOT ours —
 *     whoever allocated it with the free flag did so on the game's heap — so
 *     this stays an alloc.h site, per "alloc.h retires by attrition".
 *
 * ─── CreatePaletteFromDIBColorTable (0x43ea50) — the TU's last function ───
 *
 * __stdcall(IDirectDraw4 *dd, HBITMAP hbmp) -> IDirectDrawPalette *, ret 8.
 * NOT __thiscall, despite the `mov ecx,esi` at its one call site: ecx is
 * overwritten at the fourth instruction and never read.  scenetexture.cpp's
 * header note already said so; this is the cycle that acts on it.  One CALL
 * site (0x0043fd91, inside our BindTextureResource) and no other reference of
 * any kind, so there is no CALL_PATCHES entry — only a SAFETY_STUB.
 *
 * It reads the DIB's colour table off a scratch DC, rewrites each RGBQUAD in
 * place as a PALETTEENTRY, and hands the result to IDirectDraw4::CreatePalette
 * through the proxy device pointer the caller passed (vtable +0x14) — never by
 * byte-patching the call site, per CLAUDE.md.
 *
 * The byte shuffle is the whole of it, and it is a reversal: GetDIBColorTable
 * writes { b, g, r, reserved } and CreatePalette wants { r, g, b, flags }.  The
 * original does it with two 8-bit register halves and a shift
 * (`mov dl,[eax+1]; mov dh,[eax]; shr esi,0x10; shl edx,8; and esi,0xff;
 * or edx,esi`), which is r | g<<8 | b<<16 with the flags byte left ZERO —
 * `reserved` is shifted out, not copied.
 *
 * Preserved deliberately:
 *
 *  4. The 256-entry scratch buffer is NOT initialised.  Only the local holding
 *     the returned palette is zeroed at entry.  When the DIB has fewer than
 *     256 colours the tail of the buffer is whatever was on the stack, and
 *     CreatePalette reads all 256 for an 8-bit palette.  Left as is.
 *  5. `test edi,edi / jz` returns the zeroed palette pointer when the colour
 *     table is empty — CreatePalette is not called at all.  `jle` then skips
 *     only the conversion loop for a NEGATIVE count and still calls
 *     CreatePalette with an unconverted buffer.  GetDIBColorTable cannot
 *     return negative, so that arm is unreachable; the shape is reproduced
 *     rather than folded into the zero test.
 *  6. The palette flag is chosen by a SIGNED `setg`: DDPCAPS_8BIT (4) when the
 *     count is greater than 16, DDPCAPS_4BIT (1) otherwise.  Written as the
 *     comparison it is, not as the branchless `dec/and 0xfffffffd/add 4`
 *     sequence that computes it.
 *  7. SelectObject's return — the DC's previous bitmap — is discarded and
 *     never restored; the DC is deleted instead.  CreateCompatibleDC's result
 *     is not NULL-checked.
 */
#include "texture.h"
#include "log.h"
#include <stdlib.h>
SceneTexture g_texKaroo128;   /* was 0x004e0408 */
SceneTexture g_texShadow;   /* was 0x004e02c8 */

/* FactAlloc::Free2 — __cdecl(void *), shared helper left live in the binary. */

extern "C" {

/* ─── KAROO_IMAGE_FX / KAROO_IMAGE_DIAG — the ctor/dtor family's controls ──
 *
 * These six functions write no pixels and move no geometry, so the usual
 * "change something you can see" control has nothing to grab.  What they DO
 * own outright is the vtable pointer, and that turns into an unusually direct
 * measurement.
 *
 * `KAROO_IMAGE_FX=gamevtbl` (retired with the game's tables, ENDGAME_PLAN.md
 * "Direction"; its answer is in CONTROLS.md) made the ctors and dtor bodies
 * install the GAME's table address (0x0045d708 / 0x0045d71c) instead of ours.  Those slots point
 * at the UD2-stubbed originals, so under this mode any code that dispatches
 * through the table faults as c000001d.  The control therefore answers a
 * question rather than perturbing a value: a clean run says nothing read the
 * table during it, and a fault says our table is load-bearing and names the
 * reader.  Either outcome is information, which is not true of a control that
 * can only pass.
 *
 * Bounded by construction: it changes one pointer field, touches no geometry,
 * and cannot reach the unguarded bridge/slide spawn scans that crash
 * levelreport.py.
 *
 * `KAROO_IMAGE_DIAG=1` is the census, and it is the part that tells "ran and
 * agreed" from "never ran" — every one of the six announces its first call,
 * and the two scalar deleting dtors are the ones worth watching, since each is
 * reachable only through vtable slot 0.
 *
 * Read by VALUE, never by presence: launch.sh forwards every set KAROO_*. */
static bool image_diag(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = (GetEnvironmentVariableA("KAROO_IMAGE_DIAG", buf, sizeof(buf))
                  && buf[0] == '1') ? 1 : 0;
    }
    return cached != 0;
}

/* First-call announcement plus a running tally.  A purely periodic sample
 * would read zero forever for anything first reached after the last threshold
 * — the LinkedList census's own first draft made exactly that mistake. */
__declspec(dllexport) void Texture_ImageFirstCall(const char *who,
                                                  unsigned long *seen)
{
    if (!image_diag())
        return;
    if ((*seen)++ == 0)
        log_write("texture: DIAG first call -- %s\n", who);
}
#define image_first Texture_ImageFirstCall

/* ─── LoadedImage::CreatePaletteFromDIBColorTable (0x43ea50) ─────────────── */
__declspec(dllexport) IDirectDrawPalette *__stdcall
Texture_CreatePaletteFromDIB(IDirectDraw4 *dd, HBITMAP hbmp)
{
    IDirectDrawPalette *pal = NULL;      /* the one initialised local */
    RGBQUAD table[256];                  /* note 4: deliberately uninitialised */

    HDC dc = CreateCompatibleDC(NULL);   /* note 7: unchecked */
    SelectObject(dc, hbmp);              /* note 7: old bitmap discarded */
    int count = (int)GetDIBColorTable(dc, 0, 256, table);
    DeleteDC(dc);

    if (count == 0)                      /* note 5 */
        return pal;

    if (count > 0) {
        /* BGRX -> RGB0, with `reserved` shifted out rather than copied. */
        DWORD *p = (DWORD *)table;
        for (int i = 0; i < count; ++i) {
            DWORD q = p[i];
            DWORD r = (q >> 16) & 0xff;
            DWORD g = (q >> 8)  & 0xff;
            DWORD b =  q        & 0xff;
            p[i] = r | (g << 8) | (b << 16);
        }
    }

    /* note 6: signed comparison against 16 picks the palette width. */
    DWORD flags = (count > 16) ? DDPCAPS_8BIT : DDPCAPS_4BIT;
    dd->CreatePalette(flags, (LPPALETTEENTRY)table, &pal, NULL);
    if (image_diag())
        log_write("texture: DIAG CreatePaletteFromDIB count=%d flags=%lu pal=%p\n",
                  count, (unsigned long)flags, (void *)pal);
    return pal;
}

/* Forward declaration: the vtable below needs its address. */
__declspec(dllexport) LoadedImage *__attribute__((thiscall))
Texture_ImageScalarDtor(LoadedImage *self, unsigned int flags);

/* Our own one-slot vtable; see the header note for why it is ours. */
static void *const g_LoadedImageVtable[1] = { (void *)&Texture_ImageScalarDtor };

__declspec(dllexport) void *Texture_ImageVtable(void)
{
    return (void *)g_LoadedImageVtable;
}

/* ─── LoadedImage::Ctor (0x43dde0) ─────────────────────────────────────── */
__declspec(dllexport) LoadedImage *__attribute__((thiscall))
Texture_ImageCtor(LoadedImage *self)
{
    static unsigned long seen; image_first("LoadedImage::Ctor", &seen);
    self->unknown00       = Texture_ImageVtable();
    self->pTextureSurface = NULL;
    self->pTexturePalette = NULL;
    self->ImageName       = NULL;
    self->loadedState     = 0;      /* +0x14 first — note 1 */
    self->loadStatus      = 0;      /* +0x10 second */
    return self;
}

/* ─── LoadedImage::DtorBody (0x43de20) ─────────────────────────────────── */
__declspec(dllexport) void __attribute__((thiscall))
Texture_ImageDtorBody(LoadedImage *self)
{
    static unsigned long seen; image_first("LoadedImage::DtorBody", &seen);
    self->unknown00 = Texture_ImageVtable();
    if (self->ImageName != NULL)
        free(self->ImageName);    /* note 2: NOT nulled */
}

/* ─── LoadedImage::ScalarDeletingDtor (0x43de00) ───────────────────────────
 *
 * Reachable ONLY through vtable slot 0 — xref.py finds no CALL and no JMP to
 * it anywhere in the binary.  An E8/E9 scan alone would have missed it, which
 * is the LinkedList::ScalarDestructor lesson repeating. */
__declspec(dllexport) LoadedImage *__attribute__((thiscall))
Texture_ImageScalarDtor(LoadedImage *self, unsigned int flags)
{
    static unsigned long seen; image_first("LoadedImage::ScalarDeletingDtor", &seen);
    Texture_ImageDtorBody(self);
    if ((flags & 1) != 0)
        free(self);               /* note 3 */
    return self;
}

__declspec(dllexport) void __attribute__((thiscall))
Texture_ReleaseSurfaces(LoadedImage *self)
{
    IDirectDrawSurface4 *surf = self->pTextureSurface;
    if (surf != NULL)
        surf->Release();
    self->pTextureSurface = NULL;          /* unconditional */

    IDirectDrawSurface4 *pal = self->pTexturePalette;
    if (pal != NULL)
        pal->Release();
    self->pTexturePalette = NULL;          /* unconditional */

    if (self->ImageName != NULL) {
        free(self->ImageName);
        self->ImageName = NULL;            /* only inside the check */
    }

    self->loadedState = 0;
    self->loadStatus  = 0;
}

__declspec(dllexport) void __attribute__((thiscall))
Texture_ReleaseD3DTexture(SceneTexture *self)
{
    IDirect3DTexture2 *tex = self->pTexture2;
    if (tex != NULL)
        tex->Release();
    self->pTexture2 = NULL;                /* unconditional */

    Texture_ReleaseSurfaces(&self->base);
}

} // extern "C"

/* ─── LoadedImage::Load (0x43eb00) ─────────────────────────────────────────
 *
 * Surface-lost recovery: Restore() the DirectDraw surface, then re-apply
 * whichever loader originally filled it — LoadImageA + BlitDIBToSurface for a
 * BMP/DIB (loadedState 1), ParseTGAFile for a TGA (loadedState 2), nothing
 * otherwise.  __thiscall(this), plain ret, 24 call sites, no vtable refs.
 *
 * Both loaders it dispatches to are now replaced and UD2-stubbed, so this
 * calls TextureDIB_BlitToSurface and TextureTGA_Parse directly rather than
 * the originals at 0x43dfc0 and 0x43e190.
 *
 * Return convention, preserved exactly: a bool in AL with the upper three
 * bytes carrying whatever the last call left there.
 *   - no surface                        -> 0
 *   - Restore failed                    -> hr & 0xffffff00      (AL = 0)
 *   - DIB reload failed                 -> DeleteObject & ~0xff (AL = 0)
 *   - TGA reload failed                 -> ParseTGAFile's value verbatim
 *   - otherwise                         -> (last & 0xffffff00) | 1
 * Note the last case also covers loadedState values other than 1 and 2: the
 * original computes loadedState-2, finds it non-zero, skips the TGA path and
 * still returns true.  That is reproduced rather than turned into a failure.
 */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
TextureDIB_BlitToSurface(LoadedImage *, HANDLE);

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
TextureTGA_Parse(LoadedImage *, LPCSTR);

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Texture_Load(LoadedImage *self)
{
    IDirectDrawSurface4 *surf = self->pTextureSurface;
    if (surf == NULL)
        return 0;

    HRESULT hr = surf->Restore();
    static LONG seen = 0;
    if (InterlockedIncrement(&seen) <= 4)
        log_write("texture: Load this=%p state=%d restore=%08lX name=%s\n",
                  self, self->loadedState, hr,
                  self->ImageName ? self->ImageName : "(null)");
    if (hr < 0)
        return (unsigned int)hr & 0xffffff00u;

    unsigned int last;
    if (self->loadedState == 1) {
        HANDLE h = LoadImageA(GetModuleHandleA(NULL), self->ImageName,
                              IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
        if (h == NULL) {
            h = LoadImageA(NULL, self->ImageName, IMAGE_BITMAP, 0, 0,
                           LR_LOADFROMFILE | LR_CREATEDIBSECTION);
            if (h == NULL)
                return 0;
        }
        unsigned int ok = TextureDIB_BlitToSurface(self, h);
        if ((ok & 0xff) == 0) {
            unsigned int d = (unsigned int)DeleteObject((HGDIOBJ)h);
            return d & 0xffffff00u;
        }
        last = (unsigned int)DeleteObject((HGDIOBJ)h);
    } else {
        last = (unsigned int)(self->loadedState - 2);
        if (last == 0) {
            last = TextureTGA_Parse(self, self->ImageName);
            if ((last & 0xff) == 0)
                return last;
        }
    }

    return (last & 0xffffff00u) | 1u;
}
