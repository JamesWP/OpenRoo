/* GAMETICK_PLAN.md Band B reopened — level-based sound initialisation.
 *
 *   Game::InitLevelBasedSounds  0x0041c360   1 E8 site (0x00414E17, GameTick)
 *
 * __thiscall(Game*), bare RET (the decompile's `void *param_1` is an unused
 * register artefact).  Transcribed from the LISTING.
 *
 * Callees:
 *   Log_Message 0x441b10             GameLog_LogMessage   (gamelog.cpp)
 *   CStaticSoundbuffer::Reset        CStatic_Reset        (static.cpp)
 *   Set3DPosition / TriggerPlayback  CStatic_*            (static.cpp)
 *   AcquireObjectSoundBuffersForIndex Sim_*               (soundobj.cpp)
 *   SoundManager::AcquireSoundBuffer 0x443660  } named callbacks -- the
 *   SoundManager::AcquireVoicePool   0x443810  } sound manager is not ours
 *   VoicePool::VoicePoolWipe         0x442a20  } (see soundobj.cpp)
 *
 * Order, all gated on SoundManager created (+0x13cc34):
 *   +0x175323 world code: 0, then 0 again for "Egypt", 2 for "Space", 1 for
 *     "Candy" -- the Egypt test is redundant with the initial clear; kept.
 *   pool3  (+0x175268): wipe if set, reacquire from +0x441ca, count 3
 *   pool10 (+0x175298): wipe if set, reacquire from +0x4236e, count 10
 *   eleven buffers, each Reset-if-set then reacquire-if-named:
 *     +0x17528c<-456ba  +0x175288<-42ef2  +0x17527c<-429b6  +0x175290<-4279e
 *     +0x17526c<-428aa  +0x175280<-42ac2  +0x175284<-42ac2 (same name twice)
 *     +0x175274<-42586  +0x175278<-42692  +0x175294<-42de6  +0x175270<-44c42
 *   each foe in the ID list: AcquireObjectSoundBuffersForIndex(id)
 *     (16-bit loop counter, as the listing's SI/CX compare)
 *   breakables (+0x173b1e, n +0x173e3e): +0x4d<-4310a, +0x51<-43216
 *   lifts      (+0x173719, n +0x173b19): +0x3a<-42bce
 *   slides     (+0x173588, n +0x173718): +0x39<-42cda
 *   bridges    (+0x170643, n +0x170a43): +0x47<-42ffe
 *   if +0x4220b == 0 and mode_3d (+0x2ab564): every LEO extra object
 *     (count WORD +0x13cba6, stride 0xf40) whose type byte +0x48ec2 == 3
 *     gets a buffer from its name at +0x48ba6 into +0x49ae2 and, if non-null,
 *     Set3DPosition(+0x48ca6, +0x48cae, -(+0x48caa), 1) and TriggerPlayback(1)
 *   then the closing log line.  Returns AL = 0.
 *
 * Every acquire passes a stack COPY of the name, as the listing does.
 *
 * Control: KAROO_SIM_FX=worldcode -- "Space" and "Candy" swap codes (1<->2).
 * +0x175323 is read by the simulation elsewhere; whether that reaches an
 * asserted field is measured, not assumed.
 */
#include <windows.h>
#include <string.h>
#include "log.h"
#include "soundmanager.h"
#include "game.h"
#include "player.h"
#include "soundobj.h"
#include "liftobject.h"
#include "slideobject.h"
#include "bridgeobject.h"
#include "breakabletile.h"

struct CStaticSoundbuffer;
struct VoicePool;
typedef void (__attribute__((thiscall)) *pool_wipe_fn)(VoicePool *vp);
#define ORIG_POOL_WIPE     ((pool_wipe_fn)   0x00442a20)   /* named callback */

extern "C" __declspec(dllexport) void __attribute__((thiscall))
CStatic_Reset(CStaticSoundbuffer *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CStatic_Set3DPosition(CStaticSoundbuffer *self, float x, float y, float z,
                      DWORD dwApply);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
CStatic_TriggerPlayback(CStaticSoundbuffer *self, DWORD dwLoopFlags);
extern "C" __declspec(dllexport) void __cdecl
GameLog_LogMessage(void *self, int level, const char *fmt, ...);

#define GAMELOGGER  ((void *)0x0046c4c0)
#define F_TRYINIT   ((const char *)0x004660bc)
#define F_TRYLEO    ((const char *)0x00466078)
#define F_LEOSOUND  ((const char *)0x00466058)
#define F_INITDONE  ((const char *)0x00466030)
#define S_EGYPT     ((const char *)0x004660b4)
#define S_SPACE     ((const char *)0x004660ac)
#define S_CANDY     ((const char *)0x004660a4)

#define G8(o)   (*(unsigned char *)(B + (o)))
#define G16(o)  (*(unsigned short *)(B + (o)))
#define G32(o)  (*(unsigned int *)(B + (o)))
#define GP(o)   (*(void **)(B + (o)))
#define GF(o)   (*(float *)(B + (o)))

static int s_fx = -1;

static CStaticSoundbuffer *acq(Game *game, const SoundAssetName *asset)
{
    char name[256];
    strcpy(name, asset->name);
    return game->soundManager()->acquireStatic(name, 1);
}

static VoicePool *acq_pool(Game *game, int count, const SoundAssetName *asset)
{
    char name[256];
    strcpy(name, asset->name);
    return game->soundManager()->acquirePool(count, name, 1);
}

/* Reset-if-set, then reacquire-if-named, for one of the eleven slots.  The
 * caller stores the result back: the new buffer if the asset is named, else
 * the (reset) buffer it passed -- the original left the slot untouched, and
 * storing the same value back is the same state. */
static CStaticSoundbuffer *reslot(Game *game, CStaticSoundbuffer *cur,
                                  const SoundAssetName *asset)
{
    if (cur != NULL)
        CStatic_Reset(cur);
    if (asset->enabled != 0)
        return acq(game, asset);
    return cur;
}

/* Give every object in one slot table its moving-loop sound: the lift,
 * slide and bridge each have one handle, set through setSound().  The count
 * and the slot are re-read every pass, as the listing does. */
template <typename T>
static void attachLoopSound(Game *game,
                            unsigned char (Game::*count)() const,
                            T *(Game::*slot)(unsigned int) const,
                            const SoundAssetName *asset)
{
    for (unsigned short i = 0; i < (game->*count)(); ++i) {
        if (asset->enabled != 0)
            (game->*slot)(i)->setSound(acq(game, asset));
    }
}

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_InitLevelBasedSounds(void *self)
{
    unsigned char *B = (unsigned char *)self;
    Game *game = (Game *)B;
    Player *pl = game->player();
    const char *world = (const char *)(B + 0x2ab69d);

    if (s_fx < 0) {
        char e[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "worldcode") == 0);
        if (s_fx)
            log_write("levelsounds: KAROO_SIM_FX=worldcode -- Space/Candy swapped\n");
    }

    GameLog_LogMessage(GAMELOGGER, 2, F_TRYINIT);
    if (((Game *)B)->soundCreated() != 0) {
        pl->setField15a(0);
        if (strcmp(world, S_EGYPT) == 0)
            pl->setField15a(0);
        if (strcmp(world, S_SPACE) == 0)
            pl->setField15a(s_fx ? 1 : 2);
        if (strcmp(world, S_CANDY) == 0)
            pl->setField15a(s_fx ? 2 : 1);

        if (pl->pool9f() != NULL)
            ORIG_POOL_WIPE(pl->pool9f());
        if (game->soundAsset441ca()->enabled != 0)
            pl->setPool9f(acq_pool(game, 3, game->soundAsset441ca()));
        if (pl->poolCf() != NULL)
            ORIG_POOL_WIPE(pl->poolCf());
        if (game->soundAsset4236e()->enabled != 0)
            pl->setPoolCf(acq_pool(game, 10, game->soundAsset4236e()));

        pl->setSoundC3(reslot(game, pl->soundC3(), game->soundAsset456ba()));
        pl->setSoundBf(reslot(game, pl->soundBf(), game->soundAsset42ef2()));
        pl->setSoundB3(reslot(game, pl->soundB3(), game->soundAsset429b6()));
        pl->setSoundC7(reslot(game, pl->soundC7(), game->soundAsset4279e()));
        pl->setSoundA3(reslot(game, pl->soundA3(), game->soundAsset428aa()));
        pl->setSoundB7(reslot(game, pl->soundB7(), game->soundAsset42ac2()));
        pl->setSoundBb(reslot(game, pl->soundBb(), game->soundAsset42ac2()));
        pl->setSoundAb(reslot(game, pl->soundAb(), game->soundAsset42586()));
        pl->setSoundAf(reslot(game, pl->soundAf(), game->soundAsset42692()));
        pl->setSoundCb(reslot(game, pl->soundCb(), game->soundAsset42de6()));
        pl->setSoundA7(reslot(game, pl->soundA7(), game->soundAsset44c42()));

        for (unsigned short i = 0; i < game->foeCount(); ++i)
            Sim_AcquireObjectSoundBuffersForIndex(game, game->foeId(i));

        for (unsigned short i = 0; i < game->breakableCount(); ++i) {
            if (game->soundAsset4310a()->enabled != 0) {
                CStaticSoundbuffer *p = acq(game, game->soundAsset4310a());
                game->breakableSlot(i)->setFallSound(p);
            }
            if (game->soundAsset43216()->enabled != 0) {
                CStaticSoundbuffer *p = acq(game, game->soundAsset43216());
                game->breakableSlot(i)->setRespawnSound(p);
            }
        }
        attachLoopSound(game, &Game::liftCount,   &Game::liftSlot,
                        game->soundAsset42bce());
        attachLoopSound(game, &Game::slideCount,  &Game::slideSlot,
                        game->soundAsset42cda());
        attachLoopSound(game, &Game::bridgeCount, &Game::bridgeSlot,
                        game->soundAsset42ffe());

        if (game->restartCount() == 0 && game->sound3D() != 0) {
            GameLog_LogMessage(GAMELOGGER, 1, F_TRYLEO);
            for (unsigned short i = 0; i < G16(0x13cba6); ++i) {
                unsigned char *E = B + (unsigned int)i * 0xf40;
                if (E[0x48ec2] != 3)
                    continue;
                const char *nm = (const char *)(E + 0x48ba6);
                GameLog_LogMessage(GAMELOGGER, 1, F_LEOSOUND, nm);
                CStaticSoundbuffer *p = game->soundManager()->acquireStatic(nm, 1);
                *(CStaticSoundbuffer **)(E + 0x49ae2) = p;
                if (p != NULL) {
                    CStatic_Set3DPosition(p, *(float *)(E + 0x48ca6),
                                          *(float *)(E + 0x48cae),
                                          -*(float *)(E + 0x48caa), 1);
                    CStatic_TriggerPlayback(*(CStaticSoundbuffer **)(E + 0x49ae2), 1);
                }
            }
        }
    }
    GameLog_LogMessage(GAMELOGGER, 2, F_INITDONE);
    return 0;
}
