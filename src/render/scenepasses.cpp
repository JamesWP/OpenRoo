/* scenepasses.cpp -- the frame's scene passes: the opaque placement and object
 * passes, the object effects and stencil shadows, and the translucent particle
 * passes.  RenderGameFrame calls them in that order. */
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "scenepasses.h"
#include "prof.h"
#include "renderdevice.h"
#include "renderstates.h"
#include "d3dmath.h"
#include "texture.h"
#include "world.h"
#include "camerarig.h"
#include "config.h"
#include "levelmap.h"
#include "tile.h"
#include "player.h"
#include "bomb.h"
#include "foe.h"
#include "fallingtile.h"
#include "theme.h"
#include "levelplacements.h"
#include "camera.h"
#include "framepose.h"
#include "entitymath.h"
#include "generators.h"
#include "particles.h"
#include "themedraw.h"
#include "objectshadows.h"
#include "dsoscene.h"
#include "quadbatch.h"
#include "meshbatch.h"
#include "bridgesurf.h"
#include "sceneobjects.h"
#include "renderer.h"

/* ─── Section 3: the opaque passes ──────────────────────────────────────── */

static ThemeObjectTypeSlot *slot(ThemeAssetBlock *theme, ThemeObjectType t)
{
    return theme->slot(t);
}

/* Every model pass goes through RenderSceneObjects with the placement block
 * as its quad template. */
/* The profile's names for the models, in ThemeObjectType order. */
static const char *const OBJECT_NAMES[THEME_OBJ_COUNT] = {
    "john", "catcher", "catcher fx", "thrower", "thrower fx", "plate", "side", "platform",
    "paraglide", "paraglide fx", "elevator", "exit", "glue", "destruct field",
    "destruct field fx", "jump pad", "slide", "stair", "teleporter", "crystal",
    "crystal fx", "ammunition", "bomb", "explosion", "surprise", "freeze", "speed",
    "speed fx", "collect fx", "life", "switch", "time", "ice", "obstacle", "obstacle fx",
    "protection", "protection fx", "bridge",
};

static void rso(Renderer *renderer, World *world, CameraPose *cam, Config *config, const void *pos, const void *rot,
                unsigned count, ThemeObjectType t, double now, float animTime = 0.0f,
                unsigned animCode = 0, unsigned dtMs = 0)
{
    Scene_RenderSceneObjects(world, cam, config,
                             renderer->levelPlacements.tileQuad(),
                             (const Vec3 *)pos, (const Vec3 *)rot, count, slot(&renderer->themeBlock, t),
                             renderer->device, now, animTime, animCode, dtMs);
}

static void rso_list(Renderer *renderer, World *world, CameraPose *cam, Config *config, const PlacementList &l,
                     ThemeObjectType t, double now)
{
    rso(renderer, world, cam, config, l.pos(), l.rot(), l.count(), t, now);
}

/* The dying variant of a model: its fx slot's records each light their
 * particle system while the entity's debris latch is set, and start their
 * explosion debris on the latched frame; the latch is then consumed. */
static void arm_fx_records(ThemeObjectTypeSlot *fx, MovableEntity *e)
{
    for (uint32_t k = 0; k < fx->instanceCount(); ++k) {
        ThemeLevelObject *rec = &fx->records()[k];
        if (rec->particleSystems()[0] != NULL) {
            if (e->debrisPending())
                (rec->particleSystems()[0])->enableRenderNode();
            else
                (rec->particleSystems()[0])->disableRenderNode();
        }
        if (rec->explodes() && e->debrisPending())
            rec->explodeDebris().begin(rec->mesh(), 0, rec->explodeDir());
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

/* kind 2 is a catcher, 3 a thrower; any other kind draws nothing. */
static bool foe_slot(unsigned char kind, bool dying, ThemeObjectType *out)
{
    if (kind == 2) { *out = dying ? THEME_OBJ_CATCHERFX : THEME_OBJ_CATCHER; return true; }
    if (kind == 3) { *out = dying ? THEME_OBJ_THROWERFX : THEME_OBJ_THROWER; return true; }
    return false;
}

void Scene_OpaquePasses(Renderer *renderer, World *world, CameraPose *cam, Config *config, double now, double elapsed)
{
    LevelPlacements *pl = &renderer->levelPlacements;
    RenderDevice *d3d = renderer->device;

    {
        PROF_SCOPE("level mesh");
        MeshBatch_Draw(pl, &renderer->themeBlock, d3d);
    }
    rso_list(renderer, world, cam, config, pl->switches(),    THEME_OBJ_SWITCH,        now);
    rso_list(renderer, world, cam, config, pl->ice(),   THEME_OBJ_ICE,           now);
    rso_list(renderer, world, cam, config, pl->sticky(),        THEME_OBJ_GLUE,          now);
    rso_list(renderer, world, cam, config, pl->falling(),  THEME_OBJ_DESTRUCTFIELD, now);
    /* the jump pads animate: a 500 ms cycle, animation code 0x14 */
    rso(renderer, world, cam, config, pl->jumpPads().pos(), pl->jumpPads().rot(), pl->jumpPads().count(), THEME_OBJ_JUMPPAD, now,
        (float)fmod(now * 0.002, 1.0), 0x14, 0);
    rso_list(renderer, world, cam, config, pl->teleporters(), THEME_OBJ_TELEPORTER,    now);

    {
        PROF_SCOPE("tile quads");
        QuadBatch_Draw(pl, &renderer->themeBlock, d3d);
    }

    rso_list(renderer, world, cam, config, pl->stairs(),      THEME_OBJ_STAIR,         now);
    pl->drawLifts(world, cam, config, &renderer->themeBlock, d3d, now);
    pl->drawPlatforms(world, cam, config, &renderer->themeBlock, d3d, now);

    /* The bombable blocks: whole, or (while the cell's field203 is set)
     * the fx model, starting the debris on the cell's field20f latch.  Only
     * the first fx draw of the frame gets the elapsed-ms argument.  The
     * counter is a byte. */
    bool firstFx = true;
    for (unsigned char i = 0; i < (unsigned)pl->bombables().count(); ++i) {
        const float *pos = pl->bombables().pos()[i];
        const float *rot = pl->bombables().rot()[i];
        const int v = (unsigned char)(int)(pos[2] * -1.0f);
        Tile *t = world->map()->tile((unsigned char)(int)pos[0], v);
        if (t->field203() == 0) {
            rso(renderer, world, cam, config, pos, rot, 1, THEME_OBJ_OBSTACLE, now);
            continue;
        }
        ThemeObjectTypeSlot *fx = slot(&renderer->themeBlock, THEME_OBJ_OBSTACLEFX);
        for (uint32_t k = 0; k < fx->instanceCount(); ++k) {
            ThemeLevelObject *rec = &fx->records()[k];
            if (rec->explodes() && t->field20f())
                rec->explodeDebris().begin(rec->mesh(), 0, rec->explodeDir());
        }
        if (t->field20f())
            t->setField20f(0);
        if (firstFx) {
            rso(renderer, world, cam, config, pos, rot, 1, THEME_OBJ_OBSTACLEFX, now, 0.0f, 0, (unsigned)(int)elapsed);
            firstFx = false;
        } else {
            rso(renderer, world, cam, config, pos, rot, 1, THEME_OBJ_OBSTACLEFX, now);
        }
    }

    /* The player: CameraFocus f[2..4] is its position, f[1] its yaw, f[0]
     * its animation time; the animation code is its anim byte. */
    CameraFocus *focus = &renderer->cameraFocus;
    float playerRot[3] = { 0.0f, focus->f[1], 0.0f };
    rso(renderer, world, cam, config, &focus->f[2], playerRot, 1, THEME_OBJ_JOHN, now, focus->f[0],
        world->player()->anim(), 0);

    rso_list(renderer, world, cam, config, pl->slides(), THEME_OBJ_SLIDE, now);
    rso(renderer, world, cam, config, pl->exitPos(), pl->exitRot(), 1, THEME_OBJ_EXIT, now);

    /* The items, cell by cell. */
    {
        PROF_SCOPE("items");
        LevelMap *map = world->map();
        for (unsigned v = 0; v < map->extentV(); ++v)
            for (unsigned u = 0; u < map->extentU(); ++u) {
                Tile *t = map->tile(u, v);
                ThemeObjectType ty;
                if (!item_slot(t->contents(), &ty))
                    continue;
                float pos[3] = { (float)u, (float)t->height(), -(float)v };
                float rot[3] = { 0.0f, 0.0f, 0.0f };
                PROF_SCOPE(OBJECT_NAMES[ty]);
                rso(renderer, world, cam, config, pos, rot, 1, ty, now);
            }
    }

    /* The bombs: whole, or the explosion once it has started dying. */
    if (world->bombCount() != 0) {
        PROF_SCOPE("bombs");
        for (unsigned char i = 0; i < world->bombCount(); ++i) {
            Bomb *b = world->bombSlot(world->bombId(i));
            float pos[3] = { b->posU(), b->posY(), -b->posV() };
            float rot[3] = { 0.0f, 0.0f, 0.0f };
            PROF_SCOPE(OBJECT_NAMES[b->dyingStarted() == 0 ? THEME_OBJ_BOMB : THEME_OBJ_EXPLOSION]);
            if (b->dyingStarted() == 0) {
                rso(renderer, world, cam, config, pos, rot, 1, THEME_OBJ_BOMB, now);
                continue;
            }
            arm_fx_records(slot(&renderer->themeBlock, THEME_OBJ_EXPLOSION), b);
            rso(renderer, world, cam, config, pos, rot, 1, THEME_OBJ_EXPLOSION, now, 0.0f, 0, (unsigned)(int)elapsed);
        }
    }

    /* The foes, from the poses FramePose_Foes wrote. */
    if (world->foeCount() != 0) {
        PROF_SCOPE("foes");
        for (unsigned char i = 0; i < world->foeCount(); ++i) {
            Foe *f = world->foeSlot(world->foeId(i));
            FoePose *p = &renderer->foePoses[i];
            const bool dying = f->dyingStarted() != 0;
            ThemeObjectType ty;
            if (!foe_slot(p->kind(), dying, &ty))
                continue;
            PROF_SCOPE(OBJECT_NAMES[ty]);
            if (!dying) {
                rso(renderer, world, cam, config, p->pos(), p->rot(), 1, ty, now, p->stepFrac(), f->anim(), 0);
                continue;
            }
            arm_fx_records(slot(&renderer->themeBlock, ty), f);
            rso(renderer, world, cam, config, p->pos(), p->rot(), 1, ty, now, 0.0f, 0, (unsigned)(int)elapsed);
        }
    }
}

/* ─── Section 4: scene objects, bridges, effects, shadows ───────────────── */

static void shadow(Renderer *renderer, World *world, const void *pos, const void *rot, ThemeObjectType t, double now,
                   float phase, unsigned animKey)
{
    Shadows_DrawObjectShadows(world, &renderer->levelPlacements,
                              (const float *)pos, (const float *)rot, 1, slot(&renderer->themeBlock, t),
                              renderer->device, now, phase, animKey, 0);
}

void Scene_EffectsAndShadows(Renderer *renderer, World *world, CameraPose *cam, Config *config, double now, double dt)
{
    RenderDevice *d3d = renderer->device;
    uint32_t dtBits[2];
    memcpy(dtBits, &dt, sizeof dt);
    Scene_DrawSceneObjects(renderer, d3d, world, cam->eye(), dtBits[0], dtBits[1], now);
    set_stencil_enable(d3d, false);
    BridgeSurf_Draw({world->bridgeSlots(), world->bridgeCount(), config->videoHighlights() != 0},
                    &renderer->themeBlock, d3d, now);

    CameraFocus *focus = &renderer->cameraFocus;
    float playerRot[3] = { 0.0f, focus->f[1], 0.0f };
    Player *pl = world->player();
    if (pl->effectDActive() != 0 && pl->anim() != 10)
        rso(renderer, world, cam, config, &focus->f[2], playerRot, 1, THEME_OBJ_PROTECTIONFX, now);
    if (pl->anim() == 5)
        rso(renderer, world, cam, config, &focus->f[2], playerRot, 1, THEME_OBJ_PARAGLIDEFX, now);

    {
        PROF_SCOPE("shadows");
        /* Stencil shadows: a stencil buffer, more than 16 bpp, and the option. */
        if (d3d->hasStencil() && d3d->bitDepth() > 16 &&
            config->videoShadows() != 0) {
            d3d->SetTexture(0, &renderer->texShadow);
            d3d->SetBlend(BlendState::alpha());
            StencilState st = d3d->stencil();
            st.enable = true;
            st.ref    = 1;
            st.func   = CompareFunc::Equal;
            st.pass   = StencilOp::DecrSat;
            d3d->SetStencil(st);

            if (pl->anim() != 10)
                shadow(renderer, world, &focus->f[2], playerRot, THEME_OBJ_JOHN, now, focus->f[0], pl->anim());

            for (unsigned char i = 0; i < world->foeCount(); ++i) {
                Foe *f = world->foeSlot(world->foeId(i));
                FoePose *p = &renderer->foePoses[i];
                ThemeObjectType ty;
                if (f->dyingStarted() != 0 || !foe_slot(p->kind(), false, &ty))
                    continue;
                shadow(renderer, world, p->pos(), p->rot(), ty, now, p->stepFrac(), f->anim());
            }

            /* Setting 2 and up also shadows the items. */
            if (config->videoShadows() > 1) {
                LevelMap *map = world->map();
                for (unsigned v = 0; v < map->extentV(); ++v)
                    for (unsigned u = 0; u < map->extentU(); ++u) {
                        Tile *t = map->tile(u, v);
                        ThemeObjectType ty;
                        if (!item_slot(t->contents(), &ty))
                            continue;
                        float pos[3] = { (float)u, (float)t->height(), -(float)v };
                        float rot[3] = { 0.0f, 0.0f, 0.0f };
                        shadow(renderer, world, pos, rot, ty, now, 0.0f, 0);
                    }
            }
            set_stencil_enable(d3d, false);
        }
    }
    set_depth_write(d3d, false);
}

/* ─── Section 5: the translucent passes ─────────────────────────────────── */

static void particles(Renderer *renderer, World *world, CameraPose *cam, Config *config, const void *pos, const void *rot,
                      unsigned count, ThemeObjectType t, double now, double elapsed)
{
    if (count == 0)
        return;
    PROF_SCOPE(OBJECT_NAMES[t]);
    Theme_DrawParticleObjects(world, cam, config, &renderer->levelPlacements,
                              (const float (*)[3])pos, (const float (*)[3])rot, count,
                              slot(&renderer->themeBlock, t), renderer->device, now, elapsed, 0);
}

static void particles_list(Renderer *renderer, World *world, CameraPose *cam, Config *config, const PlacementList &l,
                           ThemeObjectType t, double now, double elapsed)
{
    particles(renderer, world, cam, config, l.pos(), l.rot(), l.count(), t, now, elapsed);
}

/* A burst system is a slot whose first record is a particle-system object:
 * its bursts[] are spawned at a point and live 1000 ms. */
static ThemeLevelObject *burst_record(ThemeAssetBlock *theme, ThemeObjectType t)
{
    ThemeLevelObject *rec = &slot(theme, t)->records()[0];
    return rec->kind() == THEME_KIND_PARTICLESYSTEM ? rec : NULL;
}

/* Spawn into the first free burst (a byte index). */
static void spawn_burst(ThemeLevelObject *rec, float x, float y, float z)
{
    for (unsigned char j = 0; j < rec->instanceCount(); ++j) {
        FxBurst *b = &rec->bursts()[j];
        if (b->active)
            continue;
        if (rec->particleSystems()[j] != NULL)
            (rec->particleSystems()[j])->enableRenderNode();
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
static void draw_bursts(RenderDevice *dev, const CameraPose *cam, ThemeLevelObject *rec, const BlendState &blend,
                        BurstTick tick, bool spin, double now, double elapsed)
{
    Texture *tex = rec->subObjects()[0].pTexture;
    if (tex != NULL)
        dev->SetTexture(0, tex);
    dev->SetBlend(blend);

    for (unsigned char j = 0; j < rec->instanceCount(); ++j) {
        FxBurst *b = &rec->bursts()[j];
        if (b->msLeft <= 0) {
            b->active = 0;
            continue;
        }
        Mat4 world = translation(b->pos);
        dev->SetWorld(world);

        ParticleSystem *ps = rec->particleSystems()[j];
        const int ms = (int)elapsed;
        if (tick == TICK_WHOLE_MS)
            ps->tick((float)((double)(unsigned)ms * 0.001f));
        else
            ps->tick((float)(elapsed * 0.001));
        ps->setVector(cam->target()[0] - cam->eye()[0],
                       cam->target()[1] - cam->eye()[1],
                       cam->target()[2] - cam->eye()[2]);
        if (spin) {
            const double a = -rec->rotRateY() * now;
            const float c = (float)cos(a), s = (float)sin(a);
            Mat4 r;
            m4_identity(&r);
            r.m[0] = c;  r.m[2] = s;  r.m[8] = -s;  r.m[10] = c;
            ps->transformCorners(r.m);
        }
        ps->render(dev);
        b->msLeft -= (tick == TICK_WHOLE_MS) ? ms : (int)elapsed;
        ps->disableRenderNode();
    }
    dev->SetBlend(BlendState::off());
}

void Scene_TranslucentPasses(Renderer *renderer, World *world, CameraPose *cam, Config *config, double now, double elapsed,
                             double dt)
{
    LevelPlacements *pl = &renderer->levelPlacements;
    CameraFocus *focus = &renderer->cameraFocus;
    RenderDevice *d3d = renderer->device;
    float playerRot[3] = { 0.0f, focus->f[1], 0.0f };

    if (config->videoParticles() != 0) {
        PROF_SCOPE("particles");
        particles(renderer, world, cam, config, &focus->f[2], playerRot, 1, THEME_OBJ_JOHN, now, elapsed);
        particles_list(renderer, world, cam, config, pl->switches(),      THEME_OBJ_SWITCH,        now, elapsed);
        particles_list(renderer, world, cam, config, pl->bombables(), THEME_OBJ_OBSTACLE,      now, elapsed);
        particles_list(renderer, world, cam, config, pl->stairs(),        THEME_OBJ_STAIR,         now, elapsed);
        particles_list(renderer, world, cam, config, pl->ice(),     THEME_OBJ_ICE,           now, elapsed);
        particles_list(renderer, world, cam, config, pl->sticky(),          THEME_OBJ_GLUE,          now, elapsed);
        particles_list(renderer, world, cam, config, pl->falling(),    THEME_OBJ_DESTRUCTFIELD, now, elapsed);
        particles_list(renderer, world, cam, config, pl->jumpPads(),      THEME_OBJ_JUMPPAD,       now, elapsed);
        particles_list(renderer, world, cam, config, pl->teleporters(),   THEME_OBJ_TELEPORTER,    now, elapsed);
        particles(renderer, world, cam, config, pl->exitPos(), pl->exitRot(), 1, THEME_OBJ_EXIT, now, elapsed);
        particles_list(renderer, world, cam, config, pl->slides(),        THEME_OBJ_SLIDE,         now, elapsed);

        for (unsigned char i = 0; i < world->foeCount(); ++i) {
            Foe *f = world->foeSlot(world->foeId(i));
            FoePose *p = &renderer->foePoses[i];
            ThemeObjectType ty;
            if (foe_slot(p->kind(), f->dyingStarted() != 0, &ty))
                particles(renderer, world, cam, config, p->pos(), p->rot(), 1, ty, now, elapsed);
        }

        /* Bombs: the fuse's effects, then the explosion's 2 s after the drop.
         */
        for (unsigned char i = 0; i < world->bombCount(); ++i) {
            Bomb *b = world->bombSlot(world->bombId(i));
            float pos[3] = { b->posU(), b->posY(), -b->posV() };
            float rot[3] = { 0.0f, 0.0f, 0.0f };
            const bool late = *world->clock() - b->droppedAt() > 2000.0;
            particles(renderer, world, cam, config, pos, rot, 1, late ? THEME_OBJ_EXPLOSION : THEME_OBJ_BOMB, now, elapsed);
        }

        /* A falling tile that fell this tick spawns a destruct-field burst on
         * its cell (the height and -V from the falling tile's own cell). */
        ThemeLevelObject *field = burst_record(&renderer->themeBlock, THEME_OBJ_DESTRUCTFIELDFX);
        for (unsigned char i = 0; i < world->fallingCount(); ++i) {
            FallingTile *bt = world->fallingSlot(i);
            if (bt->justFell() != 0 && field != NULL)
                spawn_burst(field, (float)bt->cellU(), (float)bt->heightCell(),
                            (float)-bt->cellV());
        }
        if (field != NULL)
            draw_bursts(d3d, cam, field,
                        BlendState::on(blendFactorFromTheme(field->subObjects()[0].dwBlendSrc),
                                       blendFactorFromTheme(field->subObjects()[0].dwBlendDst)),
                        TICK_WHOLE_MS, false, now, elapsed);
    }

    /* Setting 2 and up: pickups and the speed trail. */
    if (config->videoParticles() > 1) {
        Player *pl = world->player();
        ThemeLevelObject *crystal = burst_record(&renderer->themeBlock, THEME_OBJ_CRYSTALFX);
        if (pl->pickedUp() == 1 && crystal != NULL)
            spawn_burst(crystal, focus->f[2], focus->f[3], focus->f[4]);
        for (unsigned char i = 0; i < world->foeCount(); ++i) {
            Foe *f = world->foeSlot(world->foeId(i));
            if (f->pickedUp() == 1 && crystal != NULL) {
                const float *p = renderer->foePoses[i].pos();
                spawn_burst(crystal, p[0], p[1], p[2]);
            }
        }
        ThemeLevelObject *coll = burst_record(&renderer->themeBlock, THEME_OBJ_COLLFX);
        if (pl->pickedUp() > 1 && coll != NULL)
            spawn_burst(coll, focus->f[2], focus->f[3], focus->f[4]);

        /* The speed trail: burst 0, live while the speed-up runs and the
         * player moves along (or directly against) its facing. */
        ThemeLevelObject *speed = burst_record(&renderer->themeBlock, THEME_OBJ_SPEEDFX);
        if (speed != NULL) {
            bool on = false;
            if (pl->effectAActive() != 0) {
                const unsigned char facing = pl->facing();
                on = (unsigned)pl->moveDir() == facing ||
                     (unsigned)pl->moveDir() == Sim_GetTurnedDirection(facing, 2);
            }
            if (on) {
                if (!speed->bursts()[0].active) {
                    (speed->particleSystems()[0])->enableRenderNode();
                    speed->bursts()[0].active = 1;
                }
                speed->bursts()[0].pos[0] = focus->f[2];
                speed->bursts()[0].pos[1] = focus->f[3];
                speed->bursts()[0].pos[2] = focus->f[4];
            } else {
                (speed->particleSystems()[0])->disableRenderNode();
                speed->bursts()[0].active = 0;
            }
        }

        set_depth_write(d3d, false);
        if (crystal != NULL)
            draw_bursts(d3d, cam, crystal, BlendState::additive(), TICK_WHOLE_MS, false, now, elapsed);
        if (coll != NULL)
            draw_bursts(d3d, cam, coll, BlendState::additive(), TICK_ELAPSED, true, now, elapsed);

        if (speed != NULL) {
            ParticleSystem *ps = speed->particleSystems()[0];
            Generator *gen = ps->getGenerator(NULL);
            gen->setPosition(speed->bursts()[0].pos[0], speed->bursts()[0].pos[1],
                              speed->bursts()[0].pos[2]);
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
            gen = ps->getGenerator(NULL);
            gen->setDirection(o[0], o[1], o[2]);

            RenderDevice *dev = renderer->device;
            Mat4 ident;
            m4_identity(&ident);
            dev->SetWorld(ident);
            dev->SetTexture(0, speed->subObjects()[0].pTexture);
            dev->SetBlend(BlendState::additive());
            ps->tick((float)(elapsed * 0.001));
                        ps->setVector(cam->target()[0] - cam->eye()[0],
                           cam->target()[1] - cam->eye()[1],
                           cam->target()[2] - cam->eye()[2]);
            ps->render(dev);
            renderer->device->SetBlend(BlendState::off());
        }
    }

    Scene_DrawParticleSystems(&renderer->scene, renderer->showSplinePaths,
                              renderer->showSplineControlPolygons, renderer->device, cam->eye(), dt, now);

    Player *player = world->player();
    if (player->effectDActive() != 0 && player->anim() != 10)
        particles(renderer, world, cam, config, &focus->f[2], playerRot, 1, THEME_OBJ_PROTECTIONFX, now, elapsed);
    if (player->anim() == 5)
        particles(renderer, world, cam, config, &focus->f[2], playerRot, 1, THEME_OBJ_PARAGLIDEFX, now, elapsed);
}
