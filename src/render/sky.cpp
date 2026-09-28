/* SkyBackground: build the six-faced cube, draw it, and its lifecycle (sky.h).
 *
 * DrawSkyBackground draws the cube with the z-buffer off, under WORLD =
 * RotY(flYawAngle) with the caller's centre as the translation, so the sky
 * follows the viewer.  The centre is a 12-byte struct passed by value after
 * the device; dropping the translation pins the sky at the world origin.
 *
 * KAROO_SKY_FX=noskip draws only the first of the six quads. */

#include "renderdevice.h"
#include "sky.h"
#include "log.h"
#include "scenetexture.h"
#include <stdlib.h>

#include <math.h>

/* SkyBackground is packed for its place in the packed theme block (it holds
 * SceneTextures, which are classes); its members are 4-aligned within it all
 * the same, so taking their addresses is safe. */
 

#define SKY_FVF        VertexFormat::Lit
#define SKY_QUADS      6
#define SKY_LOG_FIRST  8

static bool fx_one_quad(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (GetEnvironmentVariableA("KAROO_SKY_FX", buf, sizeof(buf)))
            cached = (lstrcmpiA(buf, "noskip") == 0);
        log_write("sky: FX mode = %s\n", cached ? "noskip (1 quad only)" : "off");
    }
    return cached != 0;
}

float *SkyBackground::draw(RenderDevice *dev,
                      float flCentreX, float flCentreY, float flCentreZ)
{
    dev->SetRenderState(RS::ZEnable, 0);

    const float c = (float)cos(flYawAngle_);
    const float s = (float)sin(flYawAngle_);

    float *m = WorldMatrix_;
    for (int i = 0; i < 16; i++)
        m[i] = 0.0f;
    // The transpose of the usual D3D Y-rotation: _13 = +sin, _31 = -sin.
    m[0]  = c;   m[2]  = s;
    m[5]  = 1.0f;
    m[8]  = -s;  m[10] = c;
    m[15] = 1.0f;

    // RotY * Translate(centre): RotY's last row is (0,0,0,1) and the
    // translation's 3x3 is identity, so the product is the rotation with the
    // row copied in.
    m[12] = flCentreX;
    m[13] = flCentreY;
    m[14] = flCentreZ;

    dev->SetTransform(Transform::World, (const Mat4 *)m);

    const int nquads = fx_one_quad() ? 1 : SKY_QUADS;
    for (int i = 0; i < nquads; i++) {
        const SceneTexture *tex = &Textures_[i];
        dev->SetTexture(0, tex);
        bool ok = dev->Draw(Prim::TriangleStrip, SKY_FVF, QuadVerts_[i], 4,
                            DrawFlag::NoUpdateExtents);

        static LONG logged = 0;
        if (InterlockedIncrement(&logged) <= SKY_LOG_FIRST)
            log_write("sky: quad %d tex=%p yaw=%d/1000 -> ok=%d\n",
                      i, (void *)tex, (int)(flYawAngle_ * 1000.0f), ok);
    }

    dev->SetRenderState(RS::ZEnable, 1);
    return WorldMatrix_;
}

/* ─── SkyBackground::buildFromFaceNames ─────────────────────────────────
 *
 * The cube is +-55 on each axis.  Each face is a 4-vertex strip with UVs (1,0)
 * (1,1) (0,0) (0,1); the corners below are in vertex order.  Every vertex is
 * white with a black, opaque-alpha specular.  The matrix is identity until
 * DrawSkyBackground rebuilds it. */
static const signed char kSkyCorners[24][3] = {
    {-1, 1,-1}, { 1, 1,-1}, {-1, 1, 1}, { 1, 1, 1},  // UP
    { 1,-1,-1}, {-1,-1,-1}, { 1,-1, 1}, {-1,-1, 1},  // DN
    {-1, 1,-1}, {-1,-1,-1}, { 1, 1,-1}, { 1,-1,-1},  // FR
    { 1, 1, 1}, { 1,-1, 1}, {-1, 1, 1}, {-1,-1, 1},  // BK
    {-1, 1, 1}, {-1,-1, 1}, {-1, 1,-1}, {-1,-1,-1},  // LF
    { 1, 1,-1}, { 1,-1,-1}, { 1, 1, 1}, { 1,-1, 1},  // RT
};
static const float kSkyUV[4][2] = { {1, 0}, {1, 1}, {0, 0}, {0, 1} };

/* Identity matrix and the cube's 24 vertices, shared by the ctor and
 * BuildFromFaceNames.  flYawAngle and each vertex's `reserved` are left alone.
 */
void SkyBackground::skyFillGeometry()
{
    for (int i = 0; i < 16; i++)
        WorldMatrix_[i] = (i % 5 == 0) ? 1.0f : 0.0f;

    for (int f = 0; f < 6; f++) {
        for (int k = 0; k < 4; k++) {
            SkyVertex *v = &QuadVerts_[f][k];
            v->diffuse  = 0xffffffff;
            v->specular = 0xff000000;
            v->u = kSkyUV[k][0];
            v->v = kSkyUV[k][1];
            v->x = 55.0f * kSkyCorners[f * 4 + k][0];
            v->y = 55.0f * kSkyCorners[f * 4 + k][1];
            v->z = 55.0f * kSkyCorners[f * 4 + k][2];
        }
    }
}

unsigned int SkyBackground::buildFromFaceNames(RenderDevice *dev, const char *up, const char *dn,
                       const char *fr, const char *bk, const char *lf,
                       const char *rt, UINT bpp)
{
    skyFillGeometry();

    for (int f = 0; f < 6; f++)
        Textures_[f].releaseD3DTexture();

    const char *names[6] = { up, dn, fr, bk, lf, rt };
    unsigned int r = 0;
    for (int f = 0; f < 6; f++) {
        r = Textures_[f].selectTextureLoader(dev, names[f], bpp, 0);
        if ((r & 0xff) == 0)
            return r;
    }
    // Only the low byte is the result; the upper bytes are the last loader's.
    return (r & 0xffffff00u) | 1u;
}

/* ─── The lifecycle ─────────────────────────────────────────────────────────
 *
 * The one instance is ThemeAssetBlock::sky, built and destroyed by the block's
 * aggregate ctor/dtor (theme.cpp).  The dtor releases the six face textures
 * last to first. */
static void *const g_SkyVtable[1] = { (void *)&SkyBackground::scalarDtor };

SkyBackground *SkyBackground::construct()
{
    for (int f = 0; f < 6; f++)
        Textures_[f].construct();
    pVtable_ = g_SkyVtable;
    skyFillGeometry();
    return this;
}

void SkyBackground::dtorBody()
{
    pVtable_ = g_SkyVtable;
    for (int f = 6; f-- > 0; )
        Textures_[f].dtorBody();
}

SkyBackground * 
SkyBackground::scalarDtor(SkyBackground *self, unsigned int flags)
{
    self->dtorBody();
    if (flags & 1)
        free(self);
    return self;
}
