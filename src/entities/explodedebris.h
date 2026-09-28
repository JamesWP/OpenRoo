/* ExplodeDebris: the theme "explode" effect.  A model is shattered into its
 * triangles, which fly outward and thin out.  RenderGameFrame begins it when
 * an object explodes; RenderSceneObjects advances and draws it, and
 * DrawObjectShadows draws its shadow, the debris flattened like any model.
 *
 * The object sits at ThemeLevelObject+0x15, 0x9c bytes up to the next field
 * the theme loader writes.  The 30-float speed table and its cursor tile
 * exactly: 0x14 + 30 * 4 is 0x8c. */

#pragma once

#include <windows.h>
#include "layout.h"
class RenderDevice;

class __attribute__((packed)) ExplodeDebris {
public:
    static const int ORIGIN = 0;

    /* The one-slot vtable, installed by the ctor and the dtor body. */
    static void *vtbl();

    ExplodeDebris *construct();

    /* Vtable slot 0.  Returns self; bit 0 of flags frees. */
    static void * __attribute__((thiscall))
    scalarDtor(ExplodeDebris *self, unsigned int flags);

    void dtorBody();

    /* Frees the two scratch buffers.  See the .cpp for the field it does not
     * clear. */
    void release();

    /* The theme loader's "explode": size the buffers, and store the drop rate
     * (the keyword's third argument). */
    void allocateExplodeBuffers(struct CFaktMesh *mesh);

    void storeExplodeScaledCount(float scale);

    /* Copies one mesh frame and gives every triangle a velocity.  Returns 1, or
     * 0 if the frame, the vertex count or the buffer is wrong. */
    int begin(struct CFaktMesh *mesh, unsigned short frame,
              const float *origin);

    /* Advances by dt: moves the live vertices, then drops whole triangles. */
    void advance(float dt);

    /* Draws the live triangles twice (SRCALPHA, then DESTALPHA); 0x800401f0 if
     * inactive. */
    HRESULT draw(RenderDevice *dev);

    void         *vtable() const { return vtable_; }
    int vertexCount() const { return nVertexCount_; }
    DWORD active() const { return bActive_; }
    /* 4-aligned in practice; the class is packed only for its embedders. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    float *samples() { return samples_; }
#pragma GCC diagnostic pop
    DWORD cursor() const { return cursor_; }
    void  setCursor(DWORD c) { cursor_ = c; }
    float explodeScaledCount() const { return flExplodeScaledCount_; }

private:
    float *debrisVertex(int i);
    float *debrisVelocity(int tri);

    void         *vtable_;                // +0x00  the one-slot vtable
    void         *pVertexCopy_;           // +0x04  nVertexCount * 0x28 (FVF 0x212)
    void         *pFaceRecords_;  // +0x08  a velocity per triangle, (nVertexCount / 3) * 0xc
    int           nVertexCount_;          // +0x0c  as it was when the buffers were built
    DWORD         bActive_;               // +0x10  set by begin; cleared by the ctor and release
    float         samples_[30];  // +0x14  Gaussian speeds, mu 2.0, sigma 1.0; refilled whole
    DWORD         cursor_;                // +0x8c  reset by the same refill
    int           nLiveVertices_;         // +0x90  begin sets it; advance drops it by threes
    float         flDropAccum_;           // +0x94  fractional triangles owed
    float         flExplodeScaledCount_;  // +0x98  the drop rate: nVertexCount * arg / 300
    KAROO_LAYOUT_REGISTER(ExplodeDebris);
};

KAROO_LAYOUT_CHECKS(ExplodeDebris)
{
    KAROO_LAYOUT_AT(pVertexCopy_,  0x04);
    KAROO_LAYOUT_AT(pFaceRecords_, 0x08);
    KAROO_LAYOUT_AT(nVertexCount_, 0x0c);
    KAROO_LAYOUT_AT(samples_,      0x14);
    KAROO_LAYOUT_AT(cursor_,       0x8c);
    KAROO_LAYOUT_AT(nLiveVertices_, 0x90);
    KAROO_LAYOUT_AT(flDropAccum_,  0x94);
    KAROO_LAYOUT_AT(flExplodeScaledCount_, 0x98);
    KAROO_LAYOUT_SIZE(0x9c);
}

