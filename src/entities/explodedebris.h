/* ExplodeDebris: the theme "explode" effect.  A model is shattered into its
 * triangles, which fly outward and thin out.  RenderGameFrame begins it when
 * an object explodes; RenderSceneObjects advances and draws it, and
 * DrawObjectShadows draws its shadow, the debris flattened like any model.
 *
 * The object is embedded in ThemeLevelObject. */

#pragma once

#include <windows.h>
#include "faktmesh.h"
class RenderDevice;

struct ExplodeDebris {

    void         *vtable;                // the one-slot vtable
    MeshVertex   *pVertexCopy;           // nVertexCount vertices
    float       (*pFaceRecords)[3];      // a velocity per triangle
    int           nVertexCount;          // as it was when the buffers were built
    DWORD         bActive;               // set by begin; cleared by the ctor and release
    float         samples[30];  // Gaussian speeds, mu 2.0, sigma 1.0; refilled whole
    DWORD         cursor;                // reset by the same refill
    int           nLiveVertices;         // begin sets it; advance drops it by threes
    float         flDropAccum;           // fractional triangles owed
    float         flExplodeScaledCount;  // the drop rate: nVertexCount * arg / 300

};

/* The one-slot vtable, installed by the ctor and the dtor body. */
void *ExplodeDebris_Vtable(void);

ExplodeDebris *ExplodeDebris_Construct(ExplodeDebris *self);

/* Vtable slot 0.  Returns self; bit 0 of flags frees. */
void *ExplodeDebris_ScalarDtor(ExplodeDebris *self, unsigned int flags);

void ExplodeDebris_DtorBody(ExplodeDebris *self);

/* Frees the two scratch buffers.  See the .cpp for the field it does not
 * clear. */
void ExplodeDebris_Release(ExplodeDebris *self);

/* The theme loader's "explode": size the buffers, and store the drop rate (the
 * keyword's third argument). */
void
ExplodeDebris_AllocateExplodeBuffers(ExplodeDebris *self, struct CFaktMesh *mesh);
void ExplodeDebris_StoreExplodeScaledCount(ExplodeDebris *self, float scale);

/* Copies one mesh frame and gives every triangle a velocity.  Returns 1, or 0
 * if the frame, the vertex count or the buffer is wrong. */
int ExplodeDebris_Begin(ExplodeDebris *self, struct CFaktMesh *mesh,
                        unsigned short frame, const float *origin);

/* Advances by dt: moves the live vertices, then drops whole triangles. */
void ExplodeDebris_Advance(ExplodeDebris *self, float dt);

/* Draws the live triangles twice (SRCALPHA, then DESTALPHA); 0x800401f0 if
 * inactive. */
HRESULT ExplodeDebris_Draw(ExplodeDebris *self, RenderDevice *dev);

