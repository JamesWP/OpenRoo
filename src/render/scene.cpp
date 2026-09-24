/* scene.cpp -- the level's scene-object list, ENDGAME_PLAN.md E4.  See
 * scene.h for the address map.
 *
 * ─── Defects and oddities preserved deliberately ──────────────────────────
 *
 * 1. FreeSceneObjects ends by zeroing all 0x40 bytes of the Scene -- the
 *    three vtable pointers included.  Nothing reads them again (the dtor
 *    re-installs its own, and the list works from zero), so it is harmless,
 *    but it is what the original does.
 * 2. A new SceneObject is not zeroed.  Only the fields its kind uses are
 *    written: a billboard's mesh/particle pointers, and a model's radius,
 *    are whatever the heap left there.  animLoaded is written only on
 *    success, so it too is stale unless the .ani loads.
 * 3. The segment test scales its three face-axis terms by |D|/2 * |D.i|
 *    rather than |D.i|/2 -- correct only for a unit direction.  Kept.
 * 4. The model's box is rotated a quarter-turn about X (a constant, not the
 *    object's own rotation) and then axis-aligned again, so its extents come
 *    out right only because the turn is exactly 90 degrees.
 */
#include <math.h>
#include <new>
#include "scene.h"
#include "direct3d.h"
#include "extraobjects.h"
#include "particles.h"
#include "ani.h"

/* ─── Construction and teardown ─────────────────────────────────────────── */

extern "C" __declspec(dllexport) Scene *__attribute__((thiscall))
Scene_Construct(Scene *self)
{
    List_Init(&self->objects);
    ModelManager_Construct(&self->models);
    TextureManager_Construct(&self->textures);
    return self;
}

/* Members in reverse.  The objects themselves are not freed -- only the
 * list's nodes, by List_Destruct. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Scene_Destruct(Scene *self)
{
    TextureManager_Destruct(&self->textures);
    ModelManager_Destruct(&self->models);
    List_Destruct(&self->objects);
}

/* ─── FreeSceneObjects 0x00420ee0 ───────────────────────────────────────── */

static void free_scene_objects()
{
    Scene *s = GG_SCENE;
    for (LinkedListNode *n = s->objects.pHead; n != NULL; ) {
        SceneObject *o = (SceneObject *)n->pValue;
        n = n->pNextNode;
        if (o != NULL) {
            Spline_Destruct(&o->spline);           /* 0x420f40 */
            ::operator delete(o);
        }
    }
    List_Clear(&s->objects);
    ModelManager_ClearReleaseFree(&s->models);
    TextureManager_ReleaseAll(&s->textures);
    memset(s, 0, sizeof *s);                       /* defect 1 */
}

/* ─── BuildSceneObjectList 0x00420c50 ─────────────────────────────────────
 *
 * One SceneObject per .leo record that is not a sound.  Position and the
 * third rotation are Z-negated on the way in, as are the spline points,
 * whose file order is (x, z, y) -- the record stores y at [2]. */
extern "C" __declspec(dllexport) void __cdecl
Scene_BuildObjectList(Direct3D *d3d, ExtraObjects *leo, GameLogger *logger)
{
    Scene *s = GG_SCENE;
    free_scene_objects();
    s->models.pLogger = logger;
    s->textures.pLogger = logger;

    for (unsigned i = 0; i < leo->objectCount(); ++i) {
        ExtraObjectRecord *r = leo->record(i);
        if (r->kind == EXTRA_SOUND)
            continue;

        SceneObject *o = (SceneObject *)::operator new(sizeof(SceneObject));
        Spline_Construct(&o->spline);

        o->type = r->kind;
        if (r->kind == EXTRA_MODEL)
            o->mesh = ModelManager_FindOrImport(&s->models, r->file);
        else if (r->kind == EXTRA_PARTICLE)
            o->particle = ps_load_file(r->file, logger);
        else if (r->kind == EXTRA_BILLBOARD)
            o->billboardRadius = r->billboardSize;

        if (r->animationFile[0] != 0
            && (char)Ani_LoadAnimationFile((AnimTable *)o->anim,
                                           r->animationFile, logger))
            o->animLoaded = 1;

        /* Blend modes 5/6 (SRCALPHA / INVSRCALPHA) ask for an alpha surface. */
        DWORD alpha = (r->srcBlend == 5 || r->srcBlend == 6) ? 1 : 0;
        o->texture = TextureManager_GetOrLoad(&s->textures, d3d->pDD4,
                                              d3d->pDevice, r->textureFile,
                                              alpha, 0, 0);
        o->srcBlend  = r->srcBlend;
        o->destBlend = r->destBlend;
        o->textureAddress = r->textureAddress != 0 ? r->textureAddress : 1;

        o->pos[0] =  r->position[0];
        o->pos[1] =  r->position[2];
        o->pos[2] = -r->position[1];
        o->rot[0] =  r->field_10c[0];
        o->rot[1] =  r->field_10c[2];
        o->rot[2] = -r->field_10c[1];

        o->onPath     = r->splineMode != 0;
        o->lit        = r->lit;
        o->splineMode = r->splineMode;
        if (r->splineMode != 0) {
            o->splineTime = r->splineTime;
            Spline_PurgeControlPoints(&o->spline);
            for (unsigned k = 0; k < r->splinePointCount; ++k)
                Spline_AddControlPoint(&o->spline, r->splinePoints[k][0],
                                       r->splinePoints[k][2],
                                       -r->splinePoints[k][1]);
        }
        List_Append(&s->objects, o);
    }
}

/* ─── The camera's segment test, 0x00423240 / 0x00422b90 ──────────────────
 *
 * UpdateViewTransform (0x404a34) asks whether the segment P..P+D touches any
 * model.  Each static model's box -- the mesh's first frame record, min at
 * [0..2] and max at [3..5] -- is turned a quarter-turn about X, made
 * axis-aligned again, moved to the object, and tested against the segment
 * with the usual six-axis separating test.  Objects on a spline are skipped.
 *
 * The original rotates with x87 FCOS/FSIN of a double pi/2 and runs every
 * comparison in extended precision.  Written here in float/double C: the
 * rotation's cosine is ~-4e-8, far below any box extent, and a result could
 * only differ for a segment grazing a face to the last bit.  What is kept
 * exactly is which way each comparison goes, NaN included: every test is
 * `!(sum >= |x|)` so an unordered compare misses, as FCOMPP + TEST AH,1 does.
 */
static const float  K_HALF = 0.5f;                       /* 0x45d318 */
static const double K_BOX_TURN = 1.5707963705062866;     /* 0x45d348 */

/* Row vector times the X rotation [5]=c [6]=-s [9]=s [10]=c.  w stays 1, so
 * the original's divide-by-w branch never runs. */
static void rot_x(float out[3], const float v[3], float c, float s)
{
    out[0] = v[0];
    out[1] = v[1] * c + v[2] * s;
    out[2] = v[1] * -s + v[2] * c;
}

static bool segment_hits_object(const SceneObject *o, const float p[3],
                                const float d[3])
{
    if (o->onPath != 0)
        return false;

    float mid[3];
    for (int i = 0; i < 3; ++i)
        mid[i] = p[i] + d[i] * K_HALF;
    float halfLen = (float)sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) * K_HALF;
    float c = (float)cos(K_BOX_TURN), s = (float)sin(K_BOX_TURN);

    const float *box = (const float *)o->mesh->pFrameRecords;
    float a[3], b[3];
    rot_x(a, box, c, s);
    rot_x(b, box + 3, c, s);

    /* Low/high per axis; on a tie the first corner is the low one. */
    float lo[3], hi[3];
    for (int i = 0; i < 3; ++i) {
        if (b[i] < a[i]) { lo[i] = b[i]; hi[i] = a[i]; }
        else             { lo[i] = a[i]; hi[i] = b[i]; }
    }

    float h[3], diff[3];
    for (int i = 0; i < 3; ++i) {
        h[i] = (hi[i] - lo[i]) * K_HALF;
        diff[i] = (h[i] + lo[i] + o->pos[i]) - mid[i];
    }

    float ad[3] = { fabsf(d[0]), fabsf(d[1]), fabsf(d[2]) };

    /* Face axes (defect 3). */
    for (int i = 0; i < 3; ++i)
        if (!(halfLen * ad[i] + h[i] >= fabsf(diff[i])))
            return false;
    /* Edge axes: D x each box axis. */
    if (!(h[1] * ad[2] + h[2] * ad[1] >= fabsf(d[2] * diff[1] - diff[2] * d[1])))
        return false;
    if (!(h[0] * ad[2] + h[2] * ad[0] >= fabsf(diff[2] * d[0] - d[2] * diff[0])))
        return false;
    if (!(h[0] * ad[1] + h[1] * ad[0] >= fabsf(d[1] * diff[0] - diff[1] * d[0])))
        return false;
    return true;
}

/* Only kind 0 (model) objects are tested; no null check on the list's
 * values, as the original. */
extern "C" __declspec(dllexport) int __cdecl
Scene_SegmentHitsModel(float px, float py, float pz,
                       float dx, float dy, float dz)
{
    const float p[3] = { px, py, pz }, d[3] = { dx, dy, dz };
    for (LinkedListNode *n = GG_SCENE->objects.pHead; n != NULL; n = n->pNextNode) {
        const SceneObject *o = (const SceneObject *)n->pValue;
        if (o->type == EXTRA_MODEL && segment_hits_object(o, p, d))
            return 1;
    }
    return 0;
}
