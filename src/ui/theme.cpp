/* The theme file loader.
 *
 * FORMAT: a .thm is text, read with fgets into 0x100-byte lines in text mode
 * (the delimiters " \t\n" have no '\r', so binary mode would match nothing).
 * Leading spaces and tabs are skipped, "//" lines and blank lines dropped, and
 * each line split into up to 16 tokens.
 *
 * PRESERVED in the tokenizer:
 *   - a line longer than 0x100 is split, and its tail parses as a new line;
 *   - the whitespace skip comes before the comment test, so "   // x" is a
 *     comment but "Model x // y" yields "//" and "y" as ordinary tokens;
 *   - an unknown keyword is ignored at every depth.
 *
 * KAROO_THEME_STRUCT_DIAG=1 dumps a plausibility report of the loaded block
 * after each load (see docs/CONTROLS.md). */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "theme.h"
#include <stdlib.h>
#include "log.h"
#include "game.h"
#include "renderdevice.h"
#include "gamelog.h"
#include "gamestr.h"
#include "gameglobals.h"
#include "model.h"
#include "scenetexture.h"
#include "generators.h"
#include <math.h>
ThemeAssetBlock g_themeBlock;

/* The fixed limits: fgets' line length, and 16 token slots of 0x100. */
#define LINE_MAX     0x100
#define TOKEN_SLOTS  16
#define TOKEN_MAX    0x100

static char lower_one(char c)
{
    // The C-locale lowercase: strictly between '@' and '[' on a signed char.
    // Not tolower(), which depends on the locale and folds bytes >= 0x80.
    return (c > '@' && c < '[') ? (char)(c + ' ') : c;
}

static void lower_inplace(char *s)
{
    for (; *s; s++)
        *s = lower_one(*s);
}

/* KAROO_THEME_STRUCT_DIAG. */

static int struct_diag_enabled(void)
{
    char v[32];
    DWORD n = GetEnvironmentVariableA("KAROO_THEME_STRUCT_DIAG", v, sizeof(v));
    if (n == 0 || n >= sizeof(v))
        return 0;
    return v[0] != '0';
}

static int ptr_plausible(const void *p)
{
    return p == NULL || (ULONG_PTR)p >= 0x10000;
}

static int float_plausible(float f)
{
    return !(f != f) && f > -1.0e6f && f < 1.0e6f;  // f != f catches NaN
}

static void dump_record(const char *slotName, int i, const ThemeLevelObject &r)
{
    const char *kindNote = (r.kind <= THEME_KIND_PARTICLESYSTEM) ? "" : "  SUSPICIOUS kind";
    log_write("THEME_STRUCT:   %s[%d] kind=%lu%s pMesh=%p%s subobj=%lu%s\n",
              slotName, i, (unsigned long)r.kind, kindNote, (void *)r.pMesh,
              ptr_plausible(r.pMesh) ? "" : "  SUSPICIOUS pMesh",
              (unsigned long)r.dwSubObjectCount,
              r.dwSubObjectCount <= 8 ? "" : "  SUSPICIOUS dwSubObjectCount");
    log_write("THEME_STRUCT:   %s[%d] pos=(%g,%g,%g)%s scale=(%g,%g,%g)%s rot=(%g,%g,%g)%s\n",
              slotName, i, r.flPosX, r.flPosY, r.flPosZ,
              (float_plausible(r.flPosX) && float_plausible(r.flPosY) && float_plausible(r.flPosZ)) ? "" : "  SUSPICIOUS pos",
              r.flScaleX, r.flScaleY, r.flScaleZ,
              (float_plausible(r.flScaleX) && float_plausible(r.flScaleY) && float_plausible(r.flScaleZ)) ? "" : "  SUSPICIOUS scale",
              r.flRotRateX, r.flRotRateY, r.flRotRateZ,
              (float_plausible(r.flRotRateX) && float_plausible(r.flRotRateY) && float_plausible(r.flRotRateZ)) ? "" : "  SUSPICIOUS rot");
    log_write("THEME_STRUCT:   %s[%d] count=%lu movable=%lu lit=%lu nomove=%lu nozw=%lu noshadow=%lu spec=%lu randYaw=%lu\n",
              slotName, i, (unsigned long)r.dwInstanceCount, (unsigned long)r.dwMovableType,
              (unsigned long)r.bLit, (unsigned long)r.bNoMoveStates, (unsigned long)r.bNoZWrite,
              (unsigned long)r.bNoShadow, (unsigned long)r.bSpecular, (unsigned long)r.bRandomYAngle);
    log_write("THEME_STRUCT:   %s[%d] osc amp=%g freq=%g phase=%g random=%lu pump=(%g,%g,%g,%g)\n",
              slotName, i, r.flOscillationAmplitude, r.flOscillationFrequency,
              r.flOscillationPhase, (unsigned long)r.bOscillateRandom,
              r.flPump[0], r.flPump[1], r.flPump[2], r.flPump[3]);
    if (r.bExplode)
        log_write("THEME_STRUCT:   %s[%d] explode=%lu dir=(%g,%g,%g) verts=%d scaled=%g%s\n",
                  slotName, i, (unsigned long)r.bExplode,
                  r.flExplodeDir[0], r.flExplodeDir[1], r.flExplodeDir[2],
                  r.explode.vertexCount(), r.explode.explodeScaledCount(),
                  (r.pMesh != NULL && r.explode.vertexCount() > 0) ? "" : "  SUSPICIOUS explode");
    for (DWORD k = 0; k < r.dwSubObjectCount && k < 8; k++) {
        const SceneSubObject &so = r.pSubObjects[k];
        log_write("THEME_STRUCT:   %s[%d].sub[%lu] cond=%lu tex=%p blend=%lu/%lu addr=%lu effect=%lu (%g,%g,%g)%s\n",
                  slotName, i, (unsigned long)k, (unsigned long)so.dwVisibilityGate,
                  (void *)so.pTexture, (unsigned long)so.dwBlendSrc,
                  (unsigned long)so.dwBlendDst, (unsigned long)so.dwTexAddress,
                  (unsigned long)so.effect, so.flEffectParams[0],
                  so.flEffectParams[1], so.flEffectParams[2],
                  (so.dwVisibilityGate <= 6 && so.dwBlendSrc <= 13 && so.dwBlendDst <= 11 &&
                   so.dwTexAddress <= 4 && so.effect <= SUBOBJ_EFFECT_SCROLL &&
                   ptr_plausible(so.pTexture)) ? "" : "  SUSPICIOUS sub");
    }
}

static void dump_slot(const char *name, const ThemeObjectTypeSlot &slot)
{
    log_write("THEME_STRUCT: slot %-12s dwInstanceCount=%lu%s\n", name,
              (unsigned long)slot.dwInstanceCount,
              slot.dwInstanceCount <= 8 ? "" : "  SUSPICIOUS dwInstanceCount");
    unsigned shown = slot.dwInstanceCount <= 8 ? slot.dwInstanceCount : 8;
    for (unsigned i = 0; i < shown; i++)
        dump_record(name, i, slot.records[i]);
}

/* A plausibility report on the loaded block, not a proof: it catches a field
 * offset that is plainly wrong (a pointer that looks like a small integer, a
 * NaN float, a kind outside 0..4). */
static void theme_struct_dump(const char *path)
{
    const ThemeAssetBlock *block = &g_themeBlock;

    log_write("THEME_STRUCT: after close of %s\n", path);
    log_write("THEME_STRUCT: themeName=\"%.255s\" dwUnknown100=0x%08lx\n",
              block->themeName, (unsigned long)block->dwUnknown100);

    static const char *const kSlotNames[THEME_OBJ_COUNT] = {
        "john", "catcher", "catcherfx", "thrower", "throwerfx", "plate",
        "side", "platform", "paraglide", "paraglidefx", "elevator", "exit",
        "glue", "destructfield", "destructfieldfx", "jumppad", "slide",
        "stair", "teleporter", "crystal", "crystalfx", "ammunition", "bomb",
        "explosion", "surprise", "freeze", "speed", "speedfx", "collfx",
        "life", "switch", "time", "ice", "obstacle", "obstaclefx",
        "protection", "protectionfx", "bridge",
    };
    for (int s = 0; s < THEME_OBJ_COUNT; s++)
        dump_slot(kSlotNames[s], block->slots[s]);

    log_write("THEME_STRUCT: images[HUD]=%p%s images[MENU]=%p%s\n",
              (void *)block->images[THEME_IMG_HUD],
              ptr_plausible(block->images[THEME_IMG_HUD]) ? "" : "  SUSPICIOUS",
              (void *)block->images[THEME_IMG_MENU],
              ptr_plausible(block->images[THEME_IMG_MENU]) ? "" : "  SUSPICIOUS");
    log_write("THEME_STRUCT: textColors[HUD]=%08lx/%08lx textColors[MENUSUMMARYSAVE]=%08lx/%08lx\n",
              (unsigned long)block->textColors[THEME_COLOR_HUD].color1,
              (unsigned long)block->textColors[THEME_COLOR_HUD].color2,
              (unsigned long)block->textColors[THEME_COLOR_MENUSUMMARYSAVE].color1,
              (unsigned long)block->textColors[THEME_COLOR_MENUSUMMARYSAVE].color2);
    log_write("THEME_STRUCT: bFogEnabled=%u%s flSideHeight=%g%s\n",
              (unsigned)block->bFogEnabled, block->bFogEnabled <= 1 ? "" : "  SUSPICIOUS",
              block->flSideHeight, float_plausible(block->flSideHeight) ? "" : "  SUSPICIOUS");

    for (int f = 0; f < 6; f++) {
        const LoadedImage &img = block->sky.textures()[f];
        const char *name = img.imageName();
        int nameOk = name != NULL && (ULONG_PTR)name >= 0x10000;
        log_write("THEME_STRUCT: sky.faces[%d] surface=%p%s name=%p \"%.63s\"%s\n",
                  f, (void *)img.textureSurface(),
                  ptr_plausible(img.textureSurface()) ? "" : "  SUSPICIOUS",
                  (void *)name, nameOk ? name : "",
                  (name == NULL || nameOk) ? "" : "  SUSPICIOUS name");
    }
}

static void theme_struct_dump_if_enabled(const char *path)
{
    if (struct_diag_enabled())
        theme_struct_dump(path);
}

/* What each keyword writes.  rec is the record the last model, field,
 * billboard or particlesystem line opened; sub the sub-object the last depth-2
 * texture line opened.
 *
 * PRESERVED, beyond the tokenizer's:
 *   - the record and sub-object cursors are never bounds-checked: a ninth
 *     record runs into the next slot, a ninth texture into bLit;
 *   - oscillate's `random` flag and phase, and the depth-3 `environment`
 *     and `textureadress`, write through the record with no check for a
 *     missing slot, so inside `environment { }` they would fault near 0;
 *   - `particlesystem` loads the .par even when there is no slot to keep
 *     it (inside `environment`), and leaks it;
 *   - the model and texture caches lowercase the token buffers in place.
 * Tokens past the 16th are dropped, but still counted. */

/* The block's sub-objects are handed to their owners by address, and really
 * are misaligned: the block is packed.  One warning suppression for the
 * loader. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"

typedef void *(__attribute__((thiscall)) *theme_scalar_dtor_fn)(void *self, unsigned int flags);

static void delete_via_vtable(void *obj)
{
    theme_scalar_dtor_fn dtor = **(theme_scalar_dtor_fn **)obj;
    dtor(obj, 1);
}

/* Releases one type's records. */
extern "C" __declspec(dllexport) void __attribute__((fastcall))
Theme_ReleaseSlot(ThemeObjectTypeSlot *slot)
{
    for (int i = 0; i < 8; i++) {
        ThemeLevelObject &r = slot->records[i];
        r.explode.release();
        r.wrapper.releaseSnapshot();
        for (DWORD k = 0; k < r.dwInstanceCount; k++) {
            if (r.pParticleSystems[k] != NULL) {
                delete_via_vtable(r.pParticleSystems[k]);
                r.pParticleSystems[k] = NULL;
            }
        }
    }
    memset(slot, 0, sizeof(*slot));
}

/* Every slot is a member of the global block.  Records are constructed in
 * order and destroyed last to first, as MSVC's vector iterators do; none of
 * the constructors can throw.  PRESERVED: the slot destructor installs the
 * vtable, then the release zeroes the whole slot, so the record destructors
 * that follow run on zeroed members. */
static void *const g_ThemeSlotVtable[1] = { (void *)&Theme_SlotScalarDtor };

extern "C" __declspec(dllexport) ThemeLevelObject *__attribute__((thiscall))
Theme_RecordConstruct(ThemeLevelObject *self)
{
    self->wrapper.construct();
    self->explode.construct();
    return self;
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Theme_RecordDestruct(ThemeLevelObject *self)
{
    self->explode.dtorBody();
    self->wrapper.dtorBody();
}

extern "C" __declspec(dllexport) ThemeObjectTypeSlot *__attribute__((thiscall))
Theme_SlotConstruct(ThemeObjectTypeSlot *self)
{
    for (int i = 0; i < 8; i++)
        Theme_RecordConstruct(&self->records[i]);
    self->pVtable = (void *)g_ThemeSlotVtable;
    return self;
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Theme_SlotDestruct(ThemeObjectTypeSlot *self)
{
    self->pVtable = (void *)g_ThemeSlotVtable;
    Theme_ReleaseSlot(self);
    for (int i = 8; i-- > 0; )
        Theme_RecordDestruct(&self->records[i]);
}

extern "C" __declspec(dllexport) ThemeObjectTypeSlot *__attribute__((thiscall))
Theme_SlotScalarDtor(ThemeObjectTypeSlot *self, unsigned int flags)
{
    Theme_SlotDestruct(self);
    if (flags & 1)
        free(self);
    return self;
}

/* Members only: the 38 slots in order, then the sky; destruction in reverse.
 * The plain data between them is left alone. */
extern "C" __declspec(dllexport) ThemeAssetBlock *__attribute__((thiscall))
Theme_BlockConstruct(ThemeAssetBlock *self)
{
    for (int i = 0; i < THEME_OBJ_COUNT; i++)
        Theme_SlotConstruct(&self->slots[i]);
    self->sky.construct();
    return self;
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Theme_BlockDestruct(ThemeAssetBlock *self)
{
    self->sky.dtorBody();
    for (int i = THEME_OBJ_COUNT; i-- > 0; )
        Theme_SlotDestruct(&self->slots[i]);
}

/* PRESERVED: EXPLOSION's slot is not in the list, so its particle systems and
 * explode buffers are never released, though the memset still zeroes them. */
static const ThemeObjectType kReleaseOrder[] = {
    THEME_OBJ_JOHN, THEME_OBJ_CATCHER, THEME_OBJ_CATCHERFX, THEME_OBJ_THROWER,
    THEME_OBJ_THROWERFX, THEME_OBJ_BOMB, THEME_OBJ_DESTRUCTFIELD,
    THEME_OBJ_DESTRUCTFIELDFX, THEME_OBJ_EXIT, THEME_OBJ_ELEVATOR,
    THEME_OBJ_GLUE, THEME_OBJ_JUMPPAD, THEME_OBJ_PLATE, THEME_OBJ_SIDE,
    THEME_OBJ_SLIDE, THEME_OBJ_STAIR, THEME_OBJ_TELEPORTER,
    THEME_OBJ_AMMUNITION, THEME_OBJ_CRYSTAL, THEME_OBJ_CRYSTALFX,
    THEME_OBJ_FREEZE, THEME_OBJ_SPEED, THEME_OBJ_SPEEDFX, THEME_OBJ_COLLFX,
    THEME_OBJ_LIFE, THEME_OBJ_PLATFORM, THEME_OBJ_SURPRISE, THEME_OBJ_SWITCH,
    THEME_OBJ_TIME, THEME_OBJ_ICE, THEME_OBJ_OBSTACLE, THEME_OBJ_OBSTACLEFX,
    THEME_OBJ_PROTECTION, THEME_OBJ_PROTECTIONFX, THEME_OBJ_PARAGLIDE,
    THEME_OBJ_PARAGLIDEFX, THEME_OBJ_BRIDGE,
};
static_assert(sizeof(kReleaseOrder) / sizeof(kReleaseOrder[0]) == THEME_OBJ_COUNT - 1,
              "every slot but EXPLOSION");

extern "C" __declspec(dllexport) void __cdecl
Theme_ReleaseBlock(ThemeAssetBlock *block)
{
    g_textureManager.releaseAll();
    g_modelManager.clearReleaseFree();
    for (ThemeObjectType t : kReleaseOrder)
        Theme_ReleaseSlot(&block->slots[t]);
    for (int f = 0; f < 6; f++)
        block->sky.textures()[f].releaseD3DTexture();
    memset(block, 0, sizeof(*block));
}

/* "NONE" (case-exact, on the raw wave name) disables the entry without
 * logging.  PRESERVED: the path is formatted unbounded into 256 bytes. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
ThemeSound_Add(ThemeSoundTable *self, unsigned int id, const char *waveName,
               DWORD arg3, DWORD arg4)
{
    char path[256];
    sprintf(path, GS_THEME_SOUND_PATH, g_gameDir, waveName);

    SoundAssetName &e = self->entries[id & 0xffff];
    if (strcmp(waveName, GS_THEME_SOUND_NONE) == 0) {
        e.enabled = 0;
        return 0;
    }
    g_logger.logMessage(1, GS_THEME_SOUND_ADD, id & 0xffff, path);
    strcpy(e.name, path);
    e.unknown104 = arg4;
    e.unknown108 = arg3;
    e.enabled    = 1;
    return 0;
}

/* The event ids, in test order.  An unknown event registers nothing and
 * returns false. */
static const struct { const char *name; unsigned id; } kSoundEvents[] = {
    { "movejj", 1 },            { "movecatcher", 0 },     { "movethrower", 2 },
    { "moveicesliding", 3 },    { "movesliding", 4 },     { "moveparagliding", 5 },
    { "moveparaglidingstart", 6 }, { "movejumppad", 7 },  { "teleporter", 8 },
    { "elevator", 9 },          { "platform", 10 },       { "switch", 11 },
    { "glue", 12 },             { "bridge", 13 },         { "destructstart", 14 },
    { "destructregen", 15 },    { "obstacle", 16 },       { "crystal", 0x1e },
    { "splatjj", 0x28 },        { "splatcatcher", 0x29 }, { "splatthrower", 0x2a },
    { "falljj", 0x32 },         { "fallcatcher", 0x33 },  { "fallthrower", 0x34 },
    { "bombtick", 0x3c },       { "explosionbomb", 0x46 }, { "explosioncatcher", 0x47 },
};

extern "C" __declspec(dllexport) bool __cdecl
Theme_RegisterSound(Game *game, char *eventName, const char *waveName)
{
    lower_inplace(eventName);
    for (const auto &ev : kSoundEvents) {
        if (strcmp(eventName, ev.name) == 0) {
            ThemeSound_Add(game->themeSounds(), ev.id, waveName, 1, 1);
            return true;
        }
    }
    return false;
}

/* The parser's keyword tables. */

enum { FOG_NONE = 0, FOG_EXP = 1, FOG_EXP2 = 2, FOG_LINEAR = 3 };

struct KeywordValue { const char *name; DWORD value; };

/* srcblend's list; destblend's is the same without the last entry. */
static const KeywordValue kBlends[] = {
    { "zero", 1 }, { "one", 2 }, { "srccolor", 3 }, { "invsrccolor", 4 },
    { "srcalpha", 5 }, { "invsrcalpha", 6 }, { "destalpha", 7 },
    { "invdestalpha", 8 }, { "destcolor", 9 }, { "invdestcolor", 10 },
    { "srcalphasat", 11 }, { "bothinvsrcalpha", 13 },
};
static const KeywordValue kConditions[] = {
    { "active", 1 }, { "inactive", 2 }, { "dead", 3 }, { "alive", 4 },
    { "paraglide", 5 }, { "protection", 6 },
};
static const KeywordValue kTextureAddress[] = {
    { "wrap", 1 }, { "mirror", 2 }, { "clamp", 3 }, { "border", 4 },
};
static const KeywordValue kFogModes[] = {
    { "none", FOG_NONE }, { "exp", FOG_EXP }, { "exp2", FOG_EXP2 },
    { "linear", FOG_LINEAR },
};

static const struct { const char *name; ThemeObjectType type; } kObjectKeywords[] = {
    { "exit", THEME_OBJ_EXIT }, { "john", THEME_OBJ_JOHN },
    { "catcher", THEME_OBJ_CATCHER }, { "catcherfx", THEME_OBJ_CATCHERFX },
    { "thrower", THEME_OBJ_THROWER }, { "throwerfx", THEME_OBJ_THROWERFX },
    { "paraglide", THEME_OBJ_PARAGLIDE }, { "paraglidefx", THEME_OBJ_PARAGLIDEFX },
    { "bridge", THEME_OBJ_BRIDGE }, { "collfx", THEME_OBJ_COLLFX },
    { "protection", THEME_OBJ_PROTECTION }, { "protectionfx", THEME_OBJ_PROTECTIONFX },
    { "ammunition", THEME_OBJ_AMMUNITION }, { "bomb", THEME_OBJ_BOMB },
    { "explosion", THEME_OBJ_EXPLOSION }, { "crystal", THEME_OBJ_CRYSTAL },
    { "crystalfx", THEME_OBJ_CRYSTALFX }, { "speed", THEME_OBJ_SPEED },
    { "speedfx", THEME_OBJ_SPEEDFX }, { "freeze", THEME_OBJ_FREEZE },
    { "life", THEME_OBJ_LIFE }, { "time", THEME_OBJ_TIME },
    { "switch", THEME_OBJ_SWITCH }, { "surprise", THEME_OBJ_SURPRISE },
    { "elevator", THEME_OBJ_ELEVATOR }, { "platform", THEME_OBJ_PLATFORM },
    { "plate", THEME_OBJ_PLATE }, { "side", THEME_OBJ_SIDE },
    { "glue", THEME_OBJ_GLUE }, { "destructfield", THEME_OBJ_DESTRUCTFIELD },
    { "destructfieldfx", THEME_OBJ_DESTRUCTFIELDFX }, { "jumppad", THEME_OBJ_JUMPPAD },
    { "teleporter", THEME_OBJ_TELEPORTER }, { "slide", THEME_OBJ_SLIDE },
    { "ice", THEME_OBJ_ICE }, { "obstacle", THEME_OBJ_OBSTACLE },
    { "obstaclefx", THEME_OBJ_OBSTACLEFX }, { "stair", THEME_OBJ_STAIR },
};

static const struct { const char *name; ThemeImageSlot slot; } kImageKeywords[] = {
    { "hud", THEME_IMG_HUD }, { "menu", THEME_IMG_MENU }, { "edge", THEME_IMG_EDGE },
    { "pointer", THEME_IMG_POINTER }, { "radar", THEME_IMG_RADAR },
    { "freeze", THEME_IMG_FREEZE }, { "inversecontrol", THEME_IMG_INVERSECONTROL },
    { "protection", THEME_IMG_PROTECTION }, { "slowdwon", THEME_IMG_SLOWDOWN },
    { "speed", THEME_IMG_SPEED },
};

/* In ThemeTextColorSlot order. */
static const char *const kTextColorKeywords[THEME_COLOR_COUNT] = {
    "hudtextcolors",
    "menunewgametextcolors", "menuloadgametextcolors",
    "menuhighscorestextcolors", "menuoptionstextcolors",
    "menucreditstextcolors", "menuquittextcolors",
    "menuloadgameentriestextcolors", "menusavegameentriestextcolors",
    "menuhighscoresentriestextcolors",
    "menuoptionscontroltextcolors", "menuoptionsvideotextcolors",
    "menuoptionsaudiotextcolors",
    "menucontrolentriestextcolors", "menucontrolcameratextcolors",
    "menuvideoreflectiontextcolors", "menuvideoshadowtextcolors",
    "menuvideohighlighttextcolors", "menuvideoparticletextcolors",
    "menuaudio3dsoundtextcolors", "menuaudiosoundvoltextcolors",
    "menuaudiocdmusictextcolors", "menuaudiocdvoltextcolors",
    "menusummaryentriestextcolors", "menusummarynexttextcolors",
    "menusummarysavetextcolors",
};

/* Looks tok up, lowercasing it in place first; *out is untouched on a miss. */
static bool lookup(char *tok, const KeywordValue *tab, int n, DWORD *out)
{
    lower_inplace(tok);
    for (int i = 0; i < n; i++) {
        if (strcmp(tok, tab[i].name) == 0) {
            *out = tab[i].value;
            return true;
        }
    }
    return false;
}

static bool is(char *tok, const char *kw)
{
    lower_inplace(tok);
    return strcmp(tok, kw) == 0;
}

static float atof_f(const char *s) { return (float)atof(s); }

static DWORD float_bits(float f)
{
    DWORD d;
    memcpy(&d, &f, sizeof(d));
    return d;
}

/* The parser follows the file's nesting: each `{` calls the next level's block
 * function, which reads lines until its own `}`, applies that brace's effects
 * and returns, so the call stack is the depth.  At end of file every level
 * returns false without its closing effects: a block cut off by the end of the
 * file never stores a count.
 *
 *   parseFile            depth 0   pick the object type, or `environment`
 *   parseObjectBlock     depth 1   records and block-level settings
 *   parseRecordBlock     depth 2   one record's placement and look
 *   parseSubObjectBlock  depth 3   one sub-object's render state
 *
 * A `{` at depth 3 and a `}` at depth 0 are ordinary unknown tokens.
 *
 * A cursor is the record (or sub-object) the last opening line opened; count
 * is how many have been opened, which the closing brace stores.  Each starts
 * one before the first.  PRESERVED: neither is bounds- or NULL-checked; with
 * no slot they point near address 0. */
template <typename T> struct Cursor {
    T   *at;
    int  count;
};
struct ThemeParser {
    Game            *game;
    RenderDevice    *d3d;
    ThemeAssetBlock *block;
    GameLogger      *logger;
    FILE            *fp;

    char     tok[TOKEN_SLOTS][TOKEN_MAX];
    unsigned ntok;

    bool nextLine();

    void parseFile();
    bool parseObjectBlock(ThemeObjectTypeSlot *slot, bool inEnvironment);
    bool parseRecordBlock(ThemeObjectTypeSlot *slot, ThemeLevelObject *rec);
    bool parseSubObjectBlock(ThemeObjectTypeSlot *slot, SceneSubObject *sub);

    void selectTarget(ThemeObjectTypeSlot *&slot, bool &inEnvironment);
    void objectKeyword(ThemeObjectTypeSlot *slot, bool inEnvironment, Cursor<ThemeLevelObject> &rec);
    void recordKeyword(ThemeObjectTypeSlot *slot, ThemeLevelObject *rec, Cursor<SceneSubObject> &sub);
    void subObjectKeyword(ThemeObjectTypeSlot *slot, SceneSubObject *sub);

    static void open(Cursor<ThemeLevelObject> &c, ThemeObjectTypeSlot *slot)
    {
        c.at = recordAt(slot, c.count++);
    }
    static void open(Cursor<SceneSubObject> &c, ThemeLevelObject *rec)
    {
        c.at = subObjectAt(rec, c.count++);
    }

    static ThemeLevelObject *recordAt(ThemeObjectTypeSlot *s, int i)
    {
        return reinterpret_cast<ThemeLevelObject *>(
            reinterpret_cast<BYTE *>(s) + offsetof(ThemeObjectTypeSlot, records)
            + i * (int)sizeof(ThemeLevelObject));
    }
    static SceneSubObject *subObjectAt(ThemeLevelObject *r, int i)
    {
        return reinterpret_cast<SceneSubObject *>(
            reinterpret_cast<BYTE *>(r) + offsetof(ThemeLevelObject, pSubObjects)
            + i * (int)sizeof(SceneSubObject));
    }

    SceneTexture *loadTexture(char *name, char *alphaTok)
    {
        DWORD alpha = is(alphaTok, "alpha") ? 1 : 0;
        return g_textureManager.getOrLoad(d3d,
                                        name, alpha, 0, 0);
    }

    void fog(bool inEnvironment);
    void sky(bool inEnvironment);
    void particleSystem(ThemeObjectTypeSlot *slot, Cursor<ThemeLevelObject> &rec);
    void explode(ThemeObjectTypeSlot *slot, ThemeLevelObject *rec);
};

/* The next line that is not blank or a comment, tokenized; false at the end.
 * A whitespace-only last line with no newline is not skipped: it yields no
 * tokens and an empty tok[0]. */
bool ThemeParser::nextLine()
{
    char buf[LINE_MAX];
    while (!feof(fp)) {
        if (fgets(buf, LINE_MAX, fp) == NULL)
            continue;
        memset(tok, 0, sizeof(tok));
        ntok = 0;

        char *s = buf;
        while (*s == ' ' || *s == '\t')
            s++;
        if ((s[0] == '/' && s[1] == '/') || s[0] == '\n')
            continue;
        for (char *t = strtok(s, " \t\n"); t != NULL; t = strtok(NULL, " \t\n")) {
            if (ntok < TOKEN_SLOTS)
                strcpy(tok[ntok], t);
            ntok++;
        }
        return true;
    }
    return false;
}

/* Depth 0. */

void ThemeParser::parseFile()
{
    ThemeObjectTypeSlot *slot = NULL;  // the object block being filled; NULL for none
    bool inEnvironment = false;        // gates the block-level keywords
    while (nextLine()) {
        if (is(tok[0], "{")) {
            if (!parseObjectBlock(slot, inEnvironment))
                return;
            // The depth-1 `}`: back at the top, nothing selected.
            slot = NULL;
            inEnvironment = false;
        } else {
            selectTarget(slot, inEnvironment);
        }
    }
}

void ThemeParser::selectTarget(ThemeObjectTypeSlot *&slot, bool &inEnvironment)
{
    for (const auto &k : kObjectKeywords) {
        if (is(tok[0], k.name)) {
            slot = &block->slots[k.type];
            return;
        }
    }
    if (is(tok[0], "environment")) {
        inEnvironment = true;
        slot = NULL;
    } else {
        slot = NULL;
    }
}

/* Depth 1. */

bool ThemeParser::parseObjectBlock(ThemeObjectTypeSlot *slot, bool inEnvironment)
{
    Cursor<ThemeLevelObject> rec = { recordAt(slot, -1), 0 };
    while (nextLine()) {
        if (is(tok[0], "{")) {
            // Applies to the current record: records[-1] if none is open.
            if (slot) {
                rec.at->flScaleX = 1.0f;
                rec.at->flScaleY = 1.0f;
                rec.at->flScaleZ = 1.0f;
            }
            if (!parseRecordBlock(slot, rec.at))
                return false;
        } else if (is(tok[0], "}")) {
            if (slot)
                slot->dwInstanceCount = rec.count;
            return true;
        } else {
            objectKeyword(slot, inEnvironment, rec);
        }
    }
    return false;
}

void ThemeParser::particleSystem(ThemeObjectTypeSlot *slot, Cursor<ThemeLevelObject> &c)
{
    if (ntok <= 1)
        return;
    open(c, slot);
    ThemeLevelObject *rec = c.at;
    ParticleSystem *ps = Particle_LoadFromFile(tok[1], logger);
    if (ps == NULL) {
        if (slot) rec->kind = THEME_KIND_NONE;
        return;
    }
    if (slot) {
        rec->kind = THEME_KIND_PARTICLESYSTEM;
        rec->pParticleSystems[0] = ps;
    }
    if (is(tok[2], "movable1") && slot) rec->dwMovableType = 1;
    if (is(tok[2], "movable2") && slot) rec->dwMovableType = 2;

    unsigned count = (unsigned)atoi(tok[3]);
    if (count < 2 || count > 16) {
        if (slot) rec->dwInstanceCount = 1;
    } else {
        for (unsigned i = 1; i < count; i++)
            if (slot) rec->pParticleSystems[i] = Particle_CloneSystem(ps);
        if (slot) rec->dwInstanceCount = count;
    }
}

void ThemeParser::fog(bool inEnvironment)
{
    if (!inEnvironment || ntok <= 3)
        return;
    RenderDevice *dev = d3d;
    dev->SetRenderState(RS::FogEnable, 1);
    block->bFogEnabled = 1;

    DWORD mode = FOG_NONE;
    lookup(tok[1], kFogModes, 4, &mode);
    dev->SetRenderState(RS::FogTableMode, mode);

    char *colourTok;
    if (mode == FOG_LINEAR) {
        dev->SetRenderState(RS::FogTableStart, float_bits(atof_f(tok[2])));
        dev->SetRenderState(RS::FogTableEnd,   float_bits(atof_f(tok[3])));
        colourTok = tok[4];
    } else {
        dev->SetRenderState(RS::FogTableDensity, float_bits(atof_f(tok[2])));
        colourTok = tok[3];
    }
    char *end;
    dev->SetRenderState(RS::FogColor, (DWORD)strtol(colourTok, &end, 16));
}

void ThemeParser::sky(bool inEnvironment)
{
    if (!inEnvironment || ntok <= 1)
        return;
    // PRESERVED: 0x100 each and unbounded.
    char up[0x100], dn[0x100], fr[0x100], bk[0x100], lf[0x100], rt[0x100];
    sprintf(up, GS_THEME_SKY_UP, tok[1]);
    sprintf(dn, GS_THEME_SKY_DN, tok[1]);
    sprintf(fr, GS_THEME_SKY_FR, tok[1]);
    sprintf(bk, GS_THEME_SKY_BK, tok[1]);
    sprintf(lf, GS_THEME_SKY_LF, tok[1]);
    sprintf(rt, GS_THEME_SKY_RT, tok[1]);
    unsigned int ok = block->sky.buildFromFaceNames(d3d,
                                             up, dn, fr, bk, lf, rt,
                                             d3d->bitDepth());
    if (logger != NULL) {
        if ((ok & 0xff) == 0)
            logger->logMessage(3, GS_THEME_SKY_FAILED, tok[1]);
        else
            logger->logMessage(1, GS_THEME_SKY_LOADED, tok[1]);
    }
}

void ThemeParser::objectKeyword(ThemeObjectTypeSlot *slot, bool inEnvironment,
                                Cursor<ThemeLevelObject> &rec)
{
    if (is(tok[0], "model")) {
        if (slot == NULL || ntok <= 1)
            return;
        open(rec, slot);
        CFaktMesh *mesh = g_modelManager.findOrImport(tok[1]);
        if (mesh == NULL) {
            rec.at->kind = THEME_KIND_NONE;
            return;
        }
        rec.at->kind  = THEME_KIND_MODEL;
        rec.at->pMesh = mesh;
        rec.at->wrapper.setMesh(mesh);
        Ani_LoadAnimationFile(&rec.at->animTable, tok[2], logger);
        if (is(tok[3], "nomovestates"))
            rec.at->bNoMoveStates = 1;
        return;
    }
    if (is(tok[0], "field")) {
        open(rec, slot);
        if (slot) rec.at->kind = THEME_KIND_FIELD;
        return;
    }
    if (is(tok[0], "billboard")) {
        if (ntok <= 1)
            return;
        open(rec, slot);
        if (slot) {
            rec.at->kind = THEME_KIND_BILLBOARD;
            rec.at->flBillboardScale = atof_f(tok[1]);
        }
        return;
    }
    if (is(tok[0], "particlesystem")) {
        particleSystem(slot, rec);
        return;
    }
    for (const auto &k : kImageKeywords) {
        if (is(tok[0], k.name)) {
            if (inEnvironment && ntok > 1)
                block->images[k.slot] = loadTexture(tok[1], tok[2]);
            return;
        }
    }
    for (int c = 0; c < THEME_COLOR_COUNT; c++) {
        if (is(tok[0], kTextColorKeywords[c])) {
            if (inEnvironment && ntok > 2) {
                char *end;
                block->textColors[c].color1 = (DWORD)strtol(tok[1], &end, 16);
                block->textColors[c].color2 = (DWORD)strtol(tok[2], &end, 16);
            }
            return;
        }
    }
    if (is(tok[0], "fog")) {
        fog(inEnvironment);
    } else if (is(tok[0], "sky")) {
        sky(inEnvironment);
    } else if (is(tok[0], "sideheight")) {
        if (inEnvironment && ntok > 1)
            block->flSideHeight = atof_f(tok[1]);
    } else if (is(tok[0], "sound")) {
        if (inEnvironment && ntok > 2)
            Theme_RegisterSound(game, tok[1], tok[2]);
    }
}

/* Depth 2. */

bool ThemeParser::parseRecordBlock(ThemeObjectTypeSlot *slot, ThemeLevelObject *rec)
{
    Cursor<SceneSubObject> sub = { subObjectAt(rec, -1), 0 };
    while (nextLine()) {
        if (is(tok[0], "{")) {
            if (!parseSubObjectBlock(slot, sub.at))
                return false;
        } else if (is(tok[0], "}")) {
            if (slot)
                rec->dwSubObjectCount = sub.count;
            return true;
        } else {
            recordKeyword(slot, rec, sub);
        }
    }
    return false;
}

/* The explode direction: (t4, t5, t6, 1) times a rotation about X by the float
 * -pi/2, as a row vector, divided by w (always 1).  Double precision with
 * cos() and sin(); the game's x87 chain differs only in the last bits, which
 * nothing observes. */
void ThemeParser::explode(ThemeObjectTypeSlot *slot, ThemeLevelObject *rec)
{
    if (ntok <= 6 || slot == NULL || rec->pMesh == NULL)
        return;
    rec->bExplode = 1;
    rec->explode.allocateExplodeBuffers(rec->pMesh);
    Gen_FillGaussianField(&rec->explode, atof_f(tok[1]), atof_f(tok[2]));
    rec->explode.storeExplodeScaledCount(atof_f(tok[3]));

    const double a = (double)-1.5707963705062866f;
    const double c = cos(a), s = sin(a);
    const float x = atof_f(tok[4]), y = atof_f(tok[5]), z = atof_f(tok[6]);
    float out[3] = { x, (float)(c * y + s * z), (float)(-s * y + c * z) };
    const float w = 1.0f;
    if (w != 1.0f)
        for (float &v : out) v /= w;
    rec->flExplodeDir[0] = out[0];
    rec->flExplodeDir[1] = out[1];
    rec->flExplodeDir[2] = out[2];
}

void ThemeParser::recordKeyword(ThemeObjectTypeSlot *slot, ThemeLevelObject *rec,
                                Cursor<SceneSubObject> &sub)
{
    if (is(tok[0], "texture")) {
        if (ntok <= 1)
            return;
        open(sub, rec);
        DWORD alpha = is(tok[2], "alpha") ? 1 : 0;
        if (slot)
            sub.at->pTexture = g_textureManager.getOrLoad(d3d, tok[1], alpha, 0, 0);
    } else if (is(tok[0], "position")) {
        if (ntok > 3 && slot) {
            rec->flPosX = atof_f(tok[1]); rec->flPosY = atof_f(tok[2]); rec->flPosZ = atof_f(tok[3]);
        }
    } else if (is(tok[0], "scale")) {
        if (ntok > 3 && slot) {
            rec->flScaleX = atof_f(tok[1]); rec->flScaleY = atof_f(tok[2]); rec->flScaleZ = atof_f(tok[3]);
        }
    } else if (is(tok[0], "rotate")) {
        if (ntok > 3 && slot) {
            rec->flRotRateX = atof_f(tok[1]); rec->flRotRateY = atof_f(tok[2]); rec->flRotRateZ = atof_f(tok[3]);
        }
    } else if (is(tok[0], "randomyangle")) {
        if (slot) rec->bRandomYAngle = 1;
    } else if (is(tok[0], "nozwrite")) {
        if (slot) rec->bNoZWrite = 1;
    } else if (is(tok[0], "noshadow")) {
        if (slot) rec->bNoShadow = 1;
    } else if (is(tok[0], "oscillate")) {
        if (ntok <= 2)
            return;
        if (slot) {
            rec->flOscillationAmplitude = atof_f(tok[1]);
            rec->flOscillationFrequency = atof_f(tok[2]);
        }
        // PRESERVED: no slot check from here on.
        if (is(tok[3], "random"))
            rec->bOscillateRandom = 1;
        rec->flOscillationPhase = (ntok < 5) ? 0.0f : atof_f(tok[4]);
    } else if (is(tok[0], "pump")) {
        if (ntok > 4 && slot)
            for (int i = 0; i < 4; i++)
                rec->flPump[i] = atof_f(tok[1 + i]);
    } else if (is(tok[0], "lit")) {
        if (slot) rec->bLit = 1;
    } else if (is(tok[0], "specular")) {
        if (slot) rec->bSpecular = 1;
    } else if (is(tok[0], "explode")) {
        explode(slot, rec);
    }
}

/* Depth 3. */

bool ThemeParser::parseSubObjectBlock(ThemeObjectTypeSlot *slot, SceneSubObject *sub)
{
    while (nextLine()) {
        if (is(tok[0], "}"))
            return true;
        subObjectKeyword(slot, sub);
    }
    return false;
}

void ThemeParser::subObjectKeyword(ThemeObjectTypeSlot *slot, SceneSubObject *sub)
{
    DWORD v;
    if (is(tok[0], "srcblend")) {
        if (ntok > 1 && lookup(tok[1], kBlends, 12, &v) && slot)
            sub->dwBlendSrc = v;
    } else if (is(tok[0], "destblend")) {
        if (ntok > 1 && lookup(tok[1], kBlends, 11, &v) && slot)
            sub->dwBlendDst = v;
    } else if (is(tok[0], "condition")) {
        if (ntok > 1 && lookup(tok[1], kConditions, 6, &v) && slot)
            sub->dwVisibilityGate = v;
    } else if (is(tok[0], "flash")) {
        if (ntok > 3 && slot) {
            sub->effect = SUBOBJ_EFFECT_FLASH;
            for (int i = 0; i < 3; i++) sub->flEffectParams[i] = atof_f(tok[1 + i]);
        }
    } else if (is(tok[0], "pulse")) {
        if (ntok > 1 && slot) {
            sub->effect = SUBOBJ_EFFECT_PULSE;
            for (int i = 0; i < 2; i++) sub->flEffectParams[i] = atof_f(tok[1 + i]);
        }
    } else if (is(tok[0], "wobble")) {
        if (ntok > 3 && slot) {
            sub->effect = SUBOBJ_EFFECT_WOBBLE;
            for (int i = 0; i < 3; i++) sub->flEffectParams[i] = atof_f(tok[1 + i]);
        }
    } else if (is(tok[0], "turn")) {
        if (ntok > 1 && slot) {
            sub->effect = SUBOBJ_EFFECT_TURN;
            sub->flEffectParams[0] = atof_f(tok[1]);
        }
    } else if (is(tok[0], "environment")) {
        sub->effect = SUBOBJ_EFFECT_ENVIRONMENT;  // PRESERVED: no slot check
    } else if (is(tok[0], "scroll")) {
        if (ntok > 2 && slot) {
            sub->effect = SUBOBJ_EFFECT_SCROLL;
            for (int i = 0; i < 2; i++) sub->flEffectParams[i] = atof_f(tok[1 + i]);
        }
    } else if (is(tok[0], "textureadress")) {
        if (ntok > 1 && lookup(tok[1], kTextureAddress, 4, &v))
            sub->dwTexAddress = v;  // PRESERVED: no slot check
    }
}

/* One parser for every call; 4 KB of tokens, so not on the stack. */
static ThemeParser s_parser;

static bool theme_load(Game *game, RenderDevice *d3d, ThemeAssetBlock *block,
                       char *path, GameLogger *logger)
{
    Theme_ReleaseBlock(block);
    d3d->SetRenderState(RS::FogEnable, 0);

    FILE *fp = fopen(path, "r");  // text mode: the CRT folds CRLF
    if (fp == NULL)
        return false;

    ThemeParser &p = s_parser;
    p = ThemeParser();
    p.game = game; p.d3d = d3d; p.block = block; p.logger = logger; p.fp = fp;
    p.parseFile();

    theme_struct_dump_if_enabled(path);
    fclose(fp);
    strcpy(block->themeName, path);  // PRESERVED: unbounded
    return true;
}

/* The load, timed.  The time goes only to our log, never into game state. */
extern "C" __declspec(dllexport) bool __cdecl
Theme_Load(Game *game, RenderDevice *d3d, ThemeAssetBlock *block, char *path,
           GameLogger *logger)
{
    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    bool ok = theme_load(game, d3d, block, path, logger);

    QueryPerformanceCounter(&t1);
    double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
    log_write("theme: %s %s in %.2f ms\n", path, ok ? "loaded" : "NOT opened", ms);
    return ok;
}

#pragma GCC diagnostic pop

/* The theme sound table's lifecycle.  ReleaseAll clears the name and the
 * enabled flag of all 100 entries between two log lines, leaving the other
 * fields, and returns 0. */
static void *const g_ThemeSoundVtable[1] = { (void *)&ThemeSound_ScalarDestructor };

extern "C" __declspec(dllexport) int __attribute__((thiscall))
ThemeSound_ReleaseAll(ThemeSoundTable *self)
{
    g_logger.logMessage(1, GS_THEME_SOUND_RELEASING);
    for (int i = 0; i < THEME_SOUND_COUNT; i++) {
        self->entries[i].enabled = 0;
        self->entries[i].name[0] = 0;
    }
    g_logger.logMessage(1, GS_THEME_SOUND_RELEASED);
    return 0;
}

extern "C" __declspec(dllexport) ThemeSoundTable *__attribute__((thiscall))
ThemeSound_Construct(ThemeSoundTable *self)
{
    self->vtable    = (void *)g_ThemeSoundVtable;
    self->unknown8  = 0;
    ThemeSound_ReleaseAll(self);
    return self;
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
ThemeSound_Destruct(ThemeSoundTable *self)
{
    self->vtable = (void *)g_ThemeSoundVtable;
}

/* Reached only through the vtable; the table is embedded in the Game. */
extern "C" __declspec(dllexport) ThemeSoundTable *__attribute__((thiscall))
ThemeSound_ScalarDestructor(ThemeSoundTable *self, unsigned char flags)
{
    ThemeSound_Destruct(self);
    if (flags & 1)
        free(self);
    return self;
}
