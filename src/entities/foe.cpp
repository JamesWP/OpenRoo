/* Foe's spawn, remove, per-tick step and chase-target search, plus the
 * chooseTarget/dropBomb/checkPlayerContact/finishDespawn helpers GameTick's
 * foe loop calls directly. */

#include <strings.h>
#include <math.h>
#include <stdint.h>
#include "sysdev.h"
#include <string.h>

#include "foe.h"
#include "foetype.h"
#include "dbg.h"
#include "tilenames.h"
#include "levelmap.h"
#include "switchcells.h"
#include "tile.h"
#include "movableentity.h"
#include "entitymath.h"
#include "foepath.h"
#include "bomb.h"
#include "tilequery.h"
#include "logger.h"
#include "themedraw.h"


/* KAROO_SPAWN_DIAG, KAROO_REMOVE_DIAG and
 * KAROO_FOESTEP_DIAG log periodic counts and first-occurrence events for the
 * spawn, remove and step below. */
static int s_diag_spawn   = 0;
static int s_diag_remove  = 0;
static int s_diag_step    = 0;
static int s_init         = 0;

static void diag_init(void)
{
    if (s_init)
        return;
    s_init = 1;

    s_diag_spawn  = sysdev::envFlag("KAROO_SPAWN_DIAG");
    s_diag_remove = sysdev::envFlag("KAROO_REMOVE_DIAG");
    s_diag_step   = sysdev::envFlag("KAROO_FOESTEP_DIAG");
}

Tile *Foe::tile(int u, int v) const
{
    return map_->tile( u, v);
}

Foe::Foe()
{
    soundSource_ = SoundSource::Foe;
    dyingStarted_         = 0;
    removeRequested_ = 0;
    dying_         = 0;
    iceDir_         = 0;
    field_124        = 1;
    moveDir_        = 0;
    pendingMove_     = 0;
    stuckSince_        = 0.0;
    sliding_         = 0;
    falling_        = 0;
    platformSlot_        = 0xff;
    switchSlot_         = 0xff;
    stepGrace_         = 20.0;
    idleDuration_         = 1000.0;
    moveState_       = 0;
    held_         = 0;
    bombsCarried_         = 0;
    bombDropRequest_         = 0;
    teleportPhase_         = 0;
    field_156        = 0;
    glides_         = 0;
    launched_        = 0;
    gliding_         = 0;
    onSwitch_        = 0;
    onLift_         = 0;
    anim_         = 0;
    idleStarted_         = 0;
    pickedUp_         = 0;
    dropContents_    = 0;
    type_            = 0;
    pathfinder_      = 0;

    // Everything else starts at zero.  stepEnd_ in particular is read by the
    // first step commit (now_ - stepEnd_, to decide where the move animation
    // starts): zero is "long ago", so it starts at once, whereas whatever a
    // recycled heap block held could be a NaN, which made animStart_ NaN and
    // left the foe's position NaN.
    now_ = 0;
    clock_ = 0;
    tickStep_ = 0;
    map_ = 0;
    facing_ = 0;
    posU_ = posY_ = posV_ = 0.0f;
    cellU_ = cellV_ = heightCell_ = 0;
    stepEnd_ = 0.0;
    fallSpeed_ = 0;
    stepDuration_ = 0.0;
    lastActive_ = 0.0;
    dyingSince_ = 0.0;
    lastContact_ = 0.0;
    field_d8 = 0;
    field_ee = 0;
    chaseSpeed_ = 0;
    kind_ = 0;
    homeU_ = homeV_ = homeH_ = 0;
    targetU_ = targetV_ = 0;
    field_15d = 0;
    lastMoveDir_ = 0;
    fallStartH_ = 0;
    fallStart_ = 0.0;
    turnKind_ = 0;
    field_12e = 0;
    animDuration_ = 0.0;
    animStart_ = 0.0;
    stepU_ = stepV_ = 0;
    field_141 = 0;
    markerCellU_ = markerCellV_ = markerCellH_ = 0;
    teleportSince_ = 0.0;
    movingBackwards_ = 0;
    onStairOrSlide_ = 0;
}

Foe::~Foe()
{
    FoePath *pf;

    tile(cellU_, cellV_)->setField1a1(0);
    tile(cellU_, cellV_)->setOccupant(0);
    tile(cellU_ - stepU_, cellV_ - stepV_)->setOccupant(0);

    pf = pathfinder_;
    if (pf != 0)
        FoePath::destroy(pf);
}

static unsigned char s_type_seen[256];
static unsigned int  s_hi_level[256];

static unsigned int level_hash(const char *s)
{
    unsigned int h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h ? h : 1u;
}

static unsigned long s_spawns          = 0;
static int           s_logged_spawn    = 0;
static int           s_logged_hightype = 0;
static int           s_logged_kind2    = 0;
static int           s_logged_kind3    = 0;

Foe &Foe::spawn(const EntityContext &ctx, EntityList<Foe> &foes,
                unsigned int uArg, unsigned int vArg,
                unsigned int hArg, unsigned int kindArg,
                unsigned int typeArg)
{
    unsigned char u    = (unsigned char)(uArg & 0xff);
    unsigned char v    = (unsigned char)(vArg & 0xff);
    unsigned char h    = (unsigned char)(hArg & 0xff);
    signed char   kind = (signed char)(kindArg & 0xff);
    unsigned char type = (unsigned char)(typeArg & 0xff);

    diag_init();

    Foe *p = &foes.add(std::unique_ptr<Foe>(new Foe));
    // Its place in the list, counting from 1.
    const unsigned int n = (unsigned int)foes.size();

    if (!s_logged_spawn) {
        s_logged_spawn = 1;
        g_logger.write("foe: first foe spawn -- n=%u obj=%p "
                  "u=%u v=%u h=%u kind=%d type=%u\n",
                  n, (void *)p, (unsigned)u,
                  (unsigned)v, (unsigned)h, (int)kind, (unsigned)type);
    }
    if (s_diag_spawn) {
        const char  *lvl = ctx.levelName;
        unsigned int lh  = level_hash(lvl);

        ++s_spawns;
        if ((s_spawns % 500) == 0)
            g_logger.write("foe: %lu spawns\n", s_spawns);
        if (!s_type_seen[type]) {
            s_type_seen[type] = 1;
            g_logger.write("foe: type %u (0x%02x)%s first seen in '%s'\n",
                      (unsigned)type, (unsigned)type,
                      type > FOE_FACING_BASE ? " >0x64" : "", lvl);
        }
        if (type > FOE_FACING_BASE && s_hi_level[type] != lh) {
            s_hi_level[type] = lh;
            g_logger.write("foe: HIGHTYPE %u (0x%02x) in '%s'\n",
                      (unsigned)type, (unsigned)type, lvl);
        }
    }

    p->clock_ = ctx.clock;

    p->tickStep_ = ctx.tickStep;

    // Each foe starts 1500ms later than the one before it in the list, so
    // their step and idle timers do not all line up.
    p->lastActive_ = (double)(int)(n * 1500) + *ctx.clock;

    p->kind_ = (unsigned char)kind;
    if (kind == (unsigned char)FoeKind::Catcher) {
        p->stepDuration_ = 500.0;
        if (s_diag_spawn && !s_logged_kind2) {
            s_logged_kind2 = 1;
            g_logger.write("foe: first kind-2 foe\n");
        }
    } else if (kind == (unsigned char)FoeKind::Thrower) {
        p->stepDuration_ = 700.0;
        if (s_diag_spawn && !s_logged_kind3) {
            s_logged_kind3 = 1;
            g_logger.write("foe: first kind-3 foe\n");
        }
    }

    p->map_ = ctx.map;
    p->deathCounter_ = ctx.deaths;
    p->type_     = type;

    if (type > FOE_FACING_BASE) {
        p->facing_ = (unsigned char)(type - FOE_FACING_BASE);  // facing_ carries the type - 0x64
        type = 0;  // type is zeroed so the pathfinder mode below never sees it.
        if (s_diag_spawn && !s_logged_hightype) {
            s_logged_hightype = 1;
            g_logger.write("foe: first type > 0x64\n");
        }
    } else {
        p->facing_ = 1;
        if (type == FOE_GEM_CARRIER) {
            p->dropContents_ = 1;
            ++*ctx.crystalsInLevel;
        } else if (type == FOE_LIFE_CARRIER) {
            p->dropContents_ = 7;
        } else if (type == FOE_GEM_RUNNER) {
            p->dropContents_ = 1;
        }
    }

    p->cellU_      = (signed char)u;
    p->cellV_      = (signed char)v;
    p->heightCell_ = (signed char)h;
    p->homeU_      = u;
    p->homeV_      = v;
    p->homeH_      = h;

    p->posU_ = (float)(int)(unsigned int)u;
    p->posY_ = (float)(int)(unsigned int)h;
    p->posV_ = (float)(int)(unsigned int)v;

    ctx.map->tile( (int)u, (int)v)->setOccupant(p->kind_);

    p->pathfinder_ = FoePath::create(p->map_, 0);

    p->pathfinder_->setMode(type);

    return *p;
}

static unsigned long s_removals       = 0;
static int           s_logged_remove  = 0;

void Foe::remove(EntityList<Foe> &foes, std::size_t i, PlayEvents &events)
{
    diag_init();

    if (!s_logged_remove) {
        s_logged_remove = 1;
        g_logger.write("foe: first foe removal -- i=%u obj=%p\n",
                  (unsigned)i, (void *)&foes[i]);
    }
    if (s_diag_remove) {
        ++s_removals;
        if ((s_removals % 500) == 0)
            g_logger.write("foe: %lu removals\n", s_removals);
    }

    foes[i].despawn(events);
    foes.erase(i);
}

void Foe::despawn(PlayEvents &events) const
{
    events.soundStop(sound(Sfx::Fall));
    events.entityDestroyed(SoundSource::Foe, id());
}

static inline int ftol_i(double v)
{
    return (int)(long long)v;
}

static inline int iabs_orig(int v)
{
    int m = v >> 31;
    return (v ^ m) - m;
}

static unsigned long s_ticks          = 0;
static int           s_logged_first   = 0;
static int           s_logged_contact = 0;
static int           s_logged_consume = 0;
static int           s_logged_chase   = 0;
static int           s_logged_turn    = 0;
static int           s_logged_repost  = 0;
static int           s_logged_frozen  = 0;

void Foe::step(unsigned char playerU, unsigned char playerV, PlayEvents &events)
{
    diag_init();

    if (!s_logged_first) {
        s_logged_first = 1;
        g_logger.write("foe: first foe step -- this=%p\n", (void *)this);
    }
    if (s_diag_step) {
        ++s_ticks;
        if ((s_ticks % 5000) == 0)
            g_logger.write("foe: %lu ticks\n", s_ticks);
    }

    tickStepCopy_ = *tickStep_;
    now_        = *clock_;

    if (isThrower() && held_ == 0 && bombDropRequest_ == 0) {
        if (iabs_orig((int)playerU - (int)cellU_) < 2 &&
            iabs_orig((int)playerV - (int)cellV_) < 2) {

            // PRESERVED: the difference is compared unsigned, so a
            // last-contact time after now wraps to a huge value and still
            // clears the 2000ms threshold.
            int last = ftol_i(lastContact_);
            int now  = ftol_i(now_);

            if ((unsigned int)(now - last) >= 0x7d0u) {
                if (s_diag_step && !s_logged_contact) {
                    s_logged_contact = 1;
                    g_logger.write("foe: first contact -- foe=(%d,%d) "
                              "player=(%u,%u)\n",
                              (int)cellU_, (int)cellV_,
                              (unsigned)playerU, (unsigned)playerV);
                }
                lastContact_ = now_;
                bombDropRequest_ = 1;
            }
        }
    }

    pickedUp_ = 0;
    if (type_ == FOE_GEM_THIEF && moveDir_ == 0) {
        Tile *t = tile(cellU_, cellV_);

        if ((int)heightCell_ == (int)t->height() &&
            (signed char)t->contents() == CONTENTS_CRYSTAL) {

            t->setContents(0);

            if (s_diag_step && !s_logged_consume) {
                s_logged_consume = 1;
                g_logger.write("foe: first tile consume -- cell=(%d,%d)\n",
                          (int)cellU_, (int)cellV_);
            }

            events.soundPlay(sound(Sfx::Crystal), (float)(int)cellU_, (float)(int)heightCell_,
                             -(float)(int)cellV_);
            pickedUp_ = 1;
        }
    }

    pendingMove_ = 0;
    if (moveDir_ != 0) {
        if (s_diag_step && !s_logged_frozen) {
            s_logged_frozen = 1;
            g_logger.write("foe: first FROZEN foe -- +0x14e=%d cell=(%d,%d)\n",
                      moveDir_, (int)cellU_, (int)cellV_);
        }
    }
    if (moveDir_ != 0) {
        updateMovement(events);
        return;
    }

    chase(playerU, playerV, chaseSpeed_);

    if ((signed char)pendingMove_ != 0) {
        if (s_diag_step && !s_logged_chase) {
            s_logged_chase = 1;
            g_logger.write("foe: first chase direction -- dir=%u foe=(%d,%d)\n",
                      (unsigned)pendingMove_, (int)cellU_, (int)cellV_);
        }
        // the early return: steps 6 and 7 (the turn table, return-to-post) run
        // only when the chase above found no move.
        targetU_ = playerU;
        targetV_ = playerV;
        updateMovement(events);
        return;
    }

    if (type_ == FOE_SWITCH_RUNNER) {
        Tile *t = tile(cellU_, cellV_);

        if ((signed char)t->objectMarker() == TILE_SWITCH) {
            unsigned char facing;

            if (s_diag_step && !s_logged_turn) {
                s_logged_turn = 1;
                g_logger.write("foe: first turn table -- cell=(%d,%d)\n",
                          (int)cellU_, (int)cellV_);
            }

            if ((signed char)lastMoveDir_ != 0)
                pendingMove_ = Sim_GetTurnedDirection(lastMoveDir_, 2);

            facing = facing_;

            if (pendingMove_ == facing) {
                pendingMove_ = facing;
                turnKind_    = 1;
            }
            if (pendingMove_ == Sim_GetTurnedDirection(facing, 1)) {
                turnKind_    = 2;
                pendingMove_ = (unsigned char)(Sim_GetTurnedDirection(facing_, 1) + 10);
            }
            if (pendingMove_ == Sim_GetTurnedDirection(facing_, 3)) {
                turnKind_    = 4;
                pendingMove_ = (unsigned char)(Sim_GetTurnedDirection(facing_, 3) + 10);
            }
            // PRESERVED: tests the turn-2 direction but writes the turn-1 one.
            if (pendingMove_ == Sim_GetTurnedDirection(facing_, 2)) {
                turnKind_    = 2;
                pendingMove_ = (unsigned char)(Sim_GetTurnedDirection(facing_, 1) + 10);
            }
        }
    }

    if (type_ == FOE_RETURNER) {
        if (s_diag_step && !s_logged_repost) {
            s_logged_repost = 1;
            g_logger.write("foe: first return-to-post -- target=(%u,%u)\n",
                      (unsigned)targetU_, (unsigned)targetV_);
        }
        chase(targetU_, targetV_, chaseSpeed_);
    }

    updateMovement(events);
}

static inline unsigned ftol32(double v)
{
    return (unsigned)(long long)v;
}

unsigned char Foe::chase(unsigned char targetU, unsigned char targetV,
                         unsigned short speed)
{
    diag_init();

    pathfinder_->setCap(speed);
    pathfinder_->setTarget(targetU, targetV);

    if (type_ == FOE_SWITCH_RUNNER)
        pathfinder_->setMode((onSwitch_ != 0) ? 0 : 2);

    if (moveDir_ != 0)
        return pendingMove_;

    if (pathfinder_->find((int)cellU_, (int)cellV_,
                          (unsigned)targetU, (unsigned)targetV) == 0) {
        pendingMove_ = 0;
        return pendingMove_;
    }

    pathfinder_->setResult(pathfinder_->result()->parent());

    const PathNode *node = pathfinder_->result();
    const unsigned char nu = (unsigned char)node->u();
    const unsigned char nv = (unsigned char)node->v();

    const signed char du = (signed char)(unsigned char)(nu - (unsigned char)cellU_);
    const signed char dv = (signed char)(unsigned char)(nv - (unsigned char)cellV_);

    if (dv == -1) pendingMove_ = 1;  // north
    if (du ==  1) pendingMove_ = 2;  // east
    if (dv ==  1) pendingMove_ = 3;  // south
    if (du == -1) pendingMove_ = 4;  // west

    if ((signed char)field_124 == 1) {
        const unsigned char facing = facing_;

        if (pendingMove_ == facing) {
            pendingMove_ = facing;
            turnKind_    = 1;
        }
        if (pendingMove_ == Sim_GetTurnedDirection(facing, 2)) {
            pendingMove_ = Sim_GetTurnedDirection(facing_, 2);
            turnKind_    = 3;
        }
        if (pendingMove_ == Sim_GetTurnedDirection(facing_, 1)) {
            turnKind_    = 2;
            pendingMove_ = (unsigned char)(Sim_GetTurnedDirection(facing_, 1) + 10);
        }
        if (pendingMove_ == Sim_GetTurnedDirection(facing_, 3)) {
            turnKind_    = 4;
            pendingMove_ = (unsigned char)(Sim_GetTurnedDirection(facing_, 3) + 10);
        }
        // PRESERVED: tests the turn-2 direction but writes the turn-1 one, as
        // the step-time turn table above does.
        if (pendingMove_ == Sim_GetTurnedDirection(facing_, 2)) {
            turnKind_    = 2;
            pendingMove_ = (unsigned char)(Sim_GetTurnedDirection(facing_, 1) + 10);
        }
    }

    if (facing_ != pendingMove_) {
        if (facing_ != Sim_GetTurnedDirection(pendingMove_, 2))
            return pendingMove_;
    }

    Tile *step = map_->tile( (int)nu, (int)nv);
    Tile *here = tile(cellU_, cellV_);

    if (step->objectMarker() == TILE_EMPTY)
        pendingMove_ = 0;
    if (platformSlot_ != 0xff && step->platformTrack() != 0)
        pendingMove_ = 0;
    if (here->objectMarker() == TILE_JUMP_PAD)
        pendingMove_ = 0;

    if (step->objectMarker() == TILE_LIFT) {
        const unsigned a = ftol32(step->liftParkedSince());
        const unsigned b = ftol32(now_ - (double)a);
        const unsigned c = ftol32(step->liftDwell());
        const unsigned d = ftol32(animDuration_);
        if ((int)(c - b) < (int)d)
            pendingMove_ = 0;
        if ((unsigned)step->height() != (unsigned)(int)heightCell_)
            pendingMove_ = 0;
    }

    if (here->objectMarker() == TILE_LIFT) {
        if ((unsigned)step->height() != (unsigned)(int)heightCell_)
            pendingMove_ = 0;
    }

    if (step->objectMarker() != TILE_PLATFORM_TRACK)
        return pendingMove_;

    {
        const unsigned e = ftol32(step->platformParkedSince());
        const unsigned f = ftol32(now_ - (double)e);
        const unsigned g = ftol32(step->platformDwell());
        const unsigned h = ftol32(animDuration_);
        if ((int)(g - f) >= (int)h)
            return pendingMove_;
    }

    pendingMove_ = 0;
    return pendingMove_;
}

/* GameTick's foe loop calls chooseTarget once per foe to pick this tick's
 * target, then dropBomb, checkPlayerContact and finishDespawn as needed; the
 * Game and Player values it passes are read once, here. */

void Foe::chooseTarget(const EntityContext &ctx, const EntityList<Foe> &foes, int hold,
                       unsigned char playerU, unsigned char playerV,
                       unsigned char escortU, unsigned char escortV,
                       unsigned char *pu, unsigned char *pv)
{
    unsigned char tu = playerU, tv = playerV;

    held_ = hold;
    chaseSpeed_ = 0x32;
    if (type_ == FOE_EXIT_GUARD) {
        tu = escortU;
        tv = escortV;
        chaseSpeed_ = 400;
    }
    if (type_ == FOE_SWITCH_RUNNER) {
        tu = (unsigned char)cellU_;
        tv = (unsigned char)cellV_;
        if (Sim_FindNearestListedObjectTile(ctx.switches, *ctx.switchMax, ctx.map, &tu, &tv, 7) != 0) {
            chaseSpeed_ = 100;
            chase(tu, tv, 100);
            if (onSwitch_ == 0 && pendingMove_ == 0 && turnKind_ == 0) {
                tu = playerU;
                tv = playerV;
                chaseSpeed_ = 0x32;
            }
        } else {
            tu = playerU;
            tv = playerV;
            chaseSpeed_ = 100;
        }
    }
    if (type_ == FOE_GEM_THIEF) {
        tu = (unsigned char)cellU_;
        tv = (unsigned char)cellV_;
        if (Sim_FindNearestFlaggedTileInRadius(ctx.map, &tu, &tv, 5) == 0) {
            tu = playerU;
            tv = playerV;
            chaseSpeed_ = 100;
        } else {
            chaseSpeed_ = 0x96;
            chase(tu, tv, 0x96);
            if (pendingMove_ == 0) {
                tu = playerU;
                tv = playerV;
                chaseSpeed_ = 100;
            }
        }
    }
    if (type_ == FOE_FOLLOWER) {
        int found = 0;
        held_ = 0;
        for (const Foe &other : foes) {
            if (other.isCatcher()) {
                found = 1;
                tu = (unsigned char)other.cellU_;
                tv = (unsigned char)other.cellV_;
                chaseSpeed_ = 0x96;
            }
        }
        if (!found)
            held_ = 1;
    }
    if (type_ == FOE_GEM_RUNNER) {
        held_ = 0;
        if (Sim_FindFarthestOccupiedTile(map(), &tu, &tv) != 0)
            chaseSpeed_ = 0x96;
        else
            held_ = 1;
    }
    *pu = tu;
    *pv = tv;
}

void Foe::dropBomb(const EntityContext &ctx, EntityList<Bomb> &bombs, PlayEvents &events)
{
    if (bombDropRequest_ == 0 || type_ == FOE_SWITCH_RUNNER)
        return;
    int spawn = 1, offset = 0;
    if (moveDir_ != 0) {
        double since = (double)*ctx.clock - (double)animStart_;
        if (since < 50.0)
            offset = 1;
        else
            spawn = 0;
    }
    if (!spawn)
        return;
    if (offset)
        Bomb::spawn(ctx, bombs, events, (unsigned char)((unsigned char)cellU_ - (unsigned char)stepU_),
                          (unsigned char)((unsigned char)cellV_ - (unsigned char)stepV_),
                          (unsigned char)((unsigned char)heightCell_ - (unsigned char)field_141),
                          facing_);
    else
        Bomb::spawn(ctx, bombs, events, (unsigned char)cellU_, (unsigned char)cellV_,
                          (unsigned char)heightCell_, facing_);
    bombDropRequest_ = 0;
}

void Foe::checkPlayerContact(unsigned char *playerMoveState,
                             float playerU, float playerY, float playerV)
{
    if (*playerMoveState == 0) {
        if (!isThrower() && moveState_ == 0) {
            double dx = (double)posU_ - (double)playerU;
            double dy = (double)posY_ - (double)playerY;
            double dz = (double)posV_ - (double)playerV;
            double s = dz * dz + dy * dy;
            s = s + dx * dx;
            double dist = sqrt(s);
            if (dist < 0.5)
                *playerMoveState = 1;
        }
    } else if (moveDir_ == 0) {
        anim_ = 0x28;
    }
}

bool Foe::finishDespawn(LevelMap *map)
{
    if (moveState_ == 0 || falling_ != 0)
        return false;
    dying_ = 1;
    Tile *home = map->snapshot((signed char)homeU_, (signed char)homeV_);
    if (home->contents() != CONTENTS_TIMED_SPAWN)
        home->setContents(0);
    if (removeRequested_ == 0)
        return false;
    if ((double)posY_ > 0.0)
        tile(cellU_, cellV_)->setContents(dropContents_);
    return true;
}

static void foe_swatch(ImDrawList *dl, ImVec2 a, ImVec2 b, void *)
{
    dl->AddCircleFilled(ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), (b.x - a.x) * 0.35f,
                        IM_COL32(230, 50, 50, 255));
}

void Foe::debugDraw(unsigned n) const
{
    if (!dbg::active())
        return;
    if (pathfinder())  // its route goes under it
        pathfinder()->debugDraw(posU(), posV());
    dbg::MapLayer layer;
    if (!layer)
        return;
    const dbg::MapView &m = *layer;
    if (!m.asLoaded) {
        const ImVec2 p = m.centre(posU(), posV());
        m.dl->AddCircleFilled(p, m.cell * 0.35f,
                              dyingStarted() ? IM_COL32(120, 60, 60, 255) : IM_COL32(230, 50, 50, 255));
        // A tick for its facing, as the player's.
        const int f = facing();
        if (f >= DIR_MIN && f <= DIR_MAX)
            m.dl->AddLine(p, ImVec2(p.x + DIR_DU[f] * m.cell * 0.5f, p.y + DIR_DV[f] * m.cell * 0.5f),
                          IM_COL32(0, 0, 0, 255), 2);
        m.legend(2, "foe", foe_swatch);
    }
    if (m.hovered((uint8_t)cellU(), (uint8_t)cellV())) {
        m.separator();
        m.tip("foe #%u at U%u V%u H%u", n, (uint8_t)cellU(), (uint8_t)cellV(), (uint8_t)heightCell());
        m.tip("kind %u, behaviour %u, facing %u%s", kind(), type(), facing(), moveDir() ? ", moving" : "");
        const char *drops = Tile_ContentsName(dropContents());
        m.tip("home U%u V%u H%u, drops %s", homeU(), homeV(), homeH(), drops ? drops : "nothing");
        if (held())
            m.tip("frozen");
        if (dyingStarted())
            m.tip("dying");
    }
}

void Foe::draw(draw::Frame &f, draw::Pass pass, unsigned deathsDrawn) const
{
    // Kind 2 is a catcher, 3 a thrower; any other kind has no look.
    if (!isCatcher() && !isThrower())
        return;
    const bool dying = dyingStarted_ != 0;
    ThemeObjectType look = isCatcher() ? THEME_OBJ_FOE_CATCHER : THEME_OBJ_FOE_THROWER;
    if (dying)
        look = isCatcher() ? THEME_OBJ_FOE_CATCHER_DYING : THEME_OBJ_FOE_THROWER_DYING;
    draw::Instance in = pose();
    if (dying) {
        in.conditions = draw::COND_DEAD;
        in.animCode = 0;
        in.animTime = 0.0f;
    }

    switch (pass) {
    case draw::Pass::Opaque:
        if (dying) {
            const bool fire = deathNumber_ > deathsDrawn;
            draw::fireEffects(f, look, fire);
            if (fire)
                draw::explode(f, look);
        }
        draw::objects(f, look, &in);
        break;
    case draw::Pass::Shadows:
        if (!dying)
            draw::shadows(f, look, &in);
        break;
    case draw::Pass::Particles:
        if (f.opts.particles > 0)
            draw::particles(f, look, &in);
        if (f.opts.particles > 1 && pickedUp_ == CONTENTS_CRYSTAL)
            draw::spawnBurst(f, THEME_OBJ_CRYSTAL_PICKUP, in.pos);
        break;
    default:
        break;
    }
}
