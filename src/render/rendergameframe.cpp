/* rendergameframe.cpp -- the per-frame body of WinMain's message loop.
 * In order:
 *
 *   1. a pending level entry: prepare it and return (nothing else runs);
 *   2. timing: now/elapsed in ms, dt against the last tick; when dt > 0,
 *      GameTick, the player/foe poses, the camera (the orbit camera, or the
 *      scripted camera while a spline runs), and the 3D sound listener;
 *   3. clear, BeginScene, the sky, then the opaque passes: the static
 *      placement lists, the destructibles, the player, the grid items, the
 *      bombs, the foes;
 *   4. scene objects, bridges, the player's effect models, the stencil
 *      shadows;
 *   5. the translucent passes (particle effects) when particles are on;
 *   6. the HUD and the menus, EndScene, the flip.
 */
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "rendergameframe.h"
#include "renderdevice.h"
#include "d3dmath.h"
#include "game.h"
#include "levelmap.h"
#include "tile.h"
#include "player.h"
#include "bomb.h"
#include "foe.h"
#include "theme.h"
#include "levelplacements.h"
#include "levelentry.h"
#include "camera.h"
#include "framepose.h"
#include "clock.h"
#include "scriptplayer.h"
#include "soundmanager.h"
#include "cfaktsound.h"
#include "sky.h"
#include "sceneobjects.h"
#include "explodedebris.h"
#include "particles.h"
#include "gametick.h"
#include "breakabletile.h"
#include "entitymath.h"
#include "generators.h"
#include "themedraw.h"
#include "objectshadows.h"
#include "dsoscene.h"
#include "quadbatch.h"
#include "meshbatch.h"
#include "bridgesurf.h"
#include "record.h"
#include "textrenderer.h"
#include "menuscreens.h"
#include "scoreoverlay.h"
#include "gameglobals.h"

/* Every pointer this file takes into the game's packed layouts (camera,
 * focus, placement block, theme records) may be unaligned; x86 reads them
 * without complaint. */
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"

/* ─── Section 2: timing, tick, camera, listener ─────────────────────────── */

/* The scripted camera (Game+0x196086, a running spline): eye from
 * Game+0x13cc94, target from Game+0x2ab580, both copied into the camera
 * globals; pitch and yaw recovered by acos; a plain LookAt with +Y up. */
static void scripted_camera(Game *g, RenderDevice *d3d)
{
    CameraGlobals *cam = &g_camera;
    for (int i = 0; i < 3; ++i) {
        cam->eye[i]    = g->field13cc94(i);
        cam->target[i] = g->cameraEye(i);
    }
    const float dx = cam->target[0] - cam->eye[0];
    const float dy = cam->target[1] - cam->eye[1];
    const float dz = cam->target[2] - cam->eye[2];
    const double horiz2 = (double)dz * dz + (double)dx * dx;
    const double len = sqrt(((double)dy * dy + (double)dz * dz) + (double)dx * dx);
    const double horiz = sqrt(horiz2);
    cam->pitch = (float)acos(horiz2 / (len * horiz));
    cam->yaw   = (float)acos(dz / (horiz * sqrt(1.0)));

    Mat4 view;
    Camera_BuildLookAt(&view, cam->eye[0], cam->eye[1], cam->eye[2],
                       cam->target[0], cam->target[1], cam->target[2],
                       0.0f, 1.0f, 0.0f, 0.0f);
    d3d->SetTransform(Transform::View, &view);
}

/* The listener follows the camera: position = eye, front = target - eye,
 * top = (0, 1, 0, 1) through A*B, where A is identity with
 * m5 = m10 = cos(-pitch), m6 = -sin(-pitch), m9 = sin(-pitch) and B is
 * identity with m0 = m10 = cos(yaw), m2 = sin(yaw), m8 = -sin(yaw); w-divided
 * unless it is exactly 1. */
static void update_listener(Game *g)
{
    CameraGlobals *cam = &g_camera;
    vec3d front = { cam->target[0] - cam->eye[0],
                    cam->target[1] - cam->eye[1],
                    cam->target[2] - cam->eye[2] };

    const float cp = (float)cos(-cam->pitch), sp = (float)sin(-cam->pitch);
    const float cy = (float)cos(cam->yaw),    sy = (float)sin(cam->yaw);
    float a[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    float b[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    a[5] = cp;  a[6] = -sp;  a[9] = sp;  a[10] = cp;
    b[0] = cy;  b[2] = sy;   b[8] = -sy; b[10] = cy;

    float r[16] = { 0 };
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) {
            float acc = r[row * 4 + col];
            for (int k = 0; k < 4; ++k)
                acc = a[row * 4 + k] * b[k * 4 + col] + acc;
            r[row * 4 + col] = acc;
        }

    const float v[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    float o[4] = { 0, 0, 0, 0 };
    for (int col = 0; col < 4; ++col) {
        float acc = o[col];
        for (int k = 0; k < 4; ++k)
            acc = v[k] * r[k * 4 + col] + acc;
        o[col] = acc;
    }
    vec3d top;
    if (o[3] != 1.0f) {
        top.x = o[0] / o[3];
        top.y = o[1] / o[3];
        top.z = o[2] / o[3];
    } else {
        top.x = o[0]; top.y = o[1]; top.z = o[2];
    }

    CFaktSound *snd = g->soundManager()->cfaktSound();
    CFaktSound_SetPosition(snd, (vec3d *)cam->eye, 1);
    CFaktSound_SetOrientation(snd, &front, &top, 1);
    CFaktSound_CommitSettings(snd);
}

/* ─── Section 3: the opaque passes ──────────────────────────────────────── */

static ThemeObjectTypeSlot *slot(ThemeObjectType t)
{
    return &g_themeBlock.slots[t];
}

/* Every model pass goes through RenderSceneObjects with the placement block
 * as its quad template. */
static void rso(const void *pos, const void *rot, unsigned count, ThemeObjectType t,
                double now, float animTime = 0.0f, unsigned animCode = 0,
                unsigned dtMs = 0)
{
    Scene_RenderSceneObjects(Game::instance(), (SceneQuadVertex *)&g_levelPlacements,
                             (const Vec3 *)pos, (const Vec3 *)rot, count, slot(t),
                             g_renderDevice, now, animTime, animCode, dtMs);
}

static void rso_list(const PlacementList &l, ThemeObjectType t, double now)
{
    rso(l.pos, l.rot, l.count, t, now);
}

/* The dying variant of a model: its fx slot's records each light their
 * particle system while the entity's debris latch is set, and start their
 * explosion debris on the latched frame; the latch is then consumed. */
static void arm_fx_records(ThemeObjectTypeSlot *fx, MovableEntity *e)
{
    for (DWORD k = 0; k < fx->dwInstanceCount; ++k) {
        ThemeLevelObject *rec = &fx->records[k];
        if (rec->pParticleSystems[0] != NULL) {
            if (e->debrisPending())
                Particle_EnableRenderNode(rec->pParticleSystems[0]);
            else
                Particle_DisableRenderNode(rec->pParticleSystems[0]);
        }
        if (rec->bExplode && e->debrisPending())
            ExplodeDebris_Begin(&rec->explode, rec->pMesh, 0, rec->flExplodeDir);
    }
    if (e->debrisPending())
        e->clearDebrisPending();
}

/* The item models on the grid, by the cell's contents byte; 2..4, 11, 12
 * and 14..254 draw nothing.  The same table serves the shadow pass. */
static bool item_slot(unsigned char contents, ThemeObjectType *out)
{
    switch (contents) {
    case 1:    *out = THEME_OBJ_CRYSTAL;    return true;
    case 5:    *out = THEME_OBJ_PARAGLIDE;  return true;
    case 6:    *out = THEME_OBJ_TIME;       return true;
    case 7:    *out = THEME_OBJ_LIFE;       return true;
    case 8:    *out = THEME_OBJ_FREEZE;     return true;
    case 9:    *out = THEME_OBJ_AMMUNITION; return true;
    case 10:   *out = THEME_OBJ_SPEED;      return true;
    case 13:   *out = THEME_OBJ_PROTECTION; return true;
    case 0xff: *out = THEME_OBJ_SURPRISE;   return true;
    default:   return false;
    }
}

static const float *pose_pos(FoePose *p) { return (const float *)((BYTE *)p + 0x01); }
static const float *pose_rot(FoePose *p) { return (const float *)((BYTE *)p + 0x0d); }

/* kind 2 is a catcher, 3 a thrower; any other kind draws nothing. */
static bool foe_slot(unsigned char kind, bool dying, ThemeObjectType *out)
{
    if (kind == 2) { *out = dying ? THEME_OBJ_CATCHERFX : THEME_OBJ_CATCHER; return true; }
    if (kind == 3) { *out = dying ? THEME_OBJ_THROWERFX : THEME_OBJ_THROWER; return true; }
    return false;
}

static void set_rs(RS s, uint32_t v)
{
    g_renderDevice->SetRenderState(s, v);
}

static void opaque_passes(Game *g, double now, double elapsed)
{
    LevelPlacements *pl = &g_levelPlacements;
    RenderDevice *d3d = g_renderDevice;

    MeshBatch_Draw(pl, &g_themeBlock, d3d);
    rso_list(pl->switches,    THEME_OBJ_SWITCH,        now);
    rso_list(pl->conveyors,   THEME_OBJ_ICE,           now);
    rso_list(pl->glue,        THEME_OBJ_GLUE,          now);
    rso_list(pl->breakables,  THEME_OBJ_DESTRUCTFIELD, now);
    /* the jump pads animate: a 500 ms cycle, animation code 0x14 */
    rso(pl->jumpPads.pos, pl->jumpPads.rot, pl->jumpPads.count, THEME_OBJ_JUMPPAD, now,
        (float)fmod(now * 0.002, 1.0), 0x14, 0);
    rso_list(pl->teleporters, THEME_OBJ_TELEPORTER,    now);
    QuadBatch_Draw((QuadVerts *)pl, &g_themeBlock, d3d);
    rso_list(pl->ramps,       THEME_OBJ_STAIR,         now);
    LevelPlacements_DrawLifts(g, pl, &g_themeBlock, d3d, now);
    LevelPlacements_DrawSlides(g, pl, &g_themeBlock, d3d, now);

    /* The destructible blocks: whole, or (while the cell's +0x203 is set)
     * the fx model, starting the debris on the cell's +0x20f latch.  Only
     * the first fx draw of the frame gets the elapsed-ms argument.  The
     * counter is a byte. */
    bool firstFx = true;
    for (unsigned char i = 0; i < (unsigned)pl->destructibles.count; ++i) {
        const float *pos = pl->destructibles.pos[i];
        const float *rot = pl->destructibles.rot[i];
        const int v = (unsigned char)(int)(pos[2] * -1.0f);
        Tile *t = g->map()->tile((unsigned char)(int)pos[0], v);
        if (t->field203() == 0) {
            rso(pos, rot, 1, THEME_OBJ_OBSTACLE, now);
            continue;
        }
        ThemeObjectTypeSlot *fx = slot(THEME_OBJ_OBSTACLEFX);
        for (DWORD k = 0; k < fx->dwInstanceCount; ++k) {
            ThemeLevelObject *rec = &fx->records[k];
            if (rec->bExplode && t->field20f())
                ExplodeDebris_Begin(&rec->explode, rec->pMesh, 0, rec->flExplodeDir);
        }
        if (t->field20f())
            t->setField20f(0);
        if (firstFx) {
            rso(pos, rot, 1, THEME_OBJ_OBSTACLEFX, now, 0.0f, 0, (unsigned)(int)elapsed);
            firstFx = false;
        } else {
            rso(pos, rot, 1, THEME_OBJ_OBSTACLEFX, now);
        }
    }

    /* The player: CameraFocus f[2..4] is its position, f[1] its yaw, f[0]
     * its animation time; the animation code is its anim byte. */
    CameraFocus *focus = &g_cameraFocus;
    float playerRot[3] = { 0.0f, focus->f[1], 0.0f };
    rso(&focus->f[2], playerRot, 1, THEME_OBJ_JOHN, now, focus->f[0],
        g->player()->anim(), 0);

    rso_list(pl->climbs, THEME_OBJ_SLIDE, now);
    rso(pl->exitPos, pl->exitRot, 1, THEME_OBJ_EXIT, now);

    /* The items, cell by cell. */
    LevelMap *map = g->map();
    for (unsigned v = 0; v < map->extentV(); ++v)
        for (unsigned u = 0; u < map->extentU(); ++u) {
            Tile *t = map->tile(u, v);
            ThemeObjectType ty;
            if (!item_slot(t->contents(), &ty))
                continue;
            float pos[3] = { (float)u, (float)t->height(), -(float)v };
            float rot[3] = { 0.0f, 0.0f, 0.0f };
            rso(pos, rot, 1, ty, now);
        }

    /* The bombs: whole, or the explosion once it has started dying. */
    for (unsigned char i = 0; i < g->bombCount(); ++i) {
        Bomb *b = g->bombSlot(g->bombId(i));
        float pos[3] = { b->posU(), b->posY(), -b->posV() };
        float rot[3] = { 0.0f, 0.0f, 0.0f };
        if (b->dyingStarted() == 0) {
            rso(pos, rot, 1, THEME_OBJ_BOMB, now);
            continue;
        }
        arm_fx_records(slot(THEME_OBJ_EXPLOSION), b);
        rso(pos, rot, 1, THEME_OBJ_EXPLOSION, now, 0.0f, 0, (unsigned)(int)elapsed);
    }

    /* The foes, from the poses FramePose_Foes wrote. */
    for (unsigned char i = 0; i < g->foeCount(); ++i) {
        Foe *f = g->foeSlot(g->foeId(i));
        FoePose *p = &g_foePoses[i];
        const bool dying = f->dyingStarted() != 0;
        ThemeObjectType ty;
        if (!foe_slot(p->kind, dying, &ty))
            continue;
        if (!dying) {
            rso(pose_pos(p), pose_rot(p), 1, ty, now, p->stepFrac, f->anim(), 0);
            continue;
        }
        arm_fx_records(slot(ty), f);
        rso(pose_pos(p), pose_rot(p), 1, ty, now, 0.0f, 0, (unsigned)(int)elapsed);
    }
}

/* ─── Section 4: scene objects, bridges, effects, shadows ───────────────── */

static void shadow(const void *pos, const void *rot, ThemeObjectType t, double now,
                   float phase, unsigned animKey)
{
    Shadows_DrawObjectShadows(Game::instance(), &g_levelPlacements,
                              (const float *)pos, (const float *)rot, 1, slot(t),
                              g_renderDevice, now, phase, animKey, 0);
}

static void effects_and_shadows(Game *g, double now, double dt)
{
    RenderDevice *d3d = g_renderDevice;
    Scene_DrawSceneObjects(d3d, g_camera.eye,
                           ((DWORD *)&dt)[0], ((DWORD *)&dt)[1], now);
    set_rs(RS::StencilEnable, 0);
    BridgeSurf_Draw(g, &g_themeBlock, d3d, now);

    CameraFocus *focus = &g_cameraFocus;
    float playerRot[3] = { 0.0f, focus->f[1], 0.0f };
    Player *pl = g->player();
    if (pl->effectDActive() != 0 && pl->anim() != 10)
        rso(&focus->f[2], playerRot, 1, THEME_OBJ_PROTECTIONFX, now);
    if (pl->anim() == 5)
        rso(&focus->f[2], playerRot, 1, THEME_OBJ_PARAGLIDEFX, now);

    /* Stencil shadows: a stencil buffer, more than 16 bpp, and the option. */
    if (d3d->hasStencil() && d3d->bitDepth() > 16 &&
        g->videoShadows() != 0) {
        d3d->SetTexture(0, &g_texShadow);
        set_rs(RS::AlphaBlendEnable, 1);
        set_rs(RS::SrcBlend,         Blend::SrcAlpha);
        set_rs(RS::DestBlend,        Blend::InvSrcAlpha);
        set_rs(RS::StencilEnable,    1);
        set_rs(RS::StencilRef,       1);
        set_rs(RS::StencilFunc,      Cmp::Equal);
        set_rs(RS::StencilPass,      StencilOp::DecrSat);

        if (pl->anim() != 10)
            shadow(&focus->f[2], playerRot, THEME_OBJ_JOHN, now, focus->f[0], pl->anim());

        for (unsigned char i = 0; i < g->foeCount(); ++i) {
            Foe *f = g->foeSlot(g->foeId(i));
            FoePose *p = &g_foePoses[i];
            ThemeObjectType ty;
            if (f->dyingStarted() != 0 || !foe_slot(p->kind, false, &ty))
                continue;
            shadow(pose_pos(p), pose_rot(p), ty, now, p->stepFrac, f->anim());
        }

        /* Setting 2 and up also shadows the items. */
        if (g->videoShadows() > 1) {
            LevelMap *map = g->map();
            for (unsigned v = 0; v < map->extentV(); ++v)
                for (unsigned u = 0; u < map->extentU(); ++u) {
                    Tile *t = map->tile(u, v);
                    ThemeObjectType ty;
                    if (!item_slot(t->contents(), &ty))
                        continue;
                    float pos[3] = { (float)u, (float)t->height(), -(float)v };
                    float rot[3] = { 0.0f, 0.0f, 0.0f };
                    shadow(pos, rot, ty, now, 0.0f, 0);
                }
        }
        set_rs(RS::StencilEnable, 0);
    }
    set_rs(RS::ZWriteEnable, 0);
}

/* ─── Section 5: the translucent passes ─────────────────────────────────── */

static void particles(const void *pos, const void *rot, unsigned count, ThemeObjectType t,
                      double now, double elapsed)
{
    Theme_DrawParticleObjects(Game::instance(), &g_levelPlacements,
                              (const float (*)[3])pos, (const float (*)[3])rot, count,
                              slot(t), g_renderDevice, now, elapsed, 0);
}

static void particles_list(const PlacementList &l, ThemeObjectType t, double now, double elapsed)
{
    particles(l.pos, l.rot, l.count, t, now, elapsed);
}

/* A burst system is a slot whose first record is a particle-system object:
 * its bursts[] are spawned at a point and live 1000 ms. */
static ThemeLevelObject *burst_record(ThemeObjectType t)
{
    ThemeLevelObject *rec = &slot(t)->records[0];
    return rec->kind == THEME_KIND_PARTICLESYSTEM ? rec : NULL;
}

/* Spawn into the first free burst (a byte index). */
static void spawn_burst(ThemeLevelObject *rec, float x, float y, float z)
{
    for (unsigned char j = 0; j < rec->dwInstanceCount; ++j) {
        FxBurst *b = &rec->bursts[j];
        if (b->active)
            continue;
        if (rec->pParticleSystems[j] != NULL)
            Particle_EnableRenderNode(rec->pParticleSystems[j]);
        b->msLeft = 1000;
        b->pos[0] = x; b->pos[1] = y; b->pos[2] = z;
        b->active = 1;
        return;
    }
}

static Mat4 translation(const float *p)
{
    Mat4 m;
    m4_identity(&m);
    m.m[12] = p[0]; m.m[13] = p[1]; m.m[14] = p[2];
    return m;
}

enum BurstTick { TICK_WHOLE_MS, TICK_ELAPSED };

/* Draw every live burst: WORLD = translate(pos), tick the
 * system, point it along the view, optionally spin its corners about Y by
 * -rotRateY * now, render, age, and switch the node off again.  A burst
 * whose time is up is retired instead. */
static void draw_bursts(ThemeLevelObject *rec, DWORD src, DWORD dst, BurstTick tick,
                        bool spin, double now, double elapsed)
{
    RenderDevice *dev = g_renderDevice;
    SceneTexture *tex = rec->pSubObjects[0].pTexture;
    if (tex != NULL)
        dev->SetTexture(0, tex);
    set_rs(RS::SrcBlend, src);
    set_rs(RS::DestBlend, dst);
    set_rs(RS::AlphaBlendEnable, 1);

    CameraGlobals *cam = &g_camera;
    for (unsigned char j = 0; j < rec->dwInstanceCount; ++j) {
        FxBurst *b = &rec->bursts[j];
        if (b->msLeft <= 0) {
            b->active = 0;
            continue;
        }
        Mat4 world = translation(b->pos);
        dev->SetTransform(Transform::World, &world);

        ParticleSystem *ps = rec->pParticleSystems[j];
        const int ms = (int)elapsed;
        if (tick == TICK_WHOLE_MS)
            ps_vtick(ps, (float)((double)(unsigned)ms * 0.001f));
        else
            ps_vtick(ps, (float)(elapsed * 0.001));
        ps_vset_vector(ps, cam->target[0] - cam->eye[0],
                       cam->target[1] - cam->eye[1],
                       cam->target[2] - cam->eye[2]);
        if (spin) {
            const double a = -rec->flRotRateY * now;
            const float c = (float)cos(a), s = (float)sin(a);
            Mat4 r;
            m4_identity(&r);
            r.m[0] = c;  r.m[2] = s;  r.m[8] = -s;  r.m[10] = c;
            ps_vtransform_corners(ps, r.m);
        }
        ps_vrender(ps, dev);
        b->msLeft -= (tick == TICK_WHOLE_MS) ? ms : (int)elapsed;
        Particle_DisableRenderNode(ps);
    }
    set_rs(RS::AlphaBlendEnable, 0);
}

static void translucent_passes(Game *g, double now, double elapsed, double dt)
{
    LevelPlacements *pl = &g_levelPlacements;
    CameraFocus *focus = &g_cameraFocus;
    float playerRot[3] = { 0.0f, focus->f[1], 0.0f };

    if (g->videoParticles() != 0) {
        particles(&focus->f[2], playerRot, 1, THEME_OBJ_JOHN, now, elapsed);
        particles_list(pl->switches,      THEME_OBJ_SWITCH,        now, elapsed);
        particles_list(pl->destructibles, THEME_OBJ_OBSTACLE,      now, elapsed);
        particles_list(pl->ramps,         THEME_OBJ_STAIR,         now, elapsed);
        particles_list(pl->conveyors,     THEME_OBJ_ICE,           now, elapsed);
        particles_list(pl->glue,          THEME_OBJ_GLUE,          now, elapsed);
        particles_list(pl->breakables,    THEME_OBJ_DESTRUCTFIELD, now, elapsed);
        particles_list(pl->jumpPads,      THEME_OBJ_JUMPPAD,       now, elapsed);
        particles_list(pl->teleporters,   THEME_OBJ_TELEPORTER,    now, elapsed);
        particles(pl->exitPos, pl->exitRot, 1, THEME_OBJ_EXIT, now, elapsed);
        particles_list(pl->climbs,        THEME_OBJ_SLIDE,         now, elapsed);

        for (unsigned char i = 0; i < g->foeCount(); ++i) {
            Foe *f = g->foeSlot(g->foeId(i));
            FoePose *p = &g_foePoses[i];
            ThemeObjectType ty;
            if (foe_slot(p->kind, f->dyingStarted() != 0, &ty))
                particles(pose_pos(p), pose_rot(p), 1, ty, now, elapsed);
        }

        /* Bombs: the fuse's effects, then the explosion's 2 s after the drop. */
        for (unsigned char i = 0; i < g->bombCount(); ++i) {
            Bomb *b = g->bombSlot(g->bombId(i));
            float pos[3] = { b->posU(), b->posY(), -b->posV() };
            float rot[3] = { 0.0f, 0.0f, 0.0f };
            const bool late = *g->clock() - b->droppedAt() > 2000.0;
            particles(pos, rot, 1, late ? THEME_OBJ_EXPLOSION : THEME_OBJ_BOMB, now, elapsed);
        }

        /* A breakable that fell this tick spawns a destruct-field burst on
         * its cell (the height and -V from the breakable's own cell). */
        ThemeLevelObject *field = burst_record(THEME_OBJ_DESTRUCTFIELDFX);
        for (unsigned char i = 0; i < g->breakableCount(); ++i) {
            BreakableTile *bt = g->breakableSlot(i);
            if (bt->justFell() != 0 && field != NULL)
                spawn_burst(field, (float)bt->cellU(), (float)bt->heightCell(),
                            (float)-bt->cellV());
        }
        if (field != NULL)
            draw_bursts(field, field->pSubObjects[0].dwBlendSrc,
                        field->pSubObjects[0].dwBlendDst, TICK_WHOLE_MS, false, now, elapsed);
    }

    /* Setting 2 and up: pickups and the speed trail. */
    if (g->videoParticles() > 1) {
        Player *pl = g->player();
        ThemeLevelObject *crystal = burst_record(THEME_OBJ_CRYSTALFX);
        if (pl->pickedUp() == 1 && crystal != NULL)
            spawn_burst(crystal, focus->f[2], focus->f[3], focus->f[4]);
        for (unsigned char i = 0; i < g->foeCount(); ++i) {
            Foe *f = g->foeSlot(g->foeId(i));
            if (f->pickedUp() == 1 && crystal != NULL) {
                const float *p = pose_pos(&g_foePoses[i]);
                spawn_burst(crystal, p[0], p[1], p[2]);
            }
        }
        ThemeLevelObject *coll = burst_record(THEME_OBJ_COLLFX);
        if (pl->pickedUp() > 1 && coll != NULL)
            spawn_burst(coll, focus->f[2], focus->f[3], focus->f[4]);

        /* The speed trail: burst 0, live while the speed-up runs and the
         * player moves along (or directly against) its facing. */
        ThemeLevelObject *speed = burst_record(THEME_OBJ_SPEEDFX);
        if (speed != NULL) {
            bool on = false;
            if (pl->effectAActive() != 0) {
                const unsigned char facing = pl->facing();
                on = (unsigned)pl->moveDir() == facing ||
                     (unsigned)pl->moveDir() == Sim_GetTurnedDirection(facing, 2);
            }
            if (on) {
                if (!speed->bursts[0].active) {
                    Particle_EnableRenderNode(speed->pParticleSystems[0]);
                    speed->bursts[0].active = 1;
                }
                speed->bursts[0].pos[0] = focus->f[2];
                speed->bursts[0].pos[1] = focus->f[3];
                speed->bursts[0].pos[2] = focus->f[4];
            } else {
                Particle_DisableRenderNode(speed->pParticleSystems[0]);
                speed->bursts[0].active = 0;
            }
        }

        set_rs(RS::ZWriteEnable, 0);
        if (crystal != NULL)
            draw_bursts(crystal, Blend::One, Blend::One, TICK_WHOLE_MS, false, now, elapsed);
        if (coll != NULL)
            draw_bursts(coll, Blend::One, Blend::One, TICK_ELAPSED, true, now, elapsed);

        if (speed != NULL) {
            ParticleSystem *ps = speed->pParticleSystems[0];
            Generator *gen = Particle_GetGenerator(ps, NULL);
            gen_vset_position(gen, speed->bursts[0].pos[0], speed->bursts[0].pos[1],
                              speed->bursts[0].pos[2]);
            /* (0, 0.1, -1, 1) through the player's yaw: the trail streams
             * out behind. */
            const float c = (float)cos(focus->f[1]), s = (float)sin(focus->f[1]);
            float r[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
            r[0] = c; r[2] = s; r[8] = -s; r[10] = c;
            const float v[4] = { 0.0f, 0.1f, -1.0f, 1.0f };
            float o[4] = { 0, 0, 0, 0 };
            for (int col = 0; col < 4; ++col) {
                float acc = o[col];
                for (int k = 0; k < 4; ++k)
                    acc = v[k] * r[k * 4 + col] + acc;
                o[col] = acc;
            }
            if (o[3] != 1.0f) {
                o[0] /= o[3]; o[1] /= o[3]; o[2] /= o[3];
            }
            gen = Particle_GetGenerator(ps, NULL);
            gen_vset_direction(gen, o[0], o[1], o[2]);

            RenderDevice *dev = g_renderDevice;
            dev->SetTransform(Transform::World, &g_worldIdentity);
            dev->SetTexture(0, speed->pSubObjects[0].pTexture);
            set_rs(RS::SrcBlend, Blend::One);
            set_rs(RS::DestBlend, Blend::One);
            set_rs(RS::AlphaBlendEnable, 1);
            ps_vtick(ps, (float)(elapsed * 0.001));
            CameraGlobals *cam = &g_camera;
            ps_vset_vector(ps, cam->target[0] - cam->eye[0],
                           cam->target[1] - cam->eye[1],
                           cam->target[2] - cam->eye[2]);
            ps_vrender(ps, dev);
            set_rs(RS::AlphaBlendEnable, 0);
        }
    }

    Scene_DrawParticleSystems(g_renderDevice, g_camera.eye, dt, now);

    Player *player = g->player();
    if (player->effectDActive() != 0 && player->anim() != 10)
        particles(&focus->f[2], playerRot, 1, THEME_OBJ_PROTECTIONFX, now, elapsed);
    if (player->anim() == 5)
        particles(&focus->f[2], playerRot, 1, THEME_OBJ_PARAGLIDEFX, now, elapsed);
}

/* ─── Section 6: the HUD, the menus, present ───────────────────────────── */

/* The one text buffer the HUD, frame counter and effect icons share.
 * PRESERVED: an icon row that formats nothing draws whatever the buffer last
 * held -- the previous row, the counter, the HUD's last line, or an earlier
 * frame's. */
static char s_text[0x100];

static ScreenVertex tl(float x, float y, uint32_t c, float u, float v)
{
    ScreenVertex t;
    t.sx = x; t.sy = y; t.sz = 0.0f; t.rhw = 10.0f;
    t.color = c; t.specular = 0; t.tu = u; t.tv = v;
    return t;
}

static void draw_strip(ScreenVertex *q)
{
    g_renderDevice->Draw(Prim::TriangleStrip, VertexFormat::Screen, q, 4, 0);
}

static void blend_on(void)
{
    set_rs(RS::AlphaBlendEnable, 1);
    set_rs(RS::SrcBlend,         Blend::SrcAlpha);
    set_rs(RS::DestBlend,        Blend::InvSrcAlpha);
}

static const SceneTexture *image(ThemeImageSlot s)
{
    return g_themeBlock.images[s];
}

/* The HUD's text positions are integer multiples of the screen width, taken
 * unsigned and scaled by 1/640 (0.0015625). */
static float wx(unsigned w, unsigned k) { return (float)(w * k) * 0.0015625f; }

static void hud_text(TextRenderer *font, int align, float x, float y, float cw, float ch,
                     float spacing, const char *s, char first, DWORD c1, DWORD c2)
{
    RenderDevice *d3d = g_renderDevice;
    if (align < 0)
        Text_RenderText(font, x, y, cw, ch, spacing, s, d3d, first, c1, c2);
    else if (align == 0)
        Text_DrawCentered(font, x, y, cw, ch, spacing, s, d3d, first, c1, c2);
    else
        Text_DrawRightAligned(font, x, y, cw, ch, spacing, s, d3d, first, c1, c2);
}

/* The in-level HUD: the side panels, the vitality needle, the radar and
 * its foe blips, then the timer and counters. */
static void draw_hud(Game *g, unsigned w, unsigned h, float W, float H, float hudH)
{
    RenderDevice *dev = g_renderDevice;
    Player *pl = g->player();

    /* The two corner panels, mirrored halves of the HUD image. */
    if (g_themeBlock.images[THEME_IMG_HUD] != NULL) {
        dev->SetTexture(0, image(THEME_IMG_HUD));
        blend_on();
        const float a  = hudH + hudH;
        const float y1 = hudH - W * 0.003125f;
        ScreenVertex q[4] = {
            tl(a,    0.0f, 0xffffffff, 1.0f, 0.0f),
            tl(a,    y1,   0xffffffff, 1.0f, 0.4921875f),
            tl(0.0f, 0.0f, 0xffffffff, 0.0f, 0.0f),
            tl(0.0f, y1,   0xffffffff, 0.0f, 0.4921875f),
        };
        draw_strip(q);
        ScreenVertex r[4] = {
            tl(W,     0.0f, 0xffffffff, 1.0f, 0.5078125f),
            tl(W,     y1,   0xffffffff, 1.0f, 1.0f),
            tl(W - a, 0.0f, 0xffffffff, 0.0f, 0.5078125f),
            tl(W - a, y1,   0xffffffff, 0.0f, 1.0f),
        };
        draw_strip(r);
        set_rs(RS::AlphaBlendEnable, 0);
        dev->SetTexture(0, nullptr);
    }

    /* The vitality needle: a quad about the origin, turned by
     * 0.9519978 - vitality% * 0.019039957 and moved to (541/640 W, 92/480 H). */
    if (g_themeBlock.images[THEME_IMG_POINTER] != NULL) {
        float p[4][3] = {
            { W * 0.1f,  H * -0.13333334f, 0.0f },
            { W * 0.1f,  H * 0.13333334f,  0.0f },
            { W * -0.1f, H * -0.13333334f, 0.0f },
            { W * -0.1f, H * 0.13333334f,  0.0f },
        };
        const double ang = 0.9519978 - (int)g->vitalityPercent() * 0.019039957;
        const float c = (float)cos(ang), s = (float)sin(ang);
        float m[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        m[0] = c;  m[1] = -s;  m[4] = s;  m[5] = c;
        m[12] = (float)(w * 541u) * 0.0015625f;
        m[13] = (float)(h * 92u) * 0.0020833334f;
        for (int k = 0; k < 4; ++k) {
            const float v[4] = { p[k][0], p[k][1], p[k][2], 1.0f };
            float o[4] = { 0, 0, 0, 0 };
            for (int col = 0; col < 4; ++col) {
                float acc = o[col];
                for (int i = 0; i < 4; ++i)
                    acc = v[i] * m[i * 4 + col] + acc;
                o[col] = acc;
            }
            if (o[3] != 1.0f) { o[0] /= o[3]; o[1] /= o[3]; o[2] /= o[3]; }
            p[k][0] = o[0]; p[k][1] = o[1]; p[k][2] = o[2];
        }
        ScreenVertex q[4] = {
            tl(p[0][0], p[0][1], 0xffffffff, 1.0f, 0.0f),
            tl(p[1][0], p[1][1], 0xffffffff, 1.0f, 1.0f),
            tl(p[2][0], p[2][1], 0xffffffff, 0.0f, 0.0f),
            tl(p[3][0], p[3][1], 0xffffffff, 0.0f, 1.0f),
        };
        for (int k = 0; k < 4; ++k)
            q[k].sz = p[k][2];
        dev->SetTexture(0, image(THEME_IMG_POINTER));
        blend_on();
        draw_strip(q);
        set_rs(RS::AlphaBlendEnable, 0);
        dev->SetTexture(0, nullptr);
    }

    /* The radar, centred at (0.9 W, 0.8667 H). */
    const float cx = W * 0.9f, cy = H * 0.8666667f;
    CameraFocus *focus = &g_cameraFocus;
    Vec3 me = { focus->f[2], focus->f[3], focus->f[4] };
    if (g_themeBlock.images[THEME_IMG_RADAR] != NULL) {
        const float r = hudH * 0.5f;
        me.y = 0.0f;
        ScreenVertex q[4] = {
            tl(cx + r, cy - r, 0xffffffff, 1.0f, 0.0f),
            tl(cx + r, cy + r, 0xffffffff, 1.0f, 1.0f),
            tl(cx - r, cy - r, 0xffffffff, 0.0f, 0.0f),
            tl(cx - r, cy + r, 0xffffffff, 0.0f, 1.0f),
        };
        blend_on();
        dev->SetTexture(0, image(THEME_IMG_RADAR));
        draw_strip(q);
        set_rs(RS::AlphaBlendEnable, 0);
        dev->SetTexture(0, nullptr);
    }

    /* A blip for every foe within 13 cells, turned into the camera's frame
     * (RotY(-yaw)) and scaled by 4; drawn untextured. */
    for (unsigned char i = 0; i < g->foeCount(); ++i) {
        const float *fp = pose_pos(&g_foePoses[i]);
        Vec3 a = { fp[0], 0.0f, fp[2] }, d;
        Math_Vec3Sub(&d, &a, &me);
        if (!(Math_Vec3Length(&d) < 13.0))
            continue;
        Mat4 rot;
        Math_Mat4RotY(&rot, -focus->f[5]);
        Vec3 t;
        Math_Vec3TransformPoint(&t, rot, d);
        Math_Vec3ScaleInPlace(&t, 4.0f);
        const float half = W * 0.0015625f;
        const float bx = t.x + cx, by = cy - t.z;
        ScreenVertex q[4] = {
            tl(bx + half, by - half, 0xffffffff, 1.0f, 0.0f),
            tl(bx + half, by + half, 0xffffffff, 1.0f, 1.0f),
            tl(bx - half, by - half, 0xffffffff, 0.0f, 0.0f),
            tl(bx - half, by + half, 0xffffffff, 0.0f, 1.0f),
        };
        draw_strip(q);
    }

    /* The timer: minutes, seconds and tenths of what is left (or of the
     * limit, outside play); red under ten seconds. */
    LevelMap *map = g->map();
    const unsigned ms = g->state() == 1
        ? (unsigned)map->timeLimit() * 1000u - map->timeElapsed()
        : (unsigned)map->timeLimit() * 1000u;
    const unsigned mins = ms / 60000u;
    const unsigned secs = ms / 1000u - mins * 60u;
    const unsigned tenths = ms / 100u - mins * 600u - secs * 10u;
    char *buf = s_text;
    sprintf(buf, "%02.0f:%02.0f;%d", (double)mins, (double)secs, tenths);
    const DWORD tc = ms > 10000u ? 0xffffffff : 0xffff0000;
    hud_text(&g_fontNumbers, 0, W * 0.5f, 0.0f, W * 0.05f, W * 0.06666667f, 1.0f,
             buf, '0', tc, tc);

    const ThemeTextColorPair &hc = g_themeBlock.textColors[THEME_COLOR_HUD];
    const float cw = W * 0.025f, ch = H * 0.033333335f;
    sprintf(buf, "x%d", pl->lives());
    hud_text(&g_fontMain, -1, wx(w, 490), W * 0.009375f, cw, ch, 0.75f, buf, 0, hc.color1, hc.color2);
    sprintf(buf, "%d/%d", pl->gemsCollected(), map->gemsRequired());
    hud_text(&g_fontMain, -1, wx(w, 10), wx(w, 32), cw, ch, 0.75f, buf, 0, hc.color1, hc.color2);
    sprintf(buf, "x%d", pl->glides());
    hud_text(&g_fontMain, -1, wx(w, 121), wx(w, 38), cw, ch, 0.75f, buf, 0, hc.color1, hc.color2);
    sprintf(buf, "%d", g->levelIndex() + 1);
    hud_text(&g_fontMain, 1, wx(w, 630), wx(w, 23), cw, ch, 0.75f, buf, 0, hc.color1, hc.color2);
    sprintf(buf, "%dx", pl->fieldE8());
    hud_text(&g_fontMain, 0, wx(w, 32), wx(w, 62), cw, ch, 0.75f, buf, 0, hc.color1, hc.color2);
}

/* The frame counter: frames over each second of timeGetTime, shown while
 * F1 is held.  Its three globals stay at their game addresses. */
static float g_fps;
static float g_fpsMark;
static DWORD g_fpsFrames;

static void draw_fps(float W, float H)
{
    const float t = (float)((double)timeGetTime() * 0.001f);
    g_fpsFrames++;
    const double span = (double)t - g_fpsMark;
    if (span > 1.0) {
        const DWORD n = g_fpsFrames;
        g_fpsFrames = 0;
        g_fps = (float)((double)n / span);
        g_fpsMark = t;
    }
    if (hooks_GetAsyncKeyState(VK_F1) & 0x8000) {
        char *buf = s_text;
        sprintf(buf, "%.1f fps", (double)g_fps);
        hud_text(&g_fontMain, 0, W * 0.5f, H * 0.96041667f, W * 0.025f, H * 0.033333335f,
                 0.75f, buf, 0, 0xffffffff, 0xffffffff);
    }
}

/* The captions and the timed-effect icons down the left edge. */
static void draw_messages(Game *g, float W, float H, float pad)
{
    const ThemeTextColorPair &hc = g_themeBlock.textColors[THEME_COLOR_HUD];
    const float cw = W * 0.025f, ch = H * 0.033333335f;
    ScriptPlayer *sp = g->scriptPlayer();
    Player *pl = g->player();

    if (sp->running() != 0) {
        hud_text(&g_fontMain, 0, W * 0.5f, W * 0.0015625f * 3.0f, cw, ch, 0.75f,
                 g->map()->title(), 0, hc.color1, hc.color2);
        if (sp->caption()[0] != '\0')
            Text_DrawPanelText(&g_fontMain, pad, H - pad, cw, ch, 0.75f, H * 0.03750938f,
                               sp->caption(), g_renderDevice, hc.color1, hc.color2,
                               g_themeBlock.images[THEME_IMG_MENU],
                               g_themeBlock.images[THEME_IMG_EDGE]);
    }
    if ((g->state() == 4 && sp->running() == 0) || (g->state() == 1 && pl->moveState() != 0))
        hud_text(&g_fontMain, 0, W * 0.5f, W * 0.0015625f * 232.0f, cw, ch, 0.75f,
                 "...press Enter", 0, hc.color1, hc.color2);
    if (g->state() == 4 && g->map()->bonus() != 0)
        hud_text(&g_fontMain, 0, W * 0.5f, W * 0.0015625f * 200.0f, cw, ch, 0.75f,
                 "BONUSLEVEL", 0, hc.color1, hc.color2);

    /* One icon per active effect, bottom up, with its seconds left.  An
     * effect code without an icon (9), or an icon the theme lacks, still
     * draws the quad and the text -- with whatever texture and text the
     * previous row left. */
    char *buf = s_text;
    const float rowStep = H * 0.06666667f + pad;
    const float xText = W * 0.05f + pad;
    const float textLift = H * 0.016666668f;
    float y = H - ch - pad;
    LinkedListNode *it = List_GetHead(pl->effectList());
    while (it != NULL) {
        const unsigned code = (unsigned char)(DWORD)List_NextValue(pl->effectList(), &it);
        ThemeImageSlot img;
        double start = 0.0, span = 10.0;
        bool icon = true;
        switch (code) {
        case 8:  img = THEME_IMG_FREEZE;         start = pl->effect8Start(); span = 5.0; break;
        case 10: img = THEME_IMG_SPEED;          start = pl->effectAStart(); break;
        case 11: img = THEME_IMG_INVERSECONTROL; start = pl->effectBStart(); break;
        case 12: img = THEME_IMG_SLOWDOWN;       start = pl->effectCStart(); break;
        case 13: img = THEME_IMG_PROTECTION;     start = pl->effectDStart(); break;
        default: icon = false; img = THEME_IMG_HUD; break;
        }
        if (icon && g_themeBlock.images[img] != NULL) {
            g_renderDevice->SetTexture(0, image(img));
            sprintf(buf, "%.1f", span - (*g->clock() - start) * 0.001);
        }
        ScreenVertex q[4] = {
            tl(xText, y - ch, 0x00ffffff, 1.0f, 0.0f),
            tl(xText, y + ch, 0x00ffffff, 1.0f, 1.0f),
            tl(pad,   y - ch, 0x00ffffff, 0.0f, 0.0f),
            tl(pad,   y + ch, 0x00ffffff, 0.0f, 1.0f),
        };
        blend_on();
        draw_strip(q);
        hud_text(&g_fontMain, -1, xText, y - textLift, cw, ch, 0.75f, buf, 0,
                 hc.color1, hc.color2);
        y -= rowStep;
    }
}

/* The Ka'roo logo in the bottom-left corner, on the menus only; the
 * blend and texture state around it are set regardless. */
static void draw_logo(Game *g, float H, float hudH, float pad)
{
    const float xr = pad + hudH, yt = H - hudH - pad, yb = H - pad;
    ScreenVertex q[4] = {
        tl(xr,  yt, 0xffffffff, 1.0f, 0.0f),
        tl(xr,  yb, 0xffffffff, 1.0f, 1.0f),
        tl(pad, yt, 0xffffffff, 0.0f, 0.0f),
        tl(pad, yb, 0xffffffff, 0.0f, 1.0f),
    };
    blend_on();
    g_renderDevice->SetTexture(0, &g_texKaroo128);
    if (g->state() == 0 || g->state() == 5)
        draw_strip(q);
    set_rs(RS::AlphaBlendEnable, 0);
    g_renderDevice->SetTexture(0, nullptr);
}

/* ─── RenderGameFrame ───────────────────────────────────────────────────── */

extern "C" __declspec(dllexport) void __cdecl
Render_RenderGameFrame(void)
{
    Game *g = Game::instance();
    if (g->field_173584() != 0) {
        LevelEntry_PrepareAssets();
        g->setField173584(0);
        return;
    }

    const double prevMs = clock_previous_seconds() * 1000.0;
    const double now = hooks_ClockSeconds() * 1000.0;
    const double elapsed = now - prevMs;
    double dt = now - g_lastTickMs;

    if (dt > 0.0) {
        g_lastTickMs = now;
        Sim_GameTick(g, dt, now);
        if (g->field_173584() != 0)
            return;
        FramePose_Player(g, now, dt, &g_cameraFocus);
        FramePose_Foes(g, now, dt, g_foePoses);
        if (g->scriptPlayer()->splineActive() == 0)
            Camera_UpdateViewTransform(&g_camera, g_renderDevice, g, g_cameraFocus, dt);
        else
            scripted_camera(g, g_renderDevice);
        if (g->soundCreated() != 0)
            update_listener(g);
    }

    RenderDevice *d3d = g_renderDevice;
    /* The sky covers the whole target, so only depth (and stencil) clear. */
    if (d3d->hasStencil())
        set_rs(RS::StencilEnable, 1);
    d3d->ClearDepth();
    if (!d3d->BeginScene())
        return;

    set_rs(RS::StencilEnable,    0);
    set_rs(RS::FogEnable,        0);
    set_rs(RS::SpecularEnable,   0);
    set_rs(RS::AlphaBlendEnable, 0);
    CameraGlobals *cam = &g_camera;
    Sky_DrawSkyBackground(&g_themeBlock.sky, d3d, cam->eye[0], cam->eye[1], cam->eye[2]);
    if (g_themeBlock.bFogEnabled)
        set_rs(RS::FogEnable, 1);
    set_rs(RS::StencilEnable, 1);
    set_rs(RS::StencilFunc,   Cmp::Always);
    set_rs(RS::StencilRef,    1);
    set_rs(RS::StencilZFail,  StencilOp::Keep);
    set_rs(RS::StencilFail,   StencilOp::Keep);
    set_rs(RS::StencilPass,   StencilOp::Replace);

    opaque_passes(g, now, elapsed);
    effects_and_shadows(g, now, dt);
    translucent_passes(g, now, elapsed, dt);

    set_rs(RS::ZWriteEnable,    1);
    set_rs(RS::TextureAddressU, TexAddress::Clamp);
    set_rs(RS::TextureAddressV, TexAddress::Clamp);
    set_rs(RS::ZEnable,         0);

    const unsigned w = d3d->width(), h = d3d->height();
    const float W = (float)w, H = (float)h;
    const float hudH = H * 0.26666668f;
    const unsigned char st = g->state();
    if (st != 6 && st != 2 && st != 0 && st != 5 && st != 3 &&
        g->scriptPlayer()->running() == 0)
        draw_hud(g, w, h, W, H, hudH);
    draw_fps(W, H);

    const float pad = H * 0.00625f;
    if (st != 0 && st != 5 && st != 3)
        draw_messages(g, W, H, pad);
    draw_logo(g, H, hudH, pad);

    const DWORD ms = (DWORD)(long long)now;
    if (st == 0 || st == 5 || st == 3)
        Menu_DispatchGameState(g, &g_themeBlock, d3d, &g_fontMain, ms);
    if (g->state() == 2)
        Score_DrawGameOverScore(g, &g_themeBlock, d3d, &g_fontMain, ms);
    if (g->menu()->node() == 3 || g->state() == 6)
        Score_DrawHighScoreTable(g, &g_themeBlock, d3d, &g_fontMain, ms);

    set_rs(RS::ZEnable, 1);
    d3d->EndScene();
    if (g->state() == 7) {
        if (g->field_0c() != 0)
            g_renderDevice->PresentImage(&g_demoImage);
        return;
    }
    d3d->Flip();
}
