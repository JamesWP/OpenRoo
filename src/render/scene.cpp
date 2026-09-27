/* scene.cpp -- the level's scene-object list (scene.h), and the camera's
 * segment test against its models. */

#include <math.h>
#include <new>
#include "scene.h"
#include "renderdevice.h"
#include "extraobjects.h"
#include "particles.h"
#include "ani.h"
Scene g_scene;

/* ─── Construction and teardown ─────────────────────────────────────────── */

Scene *Scene::construct()
{
    objects_.init();
    models_.construct();
    textures_.construct();
    return this;
}

/* Members in reverse.  The objects themselves are not freed -- only the list's
 * nodes, by List_Destruct. */
void Scene::destruct()
{
    textures_.destruct();
    models_.destruct();
    objects_.destruct();
}

void Scene::freeSceneObjects()
{
    for (LinkedListNode *n = objects_.head(); n != NULL; ) {
        SceneObject *o = (SceneObject *)n->value();
        n = n->next();
        if (o != NULL) {
            o->spline.destruct();
            ::operator delete(o);
        }
    }
    objects_.clear();
    models_.clearReleaseFree();
    textures_.releaseAll();
    memset(this, 0, sizeof *this);  // PRESERVED: vtable pointers too; nothing reads them again
}

/* ─── BuildSceneObjectList ───────────────────────────────────────────────
 *
 * One SceneObject per .leo record that is not a sound.  Position and the third
 * rotation are Z-negated on the way in, as are the spline points, whose file
 * order is (x, z, y) -- the record stores y at [2]. */
void Scene::buildObjectList(RenderDevice *d3d, ExtraObjects *leo, GameLogger *logger)
{
    freeSceneObjects();
    models_.setLogger(logger);
    textures_.setLogger(logger);

    for (unsigned i = 0; i < leo->objectCount(); ++i) {
        ExtraObjectRecord *r = leo->record(i);
        if (r->kind == EXTRA_SOUND)
            continue;

        // PRESERVED: the new object is not zeroed.  Only the fields its kind
        // uses are written, so a billboard's mesh/particle pointers and a
        // model's radius are whatever the heap left, and animLoaded is stale
        // unless the .ani loads.
        SceneObject *o = (SceneObject *)::operator new(sizeof(SceneObject));
        o->spline.construct();

        o->type = r->kind;
        if (r->kind == EXTRA_MODEL)
            o->mesh = models_.findOrImport(r->file);
        else if (r->kind == EXTRA_PARTICLE)
            o->particle = ps_load_file(r->file, logger);
        else if (r->kind == EXTRA_BILLBOARD)
            o->billboardRadius = r->billboardSize;

        if (r->animationFile[0] != 0
            && (char)o->anim.load(r->animationFile, logger))
            o->animLoaded = 1;

        // Blend modes 5/6 (SRCALPHA / INVSRCALPHA) ask for an alpha surface.
        DWORD alpha = (r->srcBlend == 5 || r->srcBlend == 6) ? 1 : 0;
        o->texture = textures_.getOrLoad(d3d, r->textureFile,
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
            o->spline.purgeControlPoints();
            for (unsigned k = 0; k < r->splinePointCount; ++k)
                o->spline.addControlPoint(r->splinePoints[k][0],
                                       r->splinePoints[k][2],
                                       -r->splinePoints[k][1]);
        }
        objects_.append(o);
    }
}

/* ─── The camera's segment test ─────────────────────────────────────────────
 *
 * UpdateViewTransform asks whether the segment P..P+D touches any model.  Each
 * static model's box -- the mesh's first frame record, min at [0..2] and max
 * at [3..5] -- is turned a quarter-turn about X, made axis-aligned again,
 * moved to the object, and tested against the segment with the usual six-axis
 * separating test.  Objects on a spline are skipped.
 *
 * Every test is written `!(sum >= |x|)` so that a NaN compare misses. */
static const float  K_HALF = 0.5f;
static const double K_BOX_TURN = 1.5707963705062866;

/* Row vector times the X rotation [5]=c [6]=-s [9]=s [10]=c; w stays 1. */
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
    // PRESERVED: a constant quarter-turn, not the object's own rotation; the
    // box comes out right only because the turn is exactly 90 degrees.
    float c = (float)cos(K_BOX_TURN), s = (float)sin(K_BOX_TURN);

    const float *box = (const float *)o->mesh->frameRecords();
    float a[3], b[3];
    rot_x(a, box, c, s);
    rot_x(b, box + 3, c, s);

    // Low/high per axis; on a tie the first corner is the low one.
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

    // PRESERVED: the face-axis terms scale by |D|/2 * |D.i| rather than
    // |D.i|/2, which is correct only for a unit direction.
    for (int i = 0; i < 3; ++i)
        if (!(halfLen * ad[i] + h[i] >= fabsf(diff[i])))
            return false;
    // Edge axes: D x each box axis.
    if (!(h[1] * ad[2] + h[2] * ad[1] >= fabsf(d[2] * diff[1] - diff[2] * d[1])))
        return false;
    if (!(h[0] * ad[2] + h[2] * ad[0] >= fabsf(diff[2] * d[0] - d[2] * diff[0])))
        return false;
    if (!(h[0] * ad[1] + h[1] * ad[0] >= fabsf(d[1] * diff[0] - diff[1] * d[0])))
        return false;
    return true;
}

/* Only kind 0 (model) objects are tested.  PRESERVED: no null check on the
 * list's values. */
int Scene::segmentHitsModel(float px, float py, float pz,
                       float dx, float dy, float dz)
{
    const float p[3] = { px, py, pz }, d[3] = { dx, dy, dz };
    for (LinkedListNode *n = objects_.head(); n != NULL; n = n->next()) {
        const SceneObject *o = (const SceneObject *)n->value();
        if (o->type == EXTRA_MODEL && segment_hits_object(o, p, d))
            return 1;
    }
    return 0;
}
