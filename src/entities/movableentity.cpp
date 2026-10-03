/* MovableEntity's construction and destruction; the
 * shared movement step, updateMovement(), follows in the rest of the file. */

#include "portable.h"
#include <stdint.h>
#include "movableentity.h"
#include "sysdev.h"
#include "ani.h"
#include <stdlib.h>

MovableEntity::MovableEntity()
{
    posU_ = 0.0f;
    posY_ = 0.0f;
    posV_ = 0.0f;
    // updateMovement re-queues these for a kind 4 entity before anything has
    // set them, so they must start empty.
    queuedMove_ = 0;
    queuedTurn_ = 0;
}

MovableEntity::~MovableEntity()
{
}

void MovableEntity::zeroSoundSlots()
{
    sound_a7_ = 0;
    sound_ab_ = 0;
    sound_af_ = 0;
    sound_b3_ = 0;
    sound_b7_ = 0;
    sound_bb_ = 0;
    sound_bf_ = 0;
    sound_c3_ = 0;
    sound_c7_ = 0;
    sound_a3_ = 0;
    sound_cb_ = 0;
    sound_cf_ = 0;
}

/* MovableEntity::updateMovement(): the movement integrator every entity
 * (player, foe or bomb) ticks through -- grid stepping, falling, gliding,
 * riding lifts and slides, and the tile-triggered special cases (glue,
 * teleporter, conveyor, jump pad, climb).
 *
 * DETERMINISM: replays depend on the floating-point expressions below being
 * evaluated exactly as written; do not reassociate the fall- or glide-height
 * arithmetic, and keep the float/double split each constant below already has
 * -- a double 0.25 compared against a float expression rounds differently from
 * a float 0.25.
 *
 * Returns 1 on the two early-out paths taken while the entity is dying, 0
 * otherwise. */
#include <string.h>
#include <math.h>
#include "audiodev.h"
#include "logger.h"
#include "entitymath.h"
#include "voicepool.h"
#include "movableentity.h"
#include "tile.h"
#include "levelmap.h"

/* Each constant keeps the type (float or double) it is used as; the split is
 * load-bearing for the arithmetic above. */
static const double K_ZERO         = 0.0;
static const double K_ONE          = 1.0;
static const double K_HALF_D       = 0.5;
static const float  K_HALF_F       = 0.5f;
static const double K_ONE_HALF     = 1.5;
static const double K_MS_TO_S_D    = 0.001;
static const float  K_MS_TO_S_F    = 0.001f;
static const double K_TWO_MS       = 0.002;
static const float  K_GRAVITY_HALF = 4.905f;
static const float  K_GRAVITY_TWO  = 19.62f;
static const float  K_ARC_BIAS     = 7.2f;
static const float  K_FALL_ACCEL   = 5.405f;
static const float  K_GLIDE_RATE   = 0.004f;
static const float  K_SNAP_EPS     = 0.15f;
static const double K_LINK_EPS     = 0.25;
static const double K_GLUE_MS      = 3000.0;
static const double K_HALF_SEC_MS  = 500.0;
static const double K_IDLE_MS      = 5000.0;
static const float  K_GLIDE_DROP   = 2.0f;
static const double K_RAMP_EPS     = 0.2;

/* KAROO_SIM_FX, read by value: "floaty" divides the fall acceleration by five
 * so a fatal drop visibly slows; "hop" quadruples the jump-pad arc so a pad
 * throws the entity far higher than normal. */
static int s_fx = -1;
static void fx_init(void)
{
    if (s_fx >= 0)
        return;
    char buf[32];
    uint32_t n = sysdev::getEnv("KAROO_SIM_FX", buf, sizeof(buf));
    s_fx = 0;
    if (n > 0 && n < sizeof(buf)) {
        if (strcaseCompare(buf, "floaty") == 0) {
            s_fx = 1;
            g_logger.write("entitymove: KAROO_SIM_FX=floaty -- fall accel %.3f, not %.3f\n",
                      (double)(K_FALL_ACCEL / 5.0f), (double)K_FALL_ACCEL);
        } else if (strcaseCompare(buf, "hop") == 0) {
            s_fx = 2;
            g_logger.write("entitymove: KAROO_SIM_FX=hop -- arc bias %.1f, not %.1f\n",
                      (double)(K_ARC_BIAS * 4.0f), (double)K_ARC_BIAS);
        }
    }
}
static inline float fall_accel(void) { return s_fx == 1 ? K_FALL_ACCEL / 5.0f : K_FALL_ACCEL; }
static inline float arc_bias(void)   { return s_fx == 2 ? K_ARC_BIAS * 4.0f  : K_ARC_BIAS; }

/* Copies eight bytes rather than assigning a double, so a stored NaN payload
 * survives identically. */
static inline void copy8(void *dst, const void *src) { memcpy(dst, src, 8); }

/* The tick reads the 8-byte record copy at tickStepCopy_ as a double (the
 * glide height). */
static inline double load_double(const void *p)
{
    double v;
    memcpy(&v, p, 8);
    return v;
}

/* Tile addressing: base->tile( u, v), both axes read signed. */
#define TILE(u, v)   map_->tile( (u), (v))
#define GU           cellU_
#define GV           cellV_
#define GH           heightCell_
#define CUR          TILE(GU, GV)

/* Positions a sound at (x, y, z) and triggers it; a null sound is skipped. */
static inline void snd_at(audiodev::Buffer *s, float x, float y, float z, uint32_t loop)
{
    if (s == 0)
        return;
    s->setPosition(x, y, z, 1);
    s->play(loop);
}

/* Is direction `d` the entity's facing, or its reverse? The second check runs
 * only when the first fails; GetTurnedDirection is pure, so the short-circuit
 * is unobservable either way. */
static inline bool facing_or_reverse(unsigned d, unsigned char facing)
{
    if (d == (unsigned)facing)
        return true;
    return d == (unsigned)Sim_GetTurnedDirection(facing, 2);
}

/* DETERMINISM: truncates toward zero into a 64-bit result and keeps only the
 * low dword. PRESERVED: the high dword is never populated, so a negative
 * elapsed time reads back as a huge positive one. */
static inline unsigned ftol32(double v)
{
    return (unsigned)(long long)v;
}

unsigned int MovableEntity::updateMovement()
{
    fx_init();

    if (moveDir_ == 0 && ((signed char)moveState_) == 0)
        anim_ = 0;

    if (idleStarted_ == 0 && climbing_ == 0)
        copy8(&animDuration_, &stepDuration_);  // move duration <- default

    // The two early outs. Both return 1.
    if (dying_ != 0) {
        if (dyingStarted_ == 0) {
            copy8(&dyingSince_, &now_);
            dyingStarted_ = 1;
            field_7a = (void *)1;
            pendingMove_ = 0;
            moveDir_ = 0;
            return 1;
        }
        if (!(now_ - dyingSince_ < K_HALF_SEC_MS))
            removeRequested_ = 1;
        pendingMove_ = 0;
        moveDir_ = 0;
        return 1;
    }

    if (held_ != 0)  // frozen: drop the queued move
        pendingMove_ = 0;

    bool turned = false;

    if (moveDir_ != 0) {
        // A move is in progress.
        if (((signed char)kind_) != 9 && ((signed char)kind_) != 3) {
            unsigned crush = CUR->blastHeight();
            if ((int)GH - 1 <= (int)crush && (int)crush <= (int)GH + 1 &&
                animDuration_ * K_MS_TO_S_D < now_ - animStart_)
                moveState_ = 4;
        }

        if (animDuration_ <= now_ - animStart_) {
            // the step has run its time: commit it
            movingBackwards_ = 0;
            stepEnd_ = animDuration_ + animStart_;

            if (facing_or_reverse(((unsigned)moveDir_), facing_)) {
                if (((signed char)kind_) != 9) {
                    // Clears the occupant of the cell being left.
                    Tile *from = map_->tile(
                        (int)GU - (int)stepU_, (int)GV - (int)stepV_);
                    from->setOccupant(0);
                }
                if (Sim_CheckTileIsRamp(CUR->objectMarker()) != 0) {
                    posY_ = (float)(int)GH + K_HALF_F;
                } else if (gliding_ == 0) {
                    posY_ = (float)(int)GH;
                }
                if ((unsigned)lastMoveDir_ != ((unsigned)moveDir_)) {
                    posU_ = (float)(int)GU;
                    posV_ = (float)(int)GV;
                }
                if (climbing_ != 0)
                    posY_ = (float)(int)GH;
                field_d8 = field_d8 + 1;
                lastMoveDir_ = (unsigned char)moveDir_;
            } else {
                turned = true;
            }

            if (((signed char)field_124) == 0) {
                facing_ = (unsigned char)moveDir_;
            } else {
                int d = moveDir_;
                if (d > 10 && d < 0x14)
                    facing_ = (signed char)(d - 10);
            }

            if (((signed char)kind_) != 9) {
                unsigned char a = anim_;
                if (((a > 0x13 && a < 0x1c && moveDir_ != 0) || turned) &&
                    ((VoicePool *)sound_cf_) != 0 && a != 3 && a != 5) {
                    (((VoicePool *)sound_cf_))->broadcastCoordinates((float)(int)GU, (float)(int)GH, -(float)(int)GV, 1);
                    (((VoicePool *)sound_cf_))->cycle(0);
                }
            }

            moveDir_ = 0;

            // kind 4 re-queues whatever the input left in
            // pendingMove_/turnKind_
            Tile *t = CUR;
            signed char k = (signed char)t->objectMarker();
            if (((signed char)kind_) == 4) {
                if (k == 0x15 || k == 0x0f || k == 0x10) {
                    if ((unsigned)t->height() != (unsigned)(int)GH && k != 0x0e) {
                        pendingMove_ = queuedMove_;
                        turnKind_ = queuedTurn_;
                    }
                } else {
                    pendingMove_ = queuedMove_;
                    turnKind_ = queuedTurn_;
                }
            }
            queuedMove_ = 0;
            queuedTurn_ = 0;
        }
    }

    if (moveDir_ == 0) {
        // Idle, standing on a tile.
        if (((signed char)kind_) != 9 && ((signed char)kind_) != 3) {
            unsigned crush = CUR->blastHeight();
            if ((int)GH - 1 <= (int)crush && (int)crush <= (int)GH + 1)
                moveState_ = 4;
        }

        Tile *t = CUR;
        if ((signed char)t->objectMarker() == TILE_SWITCH && (unsigned)t->height() == (unsigned)(int)GH &&
            ((signed char)kind_) != 9 && field_d3 == 0) {
            field_d7 = t->field1f3();
            snd_at(sound_cb_, (float)(int)GU, (float)(int)GH, -(float)(int)GV, 0);
            field_d3 = 1;
        }

        t = CUR;
        if ((unsigned)(int)GH == (unsigned)t->height()) {
            // PRESERVED: the glue pad freezes foes as well as the player --
            // there is no kind check gating it to the player alone.
            if ((signed char)t->objectMarker() == TILE_GLUE && t->busy() == 0) {
                if (field_126 == K_ZERO) {
                    copy8(&field_126, &now_);
                    if (field_156 != 0)
                        snd_at(sound_bf_, (float)(int)GU, (float)(int)GH,
                               -(float)(int)GV, 0);
                    anim_ = 9;
                }
                if (now_ - field_126 <= K_GLUE_MS) {
                    pendingMove_ = 0;  // stuck: drop the queued move
                } else {
                    field_126 = 0.0;
                    CUR->setBusy(1);  // pad spent
                }
            } else {
                field_126 = 0.0;
            }

            // Climb tile.
            Tile *c = CUR;
            if ((signed char)c->objectMarker() == TILE_CLIMB) {
                unsigned char dir = c->climbDir();
                if (climbing_ == 0)
                    snd_at(sound_af_, (float)(int)GU, (float)(int)GH,
                           -(float)(int)GV, 1);
                facing_  = dir;
                climbing_ = 1;
                pendingMove_ = dir;
                animDuration_ = 150.0;  // ms, stored as a double
            } else {
                copy8(&animDuration_, &stepDuration_);
                climbing_ = 0;
                if (sound_af_ != 0)
                    sound_af_->stop();
            }

            // Teleporter tile.
            if ((signed char)CUR->objectMarker() == TILE_TELEPORTER) {
                pendingMove_ = 0;
                if (((signed char)teleportPhase_) == 1 && K_HALF_SEC_MS <= now_ - teleportSince_) {
                    copy8(&teleportSince_, &now_);
                    teleportPhase_ = 2;
                    CUR->setBusy(0);
                    LevelMap *base = map_;
                    Tile *here = CUR;
                    signed char du = (signed char)here->teleportU();
                    signed char dv = (signed char)here->teleportV();
                    signed char dh = (signed char)base->tile( du, dv)->height();
                    if (((signed char)kind_) != 9)
                        here->setOccupant(0);
                    cellU_ = du;
                    cellV_ = dv;
                    heightCell_ = dh;
                    posU_ = (float)(int)du;
                    posY_ = (float)(int)dh;
                    posV_ = (float)(int)dv;
                    CUR->setBusy(1);
                    snd_at(sound_bb_, (float)(int)GU, (float)(int)GH,
                           -(float)(int)GV, 0);
                }
                if (((signed char)teleportPhase_) == 0) {
                    copy8(&teleportSince_, &now_);
                    teleportPhase_ = 1;
                    CUR->setBusy(1);
                    snd_at(sound_b7_, (float)(int)GU, (float)(int)GH,
                           -(float)(int)GV, 0);
                }
                if (((signed char)teleportPhase_) == 2 && K_HALF_SEC_MS <= now_ - teleportSince_) {
                    teleportPhase_ = 0;
                    CUR->setBusy(0);
                    pendingMove_ = lastMoveDir_;
                }
            }

            // Attach to a moving platform.
            if (((signed char)slideSlot_) == -1) {
                LevelMap *base = map_;
                Tile *here = CUR;
                if ((signed char)here->objectMarker() == TILE_SLIDE_TRACK) {
                    unsigned pu = here->slideOriginU();
                    unsigned pv = here->slideOriginV();
                    Tile *p = base->tile( pu, pv);
                    if (fabsf((p->slidePosU() + (float)K_HALF_D) -
                              (posU_ + K_HALF_F)) <= (float)K_LINK_EPS &&
                        fabsf((p->slidePosV() + (float)K_HALF_D) -
                              (posV_ + K_HALF_F)) <= (float)K_LINK_EPS &&
                        (float)here->height() <= posY_) {
                        slideSlot_ = here->slideSlot();
                    }
                }
            }

            // Conveyor tile.
            if ((signed char)CUR->objectMarker() == TILE_CONVEYOR) {
                if (conveyorDir_ == 0) {
                    pendingMove_ = lastMoveDir_;
                    turnKind_ = 0;
                    snd_at(sound_ab_, (float)(int)GU, (float)(int)GH,
                           -(float)(int)GV, 1);
                } else {
                    unsigned char d = (unsigned char)conveyorDir_;
                    pendingMove_ = d;
                    lastMoveDir_ = d;
                    turnKind_ = 0;
                }
                anim_ = 3;
                conveyorDir_ = (unsigned)lastMoveDir_;
            } else {
                conveyorDir_ = 0;
            }
        }

        Tile *t2 = CUR;
        if ((signed char)t2->objectMarker() != TILE_CONVEYOR ||
            (unsigned)t2->height() != (unsigned)(int)GH) {
            if (sound_ab_ != 0)
                sound_ab_->stop();
            conveyorDir_ = 0;
        }
    }

    // Standing on a lift at its current height.
    {
        Tile *t = CUR;
        if ((signed char)t->objectMarker() == TILE_LIFT && (unsigned)t->height() == (unsigned)(int)GH)
            onLift_ = 1;
        if (onLift_ != 0 && ((signed char)moveState_) == 0) {
            heightCell_ = t->height();
            posY_ = CUR->liftLiveHeight();
        }
    }

    // Riding a platform.
    if (((signed char)slideSlot_) != -1 && ((signed char)moveState_) == 0) {
        LevelMap *base = map_;
        Tile *here = CUR;
        unsigned pu = here->slideOriginU();
        unsigned pv = here->slideOriginV();
        Tile *p = base->tile( pu, pv);
        cellU_ = p->slideCellU();
        cellV_ = p->slideCellV();
        posU_  = p->slidePosU();
        posY_  = p->slidePosY();
        posV_  = p->slidePosV();
        if (((signed char)kind_) != 9)
            CUR->setOccupant(((signed char)kind_));
        if (facing_or_reverse((unsigned)pendingMove_, facing_)) {
            if (K_SNAP_EPS < posU_ - (float)(int)GU ||
                K_SNAP_EPS < posV_ - (float)(int)GV)
                pendingMove_ = 0;
            else
                slideSlot_ = 0xff;
        }
    }

    if (falling_ != 0 && gliding_ == 0) {
        pendingMove_ = 0;
        turnKind_ = 0;
    }

    // Ground contact, falling and landing.
    if (moveDir_ == 0 || falling_ != 0) {
        signed char restore = 0;
        Tile *t = CUR;

        if ((signed char)t->objectMarker() == TILE_EMPTY && (signed char)t->height() != 0) {
            restore = (signed char)t->height();
            t->setHeight(0);
        }
        if (((signed char)moveState_) == 0 && GH < 0)
            moveState_ = 2;  // fell below the floor

        t = CUR;
        bool go_fall;
        if ((signed char)t->objectMarker() == TILE_EMPTY && GH > -100) {
            go_fall = true;
        } else {
            signed char h    = GH;
            unsigned char th = t->height();
            go_fall = ((int)th < (int)h) ||
                      (gliding_ != 0 && moveDir_ != 0) ||
                      ((int)h < (int)th && th != 0 && h > -100);

            if (!go_fall && falling_ != 0) {
                // Landed.
                // PRESERVED: bombs (kind 9) skip this occupant bookkeeping
                // entirely, so a bomb landing leaves the tile's occupant byte
                // stale rather than clearing or setting it.
                if (((signed char)kind_) != 9) {
                    climbing_ = 0;
                    if ((signed char)CUR->occupant() != 0)
                        dying_ = 1;  // landed on someone
                    CUR->setOccupant(kind_);
                }
                if (gliding_ == 0) {
                    signed char h2 = GH;
                    if ((signed char)CUR->objectMarker() == TILE_JUMP_PAD ||
                        ((int)((unsigned)fallStartH_ - (int)h2) < 3 && h2 > 1)) {
                        moveState_ = 0;  // survived
                        if (((VoicePool *)sound_cf_) != 0 && ((signed char)kind_) == 4) {
                            (((VoicePool *)sound_cf_))->broadcastCoordinates((float)(int)GU, (float)(int)GH,
                                -(float)(int)GV, 1);
                            (((VoicePool *)sound_cf_))->cycle(0);
                        }
                    } else if (((signed char)kind_) == 9 && h2 > 1) {
                        moveState_ = 0;
                        pendingMove_ = 0;
                    } else {
                        moveState_ = 2;  // killed by the drop
                        if (field_156 != 0 || ((signed char)kind_) == 2 || ((signed char)kind_) == 3)
                            snd_at(sound_a7_, (float)(int)GU, (float)(int)GH,
                                   -(float)(int)GV, 0);
                        if (sound_c3_ != 0)
                            sound_c3_->stop();
                        pendingMove_ = 0;
                    }
                }
                if (sound_c7_ != 0)
                    sound_c7_->stop();
                gliding_  = 0;
                anim_   = 0;
                falling_ = 0;
                posU_ = (float)(int)GU;
                posV_ = (float)(int)GV;
                if (Sim_CheckTileIsRamp(CUR->objectMarker()) != 0)
                    posY_ = (float)(int)GH + K_HALF_F;
                else
                    posY_ = (float)(int)GH;
            }
        }

        if (go_fall) {
            // Not falling yet, or continuing a fall in progress.
            if (falling_ == 0) {
                // not falling yet: start the fall
                copy8(&animDuration_, &stepDuration_);
                climbing_ = 0;
                if (sound_af_ != 0)
                    sound_af_->stop();
                if (onLift_ == 0) {
                    fallSpeed_   = -3.0f;                         // starts at -3.0
                    fallStartH_  = ((unsigned char)heightCell_);  // fall start height
                    falling_ = 1;
                    posY_    = (float)((unsigned char)heightCell_);
                    copy8(&fallStart_, &now_);
                    pendingMove_  = 0;
                }
            } else {
                if (((signed char)kind_) != 9)
                    CUR->setOccupant(0);

                unsigned elapsed = ftol32(now_ - fallStart_);
                double   ms      = (double)elapsed;

                if (gliding_ == 0) {
                    // Free fall.
                    if (sound_c3_ != 0)
                        sound_c3_->setPosition(posU_, posY_,
                                              -posV_, 1);
                    if (facing_or_reverse((unsigned)pendingMove_, facing_))
                        pendingMove_ = 0;

                    double ts = ms * (double)K_MS_TO_S_F;
                    double h  = ((double)fallSpeed_ - ts * (double)fall_accel()) * ts
                                + (double)(unsigned)fallStartH_;
                    posY_ = (float)h;
                    heightCell_ = (signed char)((unsigned char)(unsigned)(long long)h + 1);  // PRESERVED: truncates to a byte and wraps at 256 rather than saturating

                    if (gliding_ == 0 &&
                        (int)((unsigned)fallStartH_ - (int)GH) > 2 &&
                        ((signed char)glides_) != 0) {
                        // the paraglider opens: costs one charge
                        glides_ = (signed char)(((signed char)glides_) - 1);
                        gliding_ = 1;
                        snd_at(sound_a3_, posU_, posY_, -posV_, 0);
                        snd_at(sound_c7_, posU_, posY_, -posV_, 1);
                    }

                    int drop = (int)((unsigned)fallStartH_ - (int)GH);
                    if (drop > 2 && gliding_ == 0 && drop < 6 && ((signed char)kind_) != 9) {
                        anim_ = 8;
                        snd_at(sound_c3_, posU_, posY_, -posV_, 0);
                    }

                    if (gliding_ != 0 ||
                        (K_GLIDE_DROP < (float)fallStartH_ - (float)(int)GH &&
                         ((signed char)glides_) != 0)) {
                        anim_ = 5;
                    }
                } else {
                    // Gliding.
                    if (sound_c7_ != 0)
                        sound_c7_->setPosition(posU_, posY_,
                                              -posV_, 1);
                    unsigned char th = CUR->height();
                    if ((float)th < posY_ || posY_ < (float)th - K_HALF_F) {
                        double h = (double)posY_ - load_double(&tickStepCopy_) * (double)K_GLIDE_RATE;
                        posY_ = (float)h;
                        heightCell_ = (signed char)((unsigned char)(unsigned)(long long)h + 1);  // PRESERVED: same byte wrap as the free-fall case above
                    } else {
                        heightCell_ = th;
                        posY_  = (float)(int)(signed char)th;
                    }
                    if (GH < 2)
                        gliding_ = 0;
                    anim_ = 5;
                }

                // A ramp lets the entity settle half a step lower.
                if (Sim_CheckTileIsRamp(CUR->objectMarker()) != 0) {
                    unsigned char th = CUR->height();
                    if ((float)th < posY_ &&
                        posY_ <= (float)th + (float)K_HALF_D)
                        heightCell_ = (signed char)(GH - 1);
                }
            }
        }

        if (restore != 0)
            CUR->setHeight(restore);
        if (falling_ != 0 && ((signed char)kind_) != 9)
            CUR->setOccupant(0);
    }

    // Jump pad, then start a queued move.
    if (moveDir_ == 0) {
        Tile *t = CUR;
        if ((signed char)t->objectMarker() == TILE_JUMP_PAD) {
            if ((unsigned)(int)GH == (unsigned)t->height())
                pendingMove_ = 0;

            if (field_11a == 0) {
                if ((unsigned)CUR->height() == (unsigned)(int)GH) {
                    snd_at(sound_b3_, (float)(int)GU, (float)(int)GH,
                           -(float)(int)GV, 0);
                    copy8(&fallStart_, &now_);  // the pad launch clock
                    field_11a = 1;
                    posY_ = (float)(int)GH;
                }
            } else {
                double ts = (now_ - fallStart_) * (double)K_MS_TO_S_F;
                Tile *c = CUR;
                unsigned char top = c->field1f1();
                if (posY_ < (float)top) {
                    double v0 = sqrt(((double)(unsigned char)(top - (unsigned char)GH)
                                      + (double)arc_bias()) * (double)K_GRAVITY_TWO);
                    posY_ = (float)((v0 - ts * (double)K_GRAVITY_HALF) * ts
                                      + (double)(int)GH);
                    if (sound_b3_ != 0)
                        sound_b3_->setPosition((float)(int)GU,
                                              (float)(int)GH, -(float)(int)GV, 1);
                    anim_ = 0x0b;
                } else {
                    if (((signed char)kind_) != 9)
                        c->setOccupant(0);
                    signed char nh = (signed char)CUR->field1f1();
                    field_11a = 0;
                    heightCell_   = nh;
                    posY_    = (float)(int)nh;
                    pendingMove_  = lastMoveDir_;
                    anim_   = 0x0c;
                }
            }
        }

        if (((signed char)moveState_) != 0)
            pendingMove_ = 0;

        if (moveDir_ == 0 && pendingMove_ != 0) {
            // Start the queued move.
            field_141 = 0;
            moveDir_ = (unsigned)pendingMove_;
            pendingMove_ = 0;
            stepU_ = 0;
            stepV_ = 0;
            if (climbing_ != 0)
                field_141 = 0xff;
            if (moveDir_ > 0x14)
                moveDir_ = moveDir_ - 0x14;  // PRESERVED: subtraction, not a modulo; relies on moveDir_ staying under 0x28
            if (moveDir_ == 1) stepV_ = 0xff;
            if (moveDir_ == 2) stepU_ = 1;
            if (moveDir_ == 3) stepV_ = 1;
            if (moveDir_ == 4) stepU_ = 0xff;

            Tile          *here    = CUR;
            unsigned char  h_here  = here->height();
            unsigned char  k_here  = here->objectMarker();
            Tile          *dest    = map_->tile(
                (int)stepU_ + (int)GU, (int)GV + (int)stepV_);
            unsigned char  h_dest  = dest->height();
            unsigned char  k_dest  = dest->objectMarker();
            field_12e = 0;

            if (gliding_ != 0 && k_dest != 0 && (int)GH == (int)h_dest - 1)
                moveDir_ = 0;
            if (k_dest == 0x0f)
                field_ee = (unsigned char)moveDir_;

            // Occupied destination blocks, with per-kind exceptions.
            if (facing_or_reverse(((unsigned)moveDir_), facing_)) {
                signed char occ = (signed char)dest->occupant();
                if (occ != 0) {
                    signed char me = ((signed char)kind_);
                    if (me == 3 || (me == 4 && occ == 3) || (me == 2 && occ != 4))
                        moveDir_ = 0;
                }
            }
            if (field_d3 != 0 && facing_or_reverse(((unsigned)moveDir_), facing_))
                field_d3 = 0;

            if (k_dest == 0x10 && k_here == 0x10) {
                anim_ = 4;
            } else {
                if (k_here == 9 && facing_or_reverse(((unsigned)moveDir_), facing_)) {
                    if ((float)K_RAMP_EPS < fabsf(posY_ - (float)(int)GH) ||
                        (int)GH == (int)h_dest - 1)
                        moveDir_ = 0;
                    else
                        onLift_ = 0;
                }
                if (k_dest == 0x16 ||
                    (k_dest == 0x17 && dest->busy() == 0))
                    moveDir_ = 0;
            }

            if (moveDir_ != 0 && anim_ > 0xf9)
                anim_ = 0;

            // Ramp bookkeeping: which climb animation, and whether the move is
            // allowed.
            if (Sim_CheckTileIsRamp(k_here) != 0) {
                if (Sim_CheckTileIsRamp(k_dest) == 0 && h_here == h_dest)
                    anim_ = 0x1b;
                if (((unsigned)moveDir_) == (unsigned)(k_here - 4) ||
                    ((unsigned)moveDir_) == (unsigned)Sim_GetTurnedDirection(
                                      (unsigned char)(k_here - 4), 2)) {
                    if (h_here < h_dest) {
                        field_141  = 1;
                        field_12e = 1;
                        if (((signed char)anim_) == 0)
                            anim_ = (unsigned char)
                                ((-(Sim_CheckTileIsRamp(k_dest) != 0) & 0xfeU) + 0x1a);
                    }
                    if (h_dest == h_here) {
                        if (((signed char)anim_) == 0)
                            anim_ = (unsigned char)
                                ((-(Sim_CheckTileIsRamp(k_dest) != 0) & 0xfeU) + 0x1b);
                        field_12e = 2;
                    }
                } else if ((unsigned)h_dest == (unsigned)h_here + 1) {
                    moveDir_ = 0;
                }
            }

            if (Sim_CheckTileIsRamp(k_dest) == 0) {
                if (gliding_ == 0 && Sim_CheckTileIsRamp(k_here) == 0 &&
                    Sim_CheckTileIsRamp(k_dest) == 0) {
                    if (((signed char)anim_) == 0) {
                        if (((unsigned)moveDir_) == (unsigned)facing_)
                            anim_ = 0x14;
                        if (((unsigned)moveDir_) ==
                            (unsigned)Sim_GetTurnedDirection(facing_, 2))
                            anim_ = 0x15;
                    }
                    if ((unsigned)h_dest == (unsigned)h_here + 1)
                        moveDir_ = 0;
                }
            } else {
                if (h_dest < h_here) {
                    if (((signed char)anim_) == 0)
                        anim_ = (unsigned char)
                            ((-(Sim_CheckTileIsRamp(k_here) != 0) & 2U) + 0x17);
                    field_141  = 0xff;
                    field_12e = 2;
                }
                if (h_dest == h_here) {
                    if (((signed char)anim_) == 0)
                        anim_ = (unsigned char)
                            ((-(Sim_CheckTileIsRamp(k_here) != 0) & 2U) + 0x16);
                    field_12e = 1;
                }
            }

            // A second occupancy test, this one on the real step only.
            {
                signed char occ = (signed char)dest->occupant();
                if (occ != 0 && (stepU_ != 0 || stepV_ != 0) &&
                    ((signed char)kind_) != 9) {
                    if (field_156 == 0) {
                        if (occ != 4)
                            moveDir_ = 0;
                    } else if (occ == 3) {
                        moveDir_ = 0;
                    }
                }
            }

            if (climbing_ != 0 && k_dest != 0x10)
                anim_ = 0x14;

            if (moveDir_ != 0) {
                // Commit: pick the step's start time and move the grid cell.
                double since = now_ - stepEnd_;
                if (since <= K_ZERO || stepGrace_ <= since)
                    copy8(&animStart_, &now_);
                else
                    copy8(&animStart_, &stepEnd_);

                if (((signed char)slideSlot_) == -1) {
                    if ((unsigned)lastMoveDir_ != ((unsigned)moveDir_)) {
                        posU_ = (float)(int)GU;
                        posV_ = (float)(int)GV;
                    }
                    int nu = (int)GU + (int)stepU_;
                    int nv = (int)GV + (int)stepV_;
                    if (nu < 0 || (int)(unsigned)map_->extentU() <= nu ||
                        nv < 0 || (int)(unsigned)map_->extentV() <= nv) {
                        moveDir_ = 0;  // off the edge of the map
                    } else {
                        cellU_ = (signed char)(GU + stepU_);
                        cellV_ = (signed char)(GV + stepV_);
                        heightCell_ = (signed char)(GH + field_141);
                    }
                }

                movingBackwards_ = (((signed char)turnKind_) == 3) ? 1 : 0;

                if (gliding_ == 0) {
                    if (((signed char)turnKind_) == 2) anim_ = 0x1e;
                    if (((signed char)turnKind_) == 4) anim_ = 0x1f;
                } else {
                    anim_ = 5;
                }

                if (((signed char)kind_) != 9)
                    CUR->setOccupant(((signed char)kind_));

                idleStarted_ = 0;
                copy8(&lastActive_, &now_);
            }
        }
    }

    // Height curves for the animation states.
    onStairOrSlide_ = 0;
    if (((unsigned)moveDir_) == 0) {
        if (stepGrace_ <= now_ - stepEnd_ && ((signed char)slideSlot_) == -1) {
            posU_ = (float)(int)GU;
            posV_ = (float)(int)GV;
        }
    } else if (facing_or_reverse(((unsigned)moveDir_), facing_)) {
        double dur  = animDuration_ * K_TWO_MS;
        double inv  = K_ONE / dur;
        double tsec = (now_ - animStart_) * K_MS_TO_S_D;
        double bias = K_ZERO;
        double frac = now_ - animStart_;
        double gh   = (double)(int)GH;

        if (((signed char)kind_) == 9 && field_d8 == 0) {
            bias = tsec * inv - (inv / dur) * tsec * tsec * K_HALF_D;
            posY_ = (float)(gh + bias);
        }
        switch (((signed char)anim_)) {
        case 0x16:
            posY_ = (float)(gh + (K_ONE / animDuration_) * frac * K_HALF_D + bias);
            break;
        case 0x17:
            posY_ = (float)((gh + bias + K_ONE)
                              - (K_ONE / animDuration_) * frac * K_HALF_D);
            break;
        case 0x1a:
            posY_ = (float)((gh + bias) - K_HALF_D
                              + (K_HALF_D / animDuration_) * frac);
            break;
        case 0x1b:
            posY_ = (float)(((gh + bias) - K_HALF_D + K_ONE)
                              - (K_ONE / animDuration_) * frac * K_HALF_D);
            break;
        case 0x18:
            posY_ = (float)((gh + bias) - K_HALF_D
                              + (K_ONE / animDuration_) * frac);
            break;
        case 0x19:
            posY_ = (float)((gh + bias + K_ONE_HALF)
                              - (K_ONE / animDuration_) * frac);
            break;
        default:
            break;
        }
    }

    // Interpolate the horizontal position across the step.
    {
        double frac = (now_ - animStart_) / animDuration_;
        switch (moveDir_ - 1) {
        case 0: posV_ = (float)((double)(GV + 1) - frac); break;
        case 1: posU_ = (float)(frac + (double)(GU - 1)); break;
        case 2: posV_ = (float)(frac + (double)(GV - 1)); break;
        case 3: posU_ = (float)((double)(GU + 1) - frac); break;
        default: break;
        }
        if (climbing_ != 0)  // climbing: interpolate height instead
            posY_ = (float)((double)(GH + 1) - frac);
    }

    // Footstep / idle bookkeeping.
    if (movingBackwards_ != 0) {
        signed char a = ((signed char)anim_);
        if (a == ANIM_STAIR_STAIR_DOWN || a == ANIM_FIELD_STAIR_DOWN ||
            a == ANIM_STAIR_FIELD_UP || a == ANIM_FIELD_STAIR_UP ||
            a == ANIM_STAIR_STAIR_UP)
            onStairOrSlide_ = 1;
    }
    {
        unsigned char a = anim_;
        if (a == ANIM_FIELD_STAIR_DOWN) onStairOrSlide_ = 1;
        if (a == ANIM_SLIDE)            onStairOrSlide_ = 1;
        if (a < 0xfa && a != 0) {
            copy8(&lastActive_, &now_);
            idleStarted_ = 0;
        }
    }

    // The idle timeout: five seconds standing still starts the idle animation.
    bool run_idle;
    if (anim_ == 0) {
        double dv = now_ - lastActive_;
        if (dv < K_IDLE_MS) {
            run_idle = (idleStarted_ != 0);
        } else if (idleStarted_ == 0) {
            copy8(&animStart_, &now_);
            idleStarted_ = 1;
            run_idle = true;
        } else {
            run_idle = true;
        }
    } else {
        run_idle = (idleStarted_ != 0);
    }

    if (run_idle) {
        double dur = idleDuration_;
        anim_   = 0xfa;
        animDuration_   = dur;
        if (now_ - animStart_ >= dur)
            copy8(&animStart_, &now_);
    }

    return 0;
}

