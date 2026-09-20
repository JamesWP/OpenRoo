/* WrapperObject -- the per-mesh texture-coordinate animator.
 *
 * A thirteen-byte object embedded in whatever owns a CFaktMesh: it takes a
 * snapshot of the mesh's texture-coordinate set 0 when the mesh is attached,
 * and thereafter scribbles animated UVs straight into the mesh's vertex
 * array, restoring them from the snapshot when the animation is switched
 * off.  Ghidra calls the class WrapperObject and the name is kept, though
 * "wrapper" describes where it sits rather than what it does -- every one of
 * its methods is about the mesh's UVs.
 *
 * The nine originals (all UD2-stubbed by patch.py):
 *
 *   0x0043f0c0  ctor                 install vtable 0x45d718, zero the rest
 *   0x0043f0e0  scalar deleting dtor vtable slot 0
 *   0x0043f100  dtor body            re-install the table, free the snapshot
 *   0x0043f120  setMesh              attach a mesh and snapshot its UVs
 *   0x0043f1b0  releaseSnapshot      free the snapshot, keep the mesh
 *   0x0043f1d0  flush                copy the snapshot back over the mesh
 *   0x0043f230  applySineWave        mode 4 of RenderSceneObjects
 *   0x0043f2f0  scrollUVs            mode 6 of RenderSceneObjects
 *   0x0043f390  updateObjectTransform mode 5 -- spherical environment map
 *
 * ─── Who drives it ───────────────────────────────────────────────────────
 *
 * Game::RenderSceneObjects 0x0040a1c0 switches on the scene object's
 * animation mode at +0x3e5 and calls exactly one of these per object per
 * frame: mode 0 flush, 4 sine wave, 5 environment map, 6 scroll.  The three
 * animating modes leave the dirty byte set so that a later mode 0 restores
 * the mesh.  setMesh has a single caller, 0x0040d092.
 *
 * ─── THE VTABLE IS OURS ──────────────────────────────────────────────────
 *
 * ENDGAME_PLAN.md's vtable licence, and its condition is met twice over: the
 * table at 0x0045d718 has exactly ONE slot (0x0045d71c starts the next
 * class's), and a byte scan of Karoo.exe.orig for the literal 0x0045d718
 * finds exactly two occurrences -- 0x43f0c6 and 0x43f102, the ctor and the
 * dtor body themselves.  Nothing else in the binary installs it, so every
 * WrapperObject in the game is constructed through our ctor and carries our
 * table; the game's table keeps pointing at a UD2 stub as the tripwire.
 *
 * ─── The layout ──────────────────────────────────────────────────────────
 *
 * Thirteen bytes, packed: the owning container at 0x0043b5d0 places its
 * WrapperObject at +8 and the next member at +0x15.
 */
#pragma once

#include <windows.h>
#include "layout.h"
#include "faktmesh.h"

struct IDirect3DDevice3;

/* One snapshotted texture coordinate pair -- the 8 bytes at +0x18 of an
 * FVF 0x212 vertex, which is the only part of the vertex this class keeps. */
struct WrapperUV {
    float u, v;
};
static_assert(sizeof(WrapperUV) == 8, "WrapperUV stride mismatch");

class __attribute__((packed)) WrapperObject {
public:
    static const int ORIGIN = 0;

    /* 0x0043f0c0 -- install our one-slot table and zero the three fields. */
    void construct();

    /* 0x0043f100 -- re-install the table and free the snapshot. */
    void dtorBody();

    /* 0x0043f120 -- attach `mesh` and snapshot every frame's UVs.  A NULL
     * mesh is ignored entirely, including the free of the old snapshot. */
    void setMesh(CFaktMesh *mesh);

    /* 0x0043f1b0 -- free the snapshot and NULL it.  The mesh pointer and the
     * dirty flag are left alone, which is the original's behaviour and not
     * an oversight: the owning container calls this from its own destructor
     * and never touches the object again. */
    void releaseSnapshot();

    /* 0x0043f1d0 -- restore the mesh's UVs from the snapshot and clear the
     * dirty flag.  Does nothing without a mesh, and nothing when not dirty. */
    void flush();

    /* 0x0043f230 -- the sine-wave warp.  `ticks` is a millisecond-ish
     * counter the caller has already truncated to an integer. */
    void applySineWave(unsigned int ticks, float rate, float amplitude,
                       float skew);

    /* 0x0043f2f0 -- scroll one texture axis.  `axisU` non-zero scrolls u,
     * zero scrolls v. */
    void scrollUVs(unsigned int ticks, int axisU, float speed);

    /* 0x0043f390 -- spherical environment map: generate UVs from each
     * vertex NORMAL transformed by world * view. */
    void updateObjectTransform(IDirect3DDevice3 *dev, unsigned short frame);

    CFaktMesh *mesh() const { return pMesh_; }

private:
    KAROO_LAYOUT_REGISTER(WrapperObject);

    void      *vtable_;   /* +0x00  our one-slot table */
    WrapperUV *pBaseUV_;  /* +0x04  wFrameCount * dwVertexCount pairs */
    CFaktMesh *pMesh_;    /* +0x08  not owned */
    BYTE       dirty_;    /* +0x0c  set by the three animating modes */
};

/* ─── Exports -- patch.py CALL_PATCHES / JMP_PATCHES redirect to these ─── */
extern "C" {

/* Our one-slot table; installed by the ctor and the dtor body. */
__declspec(dllexport) void *Wrapper_Vtable(void);

__declspec(dllexport) WrapperObject *__attribute__((thiscall))
Wrapper_Construct(WrapperObject *self);

/* 0x0043f0e0 -- vtable slot 0.  Returns `this`; bit 0 of `flags` frees. */
__declspec(dllexport) void *__attribute__((thiscall))
Wrapper_ScalarDtor(WrapperObject *self, unsigned int flags);

__declspec(dllexport) void __attribute__((thiscall))
Wrapper_DtorBody(WrapperObject *self);

__declspec(dllexport) void __attribute__((thiscall))
Wrapper_SetMesh(WrapperObject *self, CFaktMesh *mesh);

__declspec(dllexport) void __attribute__((thiscall))
Wrapper_ReleaseSnapshot(WrapperObject *self);

__declspec(dllexport) void __attribute__((thiscall))
Wrapper_Flush(WrapperObject *self);

__declspec(dllexport) void __attribute__((thiscall))
Wrapper_ApplySineWave(WrapperObject *self, unsigned int ticks, float rate,
                      float amplitude, float skew);

__declspec(dllexport) void __attribute__((thiscall))
Wrapper_ScrollUVs(WrapperObject *self, unsigned int ticks, int axisU,
                  float speed);

__declspec(dllexport) void __attribute__((thiscall))
Wrapper_UpdateObjectTransform(WrapperObject *self, IDirect3DDevice3 *dev,
                              unsigned short frame);

} // extern "C"
