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

struct CStaticSoundbuffer;
struct VoicePool;
typedef VoicePool *(__attribute__((thiscall)) *acquire_pool_fn)(void *sm, int count,
                                                                 const char *name,
                                                                 int mode);
typedef void (__attribute__((thiscall)) *pool_wipe_fn)(VoicePool *vp);
#define ORIG_ACQUIRE_POOL  ((acquire_pool_fn)0x00443810)   /* named callback */
#define ORIG_POOL_WIPE     ((pool_wipe_fn)   0x00442a20)   /* named callback */

extern "C" __declspec(dllexport) void __attribute__((thiscall))
CStatic_Reset(CStaticSoundbuffer *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CStatic_Set3DPosition(CStaticSoundbuffer *self, float x, float y, float z,
                      DWORD dwApply);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
CStatic_TriggerPlayback(CStaticSoundbuffer *self, DWORD dwLoopFlags);
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_AcquireObjectSoundBuffersForIndex(void *self, unsigned int objArg);
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

#define G_SOUND_MGR 0x13cba8
#define G8(o)   (*(unsigned char *)(B + (o)))
#define G16(o)  (*(unsigned short *)(B + (o)))
#define G32(o)  (*(unsigned int *)(B + (o)))
#define GP(o)   (*(void **)(B + (o)))
#define GF(o)   (*(float *)(B + (o)))

static int s_fx = -1;

static CStaticSoundbuffer *acq(unsigned char *B, unsigned int nameOff)
{
    char name[256];
    strcpy(name, (const char *)(B + nameOff));
    return ((SoundManager *)(B + G_SOUND_MGR))->acquireStatic(name, 1);
}

static VoicePool *acq_pool(unsigned char *B, int count, unsigned int nameOff)
{
    char name[256];
    strcpy(name, (const char *)(B + nameOff));
    return ORIG_ACQUIRE_POOL(B + G_SOUND_MGR, count, name, 1);
}

/* Reset-if-set, then reacquire-if-named, for one of the eleven slots. */
static void reslot(unsigned char *B, unsigned int slot, unsigned int nameOff)
{
    if (GP(slot) != NULL)
        CStatic_Reset((CStaticSoundbuffer *)GP(slot));
    if (G32(nameOff + 0x100) != 0)
        GP(slot) = acq(B, nameOff);
}

/* Per-object sound for one slot table. */
static void per_object(unsigned char *B, unsigned int table, unsigned int countOff,
                       unsigned int nameOff, unsigned int field)
{
    for (unsigned short i = 0; i < G8(countOff); ++i) {
        if (G32(nameOff + 0x100) != 0) {
            CStaticSoundbuffer *p = acq(B, nameOff);
            *(CStaticSoundbuffer **)(*(unsigned char **)(B + table + i * 4) + field) = p;
        }
    }
}

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_InitLevelBasedSounds(void *self)
{
    unsigned char *B = (unsigned char *)self;
    const char *world = (const char *)(B + 0x2ab69d);

    if (s_fx < 0) {
        char e[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "worldcode") == 0);
        if (s_fx)
            log_write("levelsounds: KAROO_SIM_FX=worldcode -- Space/Candy swapped\n");
    }

    GameLog_LogMessage(GAMELOGGER, 2, F_TRYINIT);
    if (G32(0x13cc34) != 0) {
        G32(0x175323) = 0;
        if (strcmp(world, S_EGYPT) == 0)
            G32(0x175323) = 0;
        if (strcmp(world, S_SPACE) == 0)
            G32(0x175323) = s_fx ? 1 : 2;
        if (strcmp(world, S_CANDY) == 0)
            G32(0x175323) = s_fx ? 2 : 1;

        if (GP(0x175268) != NULL)
            ORIG_POOL_WIPE((VoicePool *)GP(0x175268));
        if (G32(0x442ca) != 0)
            GP(0x175268) = acq_pool(B, 3, 0x441ca);
        if (GP(0x175298) != NULL)
            ORIG_POOL_WIPE((VoicePool *)GP(0x175298));
        if (G32(0x4246e) != 0)
            GP(0x175298) = acq_pool(B, 10, 0x4236e);

        reslot(B, 0x17528c, 0x456ba);
        reslot(B, 0x175288, 0x42ef2);
        reslot(B, 0x17527c, 0x429b6);
        reslot(B, 0x175290, 0x4279e);
        reslot(B, 0x17526c, 0x428aa);
        reslot(B, 0x175280, 0x42ac2);
        reslot(B, 0x175284, 0x42ac2);
        reslot(B, 0x175274, 0x42586);
        reslot(B, 0x175278, 0x42692);
        reslot(B, 0x175294, 0x42de6);
        reslot(B, 0x175270, 0x44c42);

        for (unsigned short i = 0; i < G8(0x174fd4); ++i)
            Sim_AcquireObjectSoundBuffersForIndex(B, G8(0x174fd5 + i));

        for (unsigned short i = 0; i < G8(0x173e3e); ++i) {
            unsigned char *obj;
            if (G32(0x4320a) != 0) {
                CStaticSoundbuffer *p = acq(B, 0x4310a);
                obj = *(unsigned char **)(B + 0x173b1e + i * 4);
                *(CStaticSoundbuffer **)(obj + 0x4d) = p;
            }
            if (G32(0x43316) != 0) {
                CStaticSoundbuffer *p = acq(B, 0x43216);
                obj = *(unsigned char **)(B + 0x173b1e + i * 4);
                *(CStaticSoundbuffer **)(obj + 0x51) = p;
            }
        }
        per_object(B, 0x173719, 0x173b19, 0x42bce, 0x3a);
        per_object(B, 0x173588, 0x173718, 0x42cda, 0x39);
        per_object(B, 0x170643, 0x170a43, 0x42ffe, 0x47);

        if (G8(0x4220b) == 0 && G32(0x2ab564) != 0) {
            GameLog_LogMessage(GAMELOGGER, 1, F_TRYLEO);
            for (unsigned short i = 0; i < G16(0x13cba6); ++i) {
                unsigned char *E = B + (unsigned int)i * 0xf40;
                if (E[0x48ec2] != 3)
                    continue;
                const char *nm = (const char *)(E + 0x48ba6);
                GameLog_LogMessage(GAMELOGGER, 1, F_LEOSOUND, nm);
                CStaticSoundbuffer *p = ((SoundManager *)(B + G_SOUND_MGR))->acquireStatic(nm, 1);
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
