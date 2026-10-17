/* The one walk every theme look is drawn through (themedraw.h): for each
 * record, its render state (specular when it asks and highlights are on,
 * depth writes unless it says not); for each sub-object, its visibility
 * condition, texture, address mode and blend; for each instance, the draw its
 * record's kind calls for.
 *
 * Matrices are row-major with row vectors (v * M), so a placement composes
 * left to right: scale, then rotation, then translation. */
#include <math.h>
#include "themedraw.h"
#include "particletypes.h"
#include <vector>
#include "renderdevice.h"
#include "texture.h"
#include "d3dmath.h"
#include "ani.h"
#include "animcode.h"
#include "animatedmesh.h"
#include "particles.h"
#include "generators.h"
#include "explodedebris.h"

namespace draw {
namespace {

const float HALF_PI     = 1.5707964f;
const float PHASE_SCALE = 4.0f;     // a random phase's share of a record's bob
const float LIGHT_OFF   = 1000.0f;  // the shadow light, this far off along (-1, +1, -1)
const float SHADOW_LIFT = 0.02f;    // shadows sit this far above the ground
const float QUAD_HALF   = 0.4f;     // a turning field's UV radius
const int   BURST_MS    = 1000;     // a burst's life

// A particle record's movableType: how its system follows an instance.
enum { MOVE_TRANSLATE = 1, MOVE_GENERATOR = 2 };

/* The unit tile-top quad a field record draws at an instance. */
const FieldVertex TILE_QUAD[4] = {
    {  0.5f, 0.0f, -0.5f, 0, 0.0f, 1.0f },
    { -0.5f, 0.0f, -0.5f, 0, 0.0f, 0.0f },
    {  0.5f, 0.0f,  0.5f, 0, 1.0f, 1.0f },
    { -0.5f, 0.0f,  0.5f, 0, 1.0f, 0.0f },
};
/* Its colour: white, the alpha nearly clear, as the game drew it. */
const uint32_t TILE_QUAD_DIFFUSE = 0x0fffffff;

/* a * b. */
Mat4 mul(const Mat4 &a, const Mat4 &b)
{
    Mat4 d;
    m4_mul(&d, &b, &a);  // m4_mul(d, x, y) is y * x
    return d;
}

/* The placements of one particle draw; kept to save allocating each frame. */
std::vector<ParticleInstance> &scratch_instances()
{
    static std::vector<ParticleInstance> at;
    return at;
}

Mat4 identity()
{
    Mat4 m;
    m4_identity(&m);
    return m;
}

Mat4 translation(float x, float y, float z)
{
    Mat4 m;
    m4_translate(&m, x, y, z);
    return m;
}

Mat4 rot_y(float a)
{
    Mat4 m;
    m4_rot_y(&m, a);
    return m;
}

Vec3 view_dir(const Frame &f)
{
    return { f.target.x - f.eye.x, f.target.y - f.eye.y, f.target.z - f.eye.z };
}

/* Whether a sub-object gated on `gate` shows where `cond` holds. */
bool visible(uint32_t gate, uint8_t cond)
{
    switch (gate) {
    case 0: return true;
    case 1: return (cond & COND_ACTIVE) != 0;
    case 2: return (cond & COND_ACTIVE) == 0;
    case 3: return (cond & COND_DEAD) != 0;
    case 4: return (cond & COND_DEAD) == 0;
    case 5: return (cond & COND_GLIDING) != 0;
    case 6: return (cond & COND_PROTECTED) != 0;
    default: return false;
    }
}

/* A record's per-record render state, and its undoing. */
void begin_record(Frame &f, const ThemeLevelObject *rec)
{
    f.dev->SetSpecular(rec->specular() != 0 && f.opts.highlights);
    DepthState depth = f.dev->depth();
    depth.write = rec->noZWrite() == 0;
    f.dev->SetDepth(depth);
}

void end_record(Frame &f)
{
    f.dev->SetSpecular(false);
}

/* A sub-object's texture, address mode and blend. */
void bind(Frame &f, const SceneSubObject &sub)
{
    const AddressMode addr = addressFromTheme(sub.dwTexAddress);
    f.dev->SetSamplerAddress(0, addr, addr);
    bindTexture(f.dev, 0, sub.pTexture);
    f.dev->SetBlend(blendFromTheme(sub.dwBlendSrc, sub.dwBlendDst));
}

/* Whether a sub-object draws at all: environment mapping needs the option. */
bool sub_enabled(const Frame &f, const SceneSubObject &sub)
{
    return sub.effect != SUBOBJ_EFFECT_ENVIRONMENT || f.opts.reflection;
}

/* The record's bob, added to y. */
float bob(const ThemeLevelObject *rec, const Instance &in, double now, float y)
{
    const float amp = rec->oscillationAmplitude();
    if (amp == 0.0f || amp != amp)
        return y;
    double a = rec->oscillationFrequency() * now + rec->oscillationPhase();
    if (rec->oscillateRandom() != 0)
        a += in.phase * PHASE_SCALE;
    return (float)(sin(a) * amp + y);
}

/* Where a record draws at an instance: its scale (swelling with its pump),
 * its own spin (from its random start, if it has one) plus the instance's
 * rotation, and the instance's position plus the record's bobbing offset.
 * Models are stood up from their files' Z-up by tiltX, a quarter turn. */
Mat4 placement(const ThemeLevelObject *rec, const Instance &in, double now,
               Vec3 scale, float tiltX)
{
    const float *pump = rec->pump();
    if (pump[3] != 0.0f && pump[3] == pump[3]) {
        const double w = (sin(pump[3] * now) + 1.0) * 0.5;
        scale = { (float)(w * pump[0] + scale.x), (float)(w * pump[1] + scale.y),
                  (float)(w * pump[2] + scale.z) };
    }
    const float start = rec->randomYAngle() != 0 ? in.phase : 0.0f;
    const float ax = (float)(now * rec->rotRateX()) + in.rot.x + tiltX;
    const float ay = (float)(now * rec->rotRateY()) + start + in.rot.y;
    const float az = (float)(now * rec->rotRateZ()) + in.rot.z;

    Mat4 m = identity();
    m.m[0] = scale.x;  m.m[5] = scale.y;  m.m[10] = scale.z;
    Mat4 r;
    m4_rot_x(&r, ax);  m = mul(m, r);
    m4_rot_y(&r, ay);  m = mul(m, r);
    m4_rot_z(&r, az);  m = mul(m, r);
    return mul(m, translation(rec->posX() + in.pos.x,
                              bob(rec, in, now, rec->posY()) + in.pos.y,
                              rec->posZ() + in.pos.z));
}

Mat4 model_placement(const ThemeLevelObject *rec, const Instance &in, double now)
{
    return placement(rec, in, now, { rec->scaleX(), rec->scaleY(), rec->scaleZ() }, HALF_PI);
}

/* The model frame: a record without move states loops its walk on the
 * clock; any other plays the instance's animation at its time. */
unsigned model_frame(ThemeLevelObject *rec, const Instance &in, double now)
{
    if (rec->noMoveStates() != 0)
        return (unsigned)AnimSlot::frameOnClock(rec->animTable().lookup(ANIM_WALK_FORWARD), now);
    return (unsigned)AnimSlot::frameAtPhase(rec->animTable().lookup(in.animCode), in.animTime);
}

/* An exploding record draws its debris instead of its mesh. */
void draw_debris(Frame &f, ThemeLevelObject *rec)
{
    if (rec->debrisDue(f.now))
        rec->explodeDebris().advance((float)(f.elapsed * 0.001));
    rec->explodeDebris().draw(f.dev);
}

void draw_model(Frame &f, ThemeLevelObject *rec, const SceneSubObject &sub, const Instance &in)
{
    f.dev->SetWorld(model_placement(rec, in, f.now));
    const unsigned frame = model_frame(rec, in, f.now);

    const float *p = sub.flEffectParams;
    UVAnimator &uv = rec->uvAnimator();
    switch (sub.effect) {
    case SUBOBJ_EFFECT_NONE:        uv.flush(); break;
    case SUBOBJ_EFFECT_WOBBLE:      uv.applySineWave((unsigned)(long long)f.now, p[0], p[1], p[2]); break;
    case SUBOBJ_EFFECT_ENVIRONMENT: uv.updateObjectTransform(f.dev, (unsigned short)frame); break;
    case SUBOBJ_EFFECT_SCROLL:      uv.scrollUVs((unsigned)(long long)f.now, p[0] != 0.0f, p[1]); break;
    default: break;
    }

    if (rec->explodes() != 0)
        draw_debris(f, rec);
    else if (rec->lit() != 0)
        rec->mesh()->drawFramedModel(f.dev, frame);
    else
        rec->mesh()->drawMeshBuffer(f.dev, frame);
}

void draw_billboard(Frame &f, const ThemeLevelObject *rec, const Instance &in)
{
    const Vec3 d = view_dir(f);
    Vec3 corner[4];
    Math_BuildBillboardQuad(corner, d.x, d.y, d.z, rec->billboardScale());
    static const float UV[4][2] = { { 0, 1 }, { 0, 0 }, { 1, 1 }, { 1, 0 } };
    BbVertex v[4];
    for (int i = 0; i < 4; i++)
        billboard_vertex(&v[i], &corner[i], 0x00ffffff, 0, UV[i][0], UV[i][1]);
    f.dev->SetWorld(translation(rec->posX() + in.pos.x, bob(rec, in, f.now, rec->posY()) + in.pos.y,
                                rec->posZ() + in.pos.z));
    f.dev->Draw(Prim::TriangleStrip, VertexFormat::Lit, v, 4, 0);
}

/* (x, y) through M, recentred on (0.5, 0.5): a turning field's UV corner. */
void turned_corner(const Mat4 &M, float x, float y, float *u, float *v)
{
    float o[4];
    for (int j = 0; j < 4; j++)
        o[j] = x * M.m[j] + y * M.m[4 + j] + M.m[12 + j];
    if (o[3] != 1.0f) {
        o[0] /= o[3];
        o[1] /= o[3];
    }
    *u = o[0] + 0.5f;
    *v = o[1] + 0.5f;
}

/* The tile-top quad's UVs (and colour), animated by the sub-object's
 * effect.  Fields read the effect numbers their own way: the parser's
 * `flash` is a scroll in one of four directions, `pulse` a grey flash,
 * `turn` the UVs spinning about the centre, `wobble` about a pivot. */
void animate_field(FieldVertex q[4], const SceneSubObject &sub, double now)
{
    const float *p = sub.flEffectParams;
    switch (sub.effect) {
    case SUBOBJ_EFFECT_FLASH: {
        const float s = (float)fmod(p[1] * now, 1.0) * p[0];
        static const float BASE[4][4][2] = {
            { { 0, 1 }, { 0, 0 }, { 1, 1 }, { 1, 0 } },   // along u
            { { 0, 0 }, { 0, 0 }, { 1, 0 }, { 1, 0 } },   // along v
            { { 0, 1 }, { 0, 0 }, { 1, 1 }, { 1, 0 } },   // diagonal
            { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 } },   // the other diagonal
        };
        static const float STEP[4][4][2] = {
            { { -1, 0 }, { -1, 0 }, { -1, 0 }, { -1, 0 } },
            { { 0, 1 }, { 0, -1 }, { 0, 1 }, { 0, -1 } },
            { { -1, 1 }, { 0, 0 }, { 0, 0 }, { 1, -1 } },
            { { 0, 0 }, { -1, 1 }, { 1, -1 }, { 0, 0 } },
        };
        const int dir = (int)p[2] - 1;
        if (dir < 0 || dir > 3)
            break;
        for (int i = 0; i < 4; i++) {
            q[i].tu = BASE[dir][i][0] + STEP[dir][i][0] * s;
            q[i].tv = BASE[dir][i][1] + STEP[dir][i][1] * s;
        }
        break;
    }
    case SUBOBJ_EFFECT_PULSE: {
        const uint32_t b = (uint32_t)((sin(p[0] * now + p[1]) + 1.0) * 0.5 * 255.0) & 0xff;
        for (int i = 0; i < 4; i++)
            q[i].diffuse = 0xff000000u | (b << 16) | (b << 8) | b;
        break;
    }
    case SUBOBJ_EFFECT_TURN: {
        const float c = (float)cos(p[0] * now), s = (float)sin(p[0] * now);
        Mat4 M = identity();
        M.m[0] = c;  M.m[1] = -s;  M.m[4] = s;  M.m[5] = c;
        turned_corner(M, -QUAD_HALF,  QUAD_HALF, &q[0].tu, &q[0].tv);
        turned_corner(M, -QUAD_HALF, -QUAD_HALF, &q[1].tu, &q[1].tv);
        turned_corner(M,  QUAD_HALF,  QUAD_HALF, &q[2].tu, &q[2].tv);
        turned_corner(M,  QUAD_HALF, -QUAD_HALF, &q[3].tu, &q[3].tv);
        break;
    }
    case SUBOBJ_EFFECT_WOBBLE: {
        const float s = (float)sin(p[0] * now), c = (float)cos(p[0] * now);
        const float pv = p[2], qv = p[1];
        const float A = (qv - pv) * s;
        const float B = (pv + qv) * s + 1.0f;
        const float C = c * qv + pv * s;
        const float D = (c * qv + 1.0f) - pv * s;
        q[0].tu = A;  q[0].tv = D;
        q[1].tu = A;  q[1].tv = C;
        q[2].tu = B;  q[2].tv = D;
        q[3].tu = B;  q[3].tv = C;
        break;
    }
    default:
        break;
    }
}

void draw_field(Frame &f, const SceneSubObject &sub, const Instance &in, const FieldVertex *own)
{
    FieldVertex q[4];
    if (own) {
        for (int i = 0; i < 4; i++)
            q[i] = own[i];
        f.dev->SetWorld(identity());
    } else {
        for (int i = 0; i < 4; i++) {
            q[i] = TILE_QUAD[i];
            q[i].diffuse = TILE_QUAD_DIFFUSE;
        }
        animate_field(q, sub, f.now);
        f.dev->SetWorld(translation(in.pos.x, in.pos.y, in.pos.z));
    }
    f.dev->Draw(Prim::TriangleStrip, VertexFormat::Diffuse1, q, 4, 0);
}

/* The planar shadow matrix for a light LIGHT_OFF off along (-1, +1, -1) from
 * p, onto the ground plane through p. */
Mat4 shadow_matrix(const Vec3 &p)
{
    const float lx = p.x - LIGHT_OFF, ly = p.y + LIGHT_OFF, lz = p.z - LIGHT_OFF;
    const float d = -p.y;
    Mat4 m;
    m.m[0]  = ly;        m.m[1]  = 0.0f;      m.m[2]  = 0.0f;      m.m[3]  = 0.0f;
    m.m[4]  = -lx;       m.m[5]  = 0.0f;      m.m[6]  = -lz;       m.m[7]  = -1.0f;
    m.m[8]  = 0.0f;      m.m[9]  = 0.0f;      m.m[10] = ly;        m.m[11] = 0.0f;
    m.m[12] = -(d * lx); m.m[13] = -(ly * d); m.m[14] = -(lz * d); m.m[15] = ly - d;
    return m;
}

/* A look whose first record is a particle system holds bursts. */
ThemeLevelObject *burst_record(Frame &f, ThemeObjectType look)
{
    ThemeLevelObject *rec = &f.theme->slot(look)->records()[0];
    return rec->kind() == THEME_KIND_PARTICLESYSTEM ? rec : nullptr;
}

}  // namespace

void beginPass(Frame &f, Pass pass)
{
    f.dev->SetSpecular(false);
    f.dev->SetDepth(DepthState{ true, pass != Pass::Particles });
    StencilState st;
    switch (pass) {
    case Pass::Opaque:
        st = { true, CompareFunc::Always, 1, StencilOp::Keep, StencilOp::Keep, StencilOp::Replace };
        break;
    case Pass::Shadows:
        st = { true, CompareFunc::Equal, 1, StencilOp::Keep, StencilOp::Keep, StencilOp::DecrSat };
        bindTexture(f.dev, 0, f.shadow);
        f.dev->SetBlend(BlendState::alpha());
        break;
    default:
        break;   // stencil off
    }
    f.dev->SetStencil(st);
}

void objects(Frame &f, ThemeObjectType look, const Instance *inst, unsigned n,
             const FieldVertex (*quads)[4])
{
    ThemeObjectTypeSlot *slot = f.theme->slot(look);
    for (uint32_t r = 0; r < slot->instanceCount(); r++) {
        ThemeLevelObject *rec = &slot->records()[r];
        const ThemeObjectKind kind = rec->kind();
        if (kind != THEME_KIND_MODEL && kind != THEME_KIND_BILLBOARD && kind != THEME_KIND_FIELD)
            continue;
        begin_record(f, rec);
        for (uint32_t s = 0; s < rec->subObjectCount(); s++) {
            const SceneSubObject &sub = rec->subObjects()[s];
            if (!sub_enabled(f, sub))
                continue;
            for (unsigned k = 0; k < n; k++) {
                if (!visible(sub.dwVisibilityGate, inst[k].conditions))
                    continue;
                bind(f, sub);
                if (kind == THEME_KIND_MODEL)
                    draw_model(f, rec, sub, inst[k]);
                else if (kind == THEME_KIND_BILLBOARD)
                    draw_billboard(f, rec, inst[k]);
                else
                    draw_field(f, sub, inst[k], quads ? quads[k] : nullptr);
            }
        }
        end_record(f);
    }
}

void fieldBuffer(Frame &f, ThemeObjectType look, const VertexBuffer *vb, unsigned vertices)
{
    if (vb == nullptr || vertices == 0)
        return;
    ThemeObjectTypeSlot *slot = f.theme->slot(look);
    for (uint32_t r = 0; r < slot->instanceCount(); r++) {
        ThemeLevelObject *rec = &slot->records()[r];
        if (rec->kind() != THEME_KIND_FIELD)
            continue;
        begin_record(f, rec);
        for (uint32_t s = 0; s < rec->subObjectCount(); s++) {
            bind(f, rec->subObjects()[s]);
            f.dev->SetWorld(identity());
            f.dev->DrawBuffer(Prim::TriangleList, vb, 0, vertices);
        }
        end_record(f);
    }
}

void shadows(Frame &f, ThemeObjectType look, const Instance *inst, unsigned n)
{
    f.dev->SetSpecular(false);
    ThemeObjectTypeSlot *slot = f.theme->slot(look);
    for (uint32_t r = 0; r < slot->instanceCount(); r++) {
        ThemeLevelObject *rec = &slot->records()[r];
        if (rec->kind() != THEME_KIND_MODEL || rec->noShadow() != 0)
            continue;
        DepthState depth = f.dev->depth();
        depth.write = rec->noZWrite() == 0;
        f.dev->SetDepth(depth);
        for (uint32_t s = 0; s < rec->subObjectCount(); s++) {
            const SceneSubObject &sub = rec->subObjects()[s];
            for (unsigned k = 0; k < n; k++) {
                if (!visible(sub.dwVisibilityGate, inst[k].conditions))
                    continue;
                Mat4 m = mul(model_placement(rec, inst[k], f.now), shadow_matrix(inst[k].pos));
                f.dev->SetWorld(mul(m, translation(0.0f, SHADOW_LIFT, 0.0f)));
                if (rec->explodes() != 0)
                    draw_debris(f, rec);
                else
                    rec->mesh()->drawMeshBuffer(f.dev, model_frame(rec, inst[k], f.now));
            }
        }
        end_record(f);
    }
}

void particles(Frame &f, ThemeObjectType look, const Instance *inst, unsigned n)
{
    const Vec3 view = view_dir(f);
    ThemeObjectTypeSlot *slot = f.theme->slot(look);
    for (uint32_t r = 0; r < slot->instanceCount(); r++) {
        ThemeLevelObject *rec = &slot->records()[r];
        ParticleSystem *ps = rec->particleSystems()[0];
        if (rec->kind() != THEME_KIND_PARTICLESYSTEM || ps == nullptr)
            continue;
        if (rec->particlesDue(f.now))
            ps->tick((float)(f.elapsed * 0.001));
        f.dev->SetSpecular(rec->specular() != 0 && f.opts.highlights);

        // Every instance a sub-object shows at is one placement of one draw.
        ps->setVector(view.x, view.y, view.z);
        Generator *gen = ps->getGenerator(NULL);
        for (uint32_t s = 0; s < rec->subObjectCount(); s++) {
            const SceneSubObject &sub = rec->subObjects()[s];
            std::vector<ParticleInstance> &at = scratch_instances();
            at.clear();
            for (unsigned k = 0; k < n; k++) {
                const Instance &in = inst[k];
                if (!visible(sub.dwVisibilityGate, in.conditions))
                    continue;
                const float spinY = (float)(rec->rotRateY() * f.now)
                                  + (rec->randomYAngle() != 0 ? in.phase : 0.0f) + in.rot.y;
                const Vec3 pos = { rec->posX(), bob(rec, in, f.now, rec->posY()), rec->posZ() };
                // Turn the camera-facing corners back against the world's spin.
                ParticleInstance pi = { identity(), rot_y(-spinY) };
                if (rec->movableType() == MOVE_TRANSLATE) {
                    pi.world = translation(in.pos.x, in.pos.y, in.pos.z);
                    if (gen)
                        gen->setPosition(pos.x, pos.y, pos.z);
                } else if (rec->movableType() == MOVE_GENERATOR) {
                    // The particles stay where they were emitted: only the
                    // generator moves (to the last instance, if several).
                    pi.corners = identity();
                    if (gen)
                        gen->setPosition(pos.x + in.pos.x, pos.y + in.pos.y, pos.z + in.pos.z);
                } else {
                    pi.world = placement(rec, in, f.now, { 1.0f, 1.0f, 1.0f }, 0.0f);
                }
                at.push_back(pi);
            }
            if (at.empty())
                continue;
            bind(f, sub);
            ps->render(f.dev, at.data(), (uint32_t)at.size());
        }
        end_record(f);
    }
}

void fireEffects(Frame &f, ThemeObjectType look, bool fire)
{
    ThemeObjectTypeSlot *slot = f.theme->slot(look);
    for (uint32_t r = 0; r < slot->instanceCount(); r++) {
        ThemeLevelObject *rec = &slot->records()[r];
        if (ParticleSystem *ps = rec->particleSystems()[0]) {
            if (fire)
                ps->enableRenderNode();
            else
                ps->disableRenderNode();
        }
    }
}

void explode(Frame &f, ThemeObjectType look)
{
    ThemeObjectTypeSlot *slot = f.theme->slot(look);
    for (uint32_t r = 0; r < slot->instanceCount(); r++) {
        ThemeLevelObject *rec = &slot->records()[r];
        if (rec->explodes() != 0)
            rec->explodeDebris().begin(rec->explodeDir());
    }
}

void spawnBurst(Frame &f, ThemeObjectType look, const Vec3 &at)
{
    ThemeLevelObject *rec = burst_record(f, look);
    if (rec == nullptr)
        return;
    for (uint32_t j = 0; j < rec->instanceCount(); ++j) {
        FxBurst *b = &rec->bursts()[j];
        if (b->active)
            continue;
        if (rec->particleSystems()[j] != nullptr)
            rec->particleSystems()[j]->enableRenderNode();
        b->msLeft = BURST_MS;
        b->pos[0] = at.x;  b->pos[1] = at.y;  b->pos[2] = at.z;
        b->active = 1;
        return;
    }
}

void bursts(Frame &f, ThemeObjectType look, const BlendState *blend)
{
    ThemeLevelObject *rec = burst_record(f, look);
    if (rec == nullptr)
        return;
    const Vec3 view = view_dir(f);
    const SceneSubObject &sub = rec->subObjects()[0];
    bindTexture(f.dev, 0, sub.pTexture);
    f.dev->SetBlend(blend ? *blend
                          : BlendState::on(blendFactorFromTheme(sub.dwBlendSrc),
                                           blendFactorFromTheme(sub.dwBlendDst)));
    for (uint32_t j = 0; j < rec->instanceCount(); ++j) {
        FxBurst *b = &rec->bursts()[j];
        ParticleSystem *ps = rec->particleSystems()[j];
        if (b->msLeft <= 0 || ps == nullptr) {
            b->active = 0;
            continue;
        }
        f.dev->SetWorld(translation(b->pos[0], b->pos[1], b->pos[2]));
        ps->tick((float)(f.elapsed * 0.001));
        ps->setVector(view.x, view.y, view.z);
        if (rec->rotRateY() != 0.0f)
            ps->transformCorners(rot_y((float)(-rec->rotRateY() * f.now)).m);
        ps->render(f.dev);
        b->msLeft -= (int)f.elapsed;
        ps->disableRenderNode();
    }
    f.dev->SetBlend(BlendState::off());
}

void trail(Frame &f, ThemeObjectType look, bool on, const Vec3 &at, float yaw)
{
    ThemeLevelObject *rec = burst_record(f, look);
    ParticleSystem *ps = rec ? rec->particleSystems()[0] : nullptr;
    if (ps == nullptr)
        return;
    FxBurst &b = rec->bursts()[0];
    if (on) {
        if (!b.active) {
            ps->enableRenderNode();
            b.active = 1;
        }
        b.pos[0] = at.x;  b.pos[1] = at.y;  b.pos[2] = at.z;
    } else {
        ps->disableRenderNode();
        b.active = 0;
    }

    if (Generator *gen = ps->getGenerator(NULL)) {
        gen->setPosition(b.pos[0], b.pos[1], b.pos[2]);
        // (0, 0.1, -1) turned by yaw: streaming out behind.
        gen->setDirection(sinf(yaw), 0.1f, -cosf(yaw));
    }
    const Vec3 view = view_dir(f);
    f.dev->SetWorld(identity());
    bindTexture(f.dev, 0, rec->subObjects()[0].pTexture);
    f.dev->SetBlend(BlendState::additive());
    ps->tick((float)(f.elapsed * 0.001));
    ps->setVector(view.x, view.y, view.z);
    ps->render(f.dev);
    f.dev->SetBlend(BlendState::off());
}

}  // namespace draw
