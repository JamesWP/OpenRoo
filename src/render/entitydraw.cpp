/* The level's objects drawn from their state (entitydraw.h).  Each reads its
 * entity through its accessors and draws through themedraw; the frame's time
 * (f.now) and the World's clock are the only clocks. */

#include <math.h>
#include <stdint.h>
#include "entitydraw.h"
#include "themedraw.h"
#include "world.h"
#include "player.h"
#include "foe.h"
#include "bomb.h"
#include "liftobject.h"
#include "platformobject.h"
#include "bridgeobject.h"
#include "effects.h"
#include "particletypes.h"
#include "animcode.h"
#include "tile.h"

/* The yaw each facing draws at: 1 looks along -V, and each turn right a
 * quarter turn clockwise from there. */
static float facing_yaw(unsigned char facing)
{
    switch (facing) {
    case 2:  return -1.5707964f;
    case 3:  return 3.1415927f;
    case 4:  return 1.5707964f;
    default: return 0.0f;
    }
}

draw::Instance pose(const MovableEntity &e, double now)
{
    draw::Instance in;
    in.pos = { e.posU(), e.posY(), -e.posV() };
    in.rot.y = facing_yaw(e.facing());
    in.animCode = e.anim();
    if (e.moveDir() != 0 || e.anim() >= ANIM_IDLE1) {
        in.animTime = (float)((now - e.animStart()) / e.animDuration());
        // A turn on the spot (moveDir past 10) swings the yaw as it plays.
        if (e.moveDir() > 10 && e.turnKind() == 2)
            in.rot.y -= in.animTime * 1.5707964f;
        else if (e.moveDir() > 10 && e.turnKind() == 4)
            in.rot.y += in.animTime * 1.5707964f;
    }
    return in;
}

/* The theme conditions the player's own state meets. */
static uint8_t player_conditions(const Player &p)
{
    if (p.anim() == ANIM_GHOST)
        return draw::COND_DEAD;
    uint8_t c = 0;
    if (p.glides() != 0 || p.gliding() != 0)
        c |= draw::COND_GLIDING;
    if (p.protectionActive() != 0)
        c |= draw::COND_PROTECTED;
    return c;
}

/* The player's path as a particle emitter's, newest first, every other
 * sample (the path keeps more than an emitter takes): where it was, the way
 * it faced and, for the speed trail, whether that emitted then.  A trail
 * streams out behind, and a little up. */
static void emitter_path(ParticlePath &out, const World &world, bool trailOnly)
{
    const std::deque<PathSample> &path = world.playerPath();
    out.count = 0;
    for (size_t i = path.size(); i > 0 && out.count < PARTICLE_PATH; i -= std::min<size_t>(i, 2)) {
        const PathSample &s = path[i - 1];
        const float yaw = facing_yaw(s.facing);
        const Vec3 dir = { sinf(yaw), 0.1f, -cosf(yaw) };
        const float len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        out.age[out.count] = (float)((world.now() - s.timeMs) * 0.001);
        out.pos[out.count] = { s.u, s.y, -s.v };
        out.dir[out.count] = { dir.x / len, dir.y / len, dir.z / len };
        out.on[out.count] = !trailOnly || s.trail;
        out.count++;
    }
}

/* The player: its model, its auras, its shadow, its particles and its speed
 * trail. */
static void draw_player(draw::Frame &f, draw::Pass pass, const World &world)
{
    const Player &p = *world.player();
    const double now = world.now();
    draw::Instance in = pose(p, now);
    in.conditions = player_conditions(p);
    // Dead or respawning, its animation holds on its first frame.
    if (p.moveState() != 0)
        in.animTime = 0.0f;
    const bool dead = p.anim() == ANIM_GHOST;
    const bool aura = p.protectionActive() != 0 && !dead;
    const bool gliding = p.anim() == ANIM_PARAGLIDE;

    switch (pass) {
    case draw::Pass::Opaque:
        draw::objects(f, THEME_OBJ_PLAYER, &in);
        break;
    case draw::Pass::Effects:
        if (aura)
            draw::objects(f, THEME_OBJ_PLAYER_PROTECTED, &in);
        if (gliding)
            draw::objects(f, THEME_OBJ_PLAYER_GLIDING, &in);
        break;
    case draw::Pass::Shadows:
        if (!dead)
            draw::shadows(f, THEME_OBJ_PLAYER, &in);
        break;
    case draw::Pass::Particles: {
        static ParticlePath path;
        if (f.opts.particles > 0) {
            emitter_path(path, world, false);
            in.path = &path;
            draw::particles(f, THEME_OBJ_PLAYER, &in);
            in.path = nullptr;
        }
        if (f.opts.particles > 1) {
            emitter_path(path, world, true);
            draw::trail(f, THEME_OBJ_PLAYER_SPEED_TRAIL, path);
        }
        if (aura)
            draw::particles(f, THEME_OBJ_PLAYER_PROTECTED, &in);
        if (gliding)
            draw::particles(f, THEME_OBJ_PLAYER_GLIDING, &in);
        break;
    }
    }
}

/* A foe: a catcher or thrower model (its dying look, with its death effects
 * by their age, once it is dying), its shadow and its particles. */
static void draw_foe(draw::Frame &f, draw::Pass pass, const Foe &e, double now)
{
    // Kind 2 is a catcher, 3 a thrower; any other kind has no look.
    if (!e.isCatcher() && !e.isThrower())
        return;
    const bool dying = e.dyingStarted() != 0;
    ThemeObjectType look = e.isCatcher() ? THEME_OBJ_FOE_CATCHER : THEME_OBJ_FOE_THROWER;
    if (dying)
        look = e.isCatcher() ? THEME_OBJ_FOE_CATCHER_DYING : THEME_OBJ_FOE_THROWER_DYING;
    draw::Instance in = pose(e, now);
    if (dying) {
        in.conditions = draw::COND_DEAD;
        in.animCode = 0;
        in.animTime = 0.0f;
        in.effectAge = (float)((now - e.dyingSince()) * 0.001);
        in.seed = e.id();
    }

    switch (pass) {
    case draw::Pass::Opaque:
        draw::objects(f, look, &in);
        break;
    case draw::Pass::Shadows:
        if (!dying)
            draw::shadows(f, look, &in);
        break;
    case draw::Pass::Particles:
        if (f.opts.particles > 0)
            draw::particles(f, look, &in);
        break;
    default:
        break;
    }
}

/* A bomb: the bomb, or once it has gone off the explosion (its debris by
 * their age), and its particles. */
static void draw_bomb(draw::Frame &f, draw::Pass pass, const Bomb &b, double now)
{
    const bool exploded = b.dyingStarted() != 0;
    const ThemeObjectType look = exploded ? THEME_OBJ_BOMB_EXPLODING : THEME_OBJ_BOMB;
    draw::Instance in;
    in.pos = { b.posU(), b.posY(), -b.posV() };
    if (exploded) {
        in.effectAge = (float)((now - b.dyingSince()) * 0.001);
        in.seed = b.id();
    }

    switch (pass) {
    case draw::Pass::Opaque:
        draw::objects(f, look, &in);
        break;
    case draw::Pass::Particles:
        if (f.opts.particles > 0)
            draw::particles(f, look, &in);
        break;
    default:
        break;
    }
}

/* A lift, in the opaque pass. */
static void draw_lift(draw::Frame &f, draw::Pass pass, const LiftObject &l)
{
    if (pass != draw::Pass::Opaque)
        return;
    draw::Instance in;
    in.pos = { l.posU(), l.height(), -l.posV() };
    draw::objects(f, THEME_OBJ_LIFT, &in);
}

/* A platform, in the opaque pass. */
static void draw_platform(draw::Frame &f, draw::Pass pass, const PlatformObject &p)
{
    if (pass != draw::Pass::Opaque)
        return;
    draw::Instance in;
    in.pos = { p.posU(), p.posY(), -p.posV() };
    if (p.kind() == TILE_PLATFORM_U)
        in.rot.y = 1.5707964f;
    // The platform's walk cycle, every 500 ms.
    in.animCode = ANIM_WALK_FORWARD;
    in.animTime = (float)fmod(f.now * 0.002, 1.0);
    draw::objects(f, THEME_OBJ_PLATFORM, &in);
}

/* A bridge's deck: one quad, a unit wide, from the anchor to the live end, z
 * negated, for clock t, ms; false while there is no deck, never armed and in
 * phase 0.  Its texture repeats every four units along it and scrolls along
 * it over time, a span's length a second. */
static bool bridge_deck(const BridgeObject &br, draw::FieldVertex v[4], double t)
{
    if (br.armed() == 0 && br.phase() == 0)
        return false;

    const float ax = br.restU(), ay = br.restY(), az = -br.restV();
    const float dx = br.posU() - ax, dy = br.posY() - ay, dz = -br.posV() - az;
    const float len = sqrtf(dx * dx + dy * dy + dz * dz);
    const float tiles = len * 0.25f;
    const float n = br.span() != 0 ? (float)br.span() : 1.0f;
    const float scroll = (float)fmod(t * 0.001 / n, 1.0) * n;

    // The deck's two ends along its axis, a and b, and the texture's v at each.
    const bool fwd = br.step() > 0;
    float a, b;
    if ((signed char)br.axis() == 1) {
        const float x0 = ax - 0.5f;
        a = fwd ? x0 + len : x0;
        b = fwd ? x0 : x0 - len;
    } else {
        const float z1 = az + 0.5f;
        a = fwd ? z1 - len : z1;
        b = fwd ? z1 : z1 + len;
    }
    const float va = fwd ? tiles - scroll : scroll + tiles;
    const float vb = fwd ? -scroll : scroll;

    const uint32_t white = 0xffffffff;
    if ((signed char)br.axis() == 1) {
        const float lo = az - 0.5f, hi = az + 0.5f;
        v[0] = { a, ay, lo, white, 0.0f, va };
        v[1] = { b, ay, lo, white, 0.0f, vb };
        v[2] = { a, ay, hi, white, 1.0f, va };
        v[3] = { b, ay, hi, white, 1.0f, vb };
    } else {
        const float lo = ax - 0.5f, hi = ax + 0.5f;
        v[0] = { hi, ay, a, white, 1.0f, va };
        v[1] = { lo, ay, a, white, 0.0f, va };
        v[2] = { hi, ay, b, white, 1.0f, vb };
        v[3] = { lo, ay, b, white, 0.0f, vb };
    }
    return true;
}

/* A bridge's deck, in the effects pass. */
static void draw_bridge(draw::Frame &f, draw::Pass pass, const BridgeObject &b)
{
    draw::FieldVertex q[1][4];
    if (pass != draw::Pass::Effects || !bridge_deck(b, q[0], f.now))
        return;
    const draw::Instance in;
    draw::objects(f, THEME_OBJ_BRIDGE, &in, 1, q);
}

void drawWorld(draw::Frame &f, draw::Pass pass, const World &world)
{
    const double now = world.now();
    for (const LiftObject &l : world.lifts())
        draw_lift(f, pass, l);
    for (const PlatformObject &p : world.platforms())
        draw_platform(f, pass, p);
    for (const BridgeObject &b : world.bridges())
        draw_bridge(f, pass, b);
    draw_player(f, pass, world);
    for (const Bomb &b : world.bombs())
        draw_bomb(f, pass, b, now);
    for (const Foe &e : world.foes())
        draw_foe(f, pass, e, now);

    // The pickups' bursts and the falling tiles' dust, by their age.
    if (pass != draw::Pass::Particles)
        return;
    const BlendState additive = BlendState::additive();
    for (const EffectStart &e : world.effects()) {
        const float age = (float)((now - e.startMs) * 0.001);
        const Vec3 at = { e.u, e.y, -e.v };
        switch (e.kind) {
        case EffectKind::FallingDust:
            if (f.opts.particles > 0)
                draw::burst(f, THEME_OBJ_FALLING_DUST, at, age, e.seed);
            break;
        case EffectKind::CrystalPickup:
            if (f.opts.particles > 1)
                draw::burst(f, THEME_OBJ_CRYSTAL_PICKUP, at, age, e.seed, &additive);
            break;
        case EffectKind::ItemPickup:
            if (f.opts.particles > 1)
                draw::burst(f, THEME_OBJ_ITEM_PICKUP, at, age, e.seed, &additive);
            break;
        }
    }
}
