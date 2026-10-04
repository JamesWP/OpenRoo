/* The theme file loader.
 *
 * FORMAT: a .thm is text, read a line at a time in text mode (so the '\r' of
 * CRLF is folded away).  Leading whitespace is skipped, "//" lines and blank
 * lines dropped, and each line split into up to 16 tokens.
 *
 * PRESERVED in the tokenizer:
 *   - the whitespace skip comes before the comment test, so "   // x" is a
 *     comment but "Model x // y" yields "//" and "y" as ordinary tokens;
 *   - an unknown keyword is ignored at every depth.
 *
 * KAROO_THEME_STRUCT_DIAG=1 dumps a plausibility report of the loaded block
 * after each load (see docs/CONTROLS.md). */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "theme.h"
#include "sysdev.h"
#include "logger.h"
#include "game.h"
#include "renderdevice.h"
#include "gamestr.h"
#include "gameglobals.h"
#include "model.h"
#include "texture.h"
#include "generators.h"
#include <math.h>
#include <algorithm>
#include <iterator>
#include <fstream>
#include <sstream>
#include <string>
ThemeAssetBlock g_themeBlock;

/* The fixed limits: 16 token slots of 0x100. */
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
    uint32_t n = sysdev::getEnv("KAROO_THEME_STRUCT_DIAG", v, sizeof(v));
    if (n == 0 || n >= sizeof(v))
        return 0;
    return v[0] != '0';
}

static int ptr_plausible(const void *p)
{
    return p == NULL || (uintptr_t)p >= 0x10000;
}

static int float_plausible(float f)
{
    return !(f != f) && f > -1.0e6f && f < 1.0e6f;  // f != f catches NaN
}

static void dump_record(const char *slotName, int i, const ThemeLevelObject &r)
{
    const char *kindNote = (r.kind() <= THEME_KIND_PARTICLESYSTEM) ? "" : "  SUSPICIOUS kind";
    g_logger.write("THEME_STRUCT:   %s[%d] kind=%lu%s pMesh=%p%s subobj=%lu%s\n",
              slotName, i, (unsigned long)r.kind(), kindNote, (void *)r.mesh(),
              ptr_plausible(r.mesh()) ? "" : "  SUSPICIOUS pMesh",
              (unsigned long)r.subObjectCount(),
              r.subObjectCount() <= 8 ? "" : "  SUSPICIOUS dwSubObjectCount");
    g_logger.write("THEME_STRUCT:   %s[%d] pos=(%g,%g,%g)%s scale=(%g,%g,%g)%s rot=(%g,%g,%g)%s\n",
              slotName, i, r.posX(), r.posY(), r.posZ(),
              (float_plausible(r.posX()) && float_plausible(r.posY()) && float_plausible(r.posZ())) ? "" : "  SUSPICIOUS pos",
              r.scaleX(), r.scaleY(), r.scaleZ(),
              (float_plausible(r.scaleX()) && float_plausible(r.scaleY()) && float_plausible(r.scaleZ())) ? "" : "  SUSPICIOUS scale",
              r.rotRateX(), r.rotRateY(), r.rotRateZ(),
              (float_plausible(r.rotRateX()) && float_plausible(r.rotRateY()) && float_plausible(r.rotRateZ())) ? "" : "  SUSPICIOUS rot");
    g_logger.write("THEME_STRUCT:   %s[%d] count=%lu movable=%lu lit=%lu nomove=%lu nozw=%lu noshadow=%lu spec=%lu randYaw=%lu\n",
              slotName, i, (unsigned long)r.instanceCount(), (unsigned long)r.movableType(),
              (unsigned long)r.lit(), (unsigned long)r.noMoveStates(), (unsigned long)r.noZWrite(),
              (unsigned long)r.noShadow(), (unsigned long)r.specular(), (unsigned long)r.randomYAngle());
    g_logger.write("THEME_STRUCT:   %s[%d] osc amp=%g freq=%g phase=%g random=%lu pump=(%g,%g,%g,%g)\n",
              slotName, i, r.oscillationAmplitude(), r.oscillationFrequency(),
              r.oscillationPhase(), (unsigned long)r.oscillateRandom(),
              r.pump()[0], r.pump()[1], r.pump()[2], r.pump()[3]);
    if (r.explodes())
        g_logger.write("THEME_STRUCT:   %s[%d] explode=%lu dir=(%g,%g,%g) verts=%d scaled=%g%s\n",
                  slotName, i, (unsigned long)r.explodes(),
                  r.explodeDir()[0], r.explodeDir()[1], r.explodeDir()[2],
                  r.explodeDebris().vertexCount(), r.explodeDebris().explodeScaledCount(),
                  (r.mesh() != NULL && r.explodeDebris().vertexCount() > 0) ? "" : "  SUSPICIOUS explode");
    for (uint32_t k = 0; k < r.subObjectCount() && k < 8; k++) {
        const SceneSubObject &so = r.subObjects()[k];
        g_logger.write("THEME_STRUCT:   %s[%d].sub[%lu] cond=%lu tex=%p blend=%lu/%lu addr=%lu effect=%lu (%g,%g,%g)%s\n",
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
    g_logger.write("THEME_STRUCT: slot %-12s dwInstanceCount=%lu%s\n", name,
              (unsigned long)slot.instanceCount(),
              slot.instanceCount() <= 8 ? "" : "  SUSPICIOUS dwInstanceCount");
    unsigned shown = slot.instanceCount() <= 8 ? slot.instanceCount() : 8;
    for (unsigned i = 0; i < shown; i++)
        dump_record(name, i, slot.records()[i]);
}

/* A plausibility report on the loaded block, not a proof: it catches a field
 * offset that is plainly wrong (a pointer that looks like a small integer, a
 * NaN float, a kind outside 0..4). */
static void theme_struct_dump(const char *path)
{
    const ThemeAssetBlock *block = &g_themeBlock;

    g_logger.write("THEME_STRUCT: after close of %s\n", path);
    g_logger.write("THEME_STRUCT: themeName=\"%.255s\" dwUnknown100=0x%08lx\n",
              block->themeName(), (unsigned long)block->unknown100());

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
        dump_slot(kSlotNames[s], *block->slot(s));

    g_logger.write("THEME_STRUCT: images[HUD]=%p%s images[MENU]=%p%s\n",
              (void *)block->image(THEME_IMG_HUD),
              ptr_plausible(block->image(THEME_IMG_HUD)) ? "" : "  SUSPICIOUS",
              (void *)block->image(THEME_IMG_MENU),
              ptr_plausible(block->image(THEME_IMG_MENU)) ? "" : "  SUSPICIOUS");
    g_logger.write("THEME_STRUCT: textColors[HUD]=%08lx/%08lx textColors[MENUSUMMARYSAVE]=%08lx/%08lx\n",
              (unsigned long)block->textColor(THEME_COLOR_HUD).color1,
              (unsigned long)block->textColor(THEME_COLOR_HUD).color2,
              (unsigned long)block->textColor(THEME_COLOR_MENUSUMMARYSAVE).color1,
              (unsigned long)block->textColor(THEME_COLOR_MENUSUMMARYSAVE).color2);
    g_logger.write("THEME_STRUCT: bFogEnabled=%u%s flSideHeight=%g%s\n",
              (unsigned)block->fogEnabled(), block->fogEnabled() <= 1 ? "" : "  SUSPICIOUS",
              block->sideHeight(), float_plausible(block->sideHeight()) ? "" : "  SUSPICIOUS");

    for (int f = 0; f < 6; f++) {
        const Texture &img = block->sky().textures()[f];
        const char *name = img.name();
        int nameOk = name != NULL && (uintptr_t)name >= 0x10000;
        g_logger.write("THEME_STRUCT: sky.faces[%d] surface=%p%s name=%p \"%.63s\"%s\n",
                  f, (void *)img.handle(),
                  ptr_plausible(img.handle()) ? "" : "  SUSPICIOUS",
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

/* Releases one type's records. */
void ThemeObjectTypeSlot::release()
{
    for (int i = 0; i < 8; i++) {
        ThemeLevelObject &r = records_[i];
        r.explode_.release();
        r.wrapper_.releaseSnapshot();
        for (uint32_t k = 0; k < r.dwInstanceCount_; k++) {
            if (r.pParticleSystems_[k] != NULL) {
                delete r.pParticleSystems_[k];
                r.pParticleSystems_[k] = NULL;
            }
        }
    }
    memset((void *)this, 0, sizeof(*this));
}

/* Every slot is a member of the global block.  PRESERVED: the destructor's
 * release zeroes the whole slot, so the record destructors that follow run on
 * zeroed members. */
ThemeObjectTypeSlot::ThemeObjectTypeSlot()
{
}

ThemeObjectTypeSlot::~ThemeObjectTypeSlot()
{
    release();
}

ThemeAssetBlock::ThemeAssetBlock()
{
}

ThemeAssetBlock::~ThemeAssetBlock()
{
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

void ThemeAssetBlock::release()
{
    g_textureManager.releaseAll();
    g_modelManager.clearReleaseFree();
    for (ThemeObjectType t : kReleaseOrder)
        slots_[t].release();
    for (int f = 0; f < 6; f++)
        sky_.textures()[f].release();
    memset((void *)this, 0, sizeof(*this));
}

/* "NONE" (case-exact, on the raw wave name) disables the entry without
 * logging.  PRESERVED: the path is formatted unbounded into 256 bytes. */
int ThemeSoundTable::add(unsigned int id, const char *waveName,
               uint32_t arg3, uint32_t arg4)
{
    char path[256];
    sprintf(path, GS_THEME_SOUND_PATH, g_gameDir, waveName);

    SoundAssetName &e = entries_[id & 0xffff];
    if (strcmp(waveName, GS_THEME_SOUND_NONE) == 0) {
        e.enabled = 0;
        return 0;
    }
    g_logger.logMessage(1, "TSM: add called (Index=%d/fn=%s)", id & 0xffff, path);
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

  bool  
Theme_RegisterSound(Game *game, char *eventName, const char *waveName)
{
    lower_inplace(eventName);
    for (const auto &ev : kSoundEvents) {
        if (strcmp(eventName, ev.name) == 0) {
            game->themeSounds()->add(ev.id, waveName, 1, 1);
            return true;
        }
    }
    return false;
}

/* The parser's keyword tables. */

enum { FOG_NONE = 0, FOG_EXP = 1, FOG_EXP2 = 2, FOG_LINEAR = 3 };

struct KeywordValue { const char *name; uint32_t value; };

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
static bool lookup(char *tok, const KeywordValue *tab, int n, uint32_t *out)
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
class ThemeParser {
public:
    /* Parses the stream into block; the parse state starts cleared. */
    void run(Game *g, RenderDevice *dev, ThemeAssetBlock *b,
             std::istream &f)
    {
        *this = ThemeParser();
        game = g; d3d = dev; block = b; in = &f;
        parseFile();
    }

private:
    Game            *game;
    RenderDevice    *d3d;
    ThemeAssetBlock *block;
    std::istream    *in;

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
        return s->records() + i;
    }
    static SceneSubObject *subObjectAt(ThemeLevelObject *r, int i)
    {
        return r->subObjects() + i;
    }

    Texture *loadTexture(char *name, char *alphaTok)
    {
        uint32_t alpha = is(alphaTok, "alpha") ? 1 : 0;
        return g_textureManager.getOrLoad(d3d,
                                        name, alpha, 0);
    }

    void fog(bool inEnvironment);
    void sky(bool inEnvironment);
    void particleSystem(ThemeObjectTypeSlot *slot, Cursor<ThemeLevelObject> &rec);
    void explode(ThemeObjectTypeSlot *slot, ThemeLevelObject *rec);
};

/* The next line that is not blank or a comment, tokenized; false at the end. */
bool ThemeParser::nextLine()
{
    std::string line;
    while (std::getline(*in, line)) {
        for (auto &t : tok)
            std::fill(std::begin(t), std::end(t), 0);
        ntok = 0;

        std::istringstream words(line);
        std::string w;
        if (!(words >> w) || w.compare(0, 2, "//") == 0)
            continue;
        do {
            if (ntok < TOKEN_SLOTS)
                strncpy(tok[ntok], w.c_str(), TOKEN_MAX - 1);
            ntok++;
        } while (words >> w);
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
            slot = &block->slots_[k.type];
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
                rec.at->flScaleX_ = 1.0f;
                rec.at->flScaleY_ = 1.0f;
                rec.at->flScaleZ_ = 1.0f;
            }
            if (!parseRecordBlock(slot, rec.at))
                return false;
        } else if (is(tok[0], "}")) {
            if (slot)
                slot->dwInstanceCount_ = rec.count;
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
    ParticleSystem *ps = ParticleSystem::loadFile(tok[1]);
    if (ps == NULL) {
        if (slot) rec->kind_ = THEME_KIND_NONE;
        return;
    }
    if (slot) {
        rec->kind_ = THEME_KIND_PARTICLESYSTEM;
        rec->pParticleSystems_[0] = ps;
    }
    if (is(tok[2], "movable1") && slot) rec->dwMovableType_ = 1;
    if (is(tok[2], "movable2") && slot) rec->dwMovableType_ = 2;

    unsigned count = (unsigned)atoi(tok[3]);
    if (count < 2 || count > 16) {
        if (slot) rec->dwInstanceCount_ = 1;
    } else {
        for (unsigned i = 1; i < count; i++)
            if (slot) rec->pParticleSystems_[i] = ps->clone();
        if (slot) rec->dwInstanceCount_ = count;
    }
}

void ThemeParser::fog(bool inEnvironment)
{
    if (!inEnvironment || ntok <= 3)
        return;
    FogState fog = d3d->fog();
    fog.enable = true;
    block->bFogEnabled_ = 1;

    uint32_t mode = FOG_NONE;
    lookup(tok[1], kFogModes, 4, &mode);
    fog.mode = (FogMode)mode;

    char *colourTok;
    if (mode == FOG_LINEAR) {
        fog.start = atof_f(tok[2]);
        fog.end   = atof_f(tok[3]);
        colourTok = tok[4];
    } else {
        fog.density = atof_f(tok[2]);
        colourTok = tok[3];
    }
    char *end;
    fog.color = (uint32_t)strtol(colourTok, &end, 16);
    d3d->SetFog(fog);
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
    unsigned int ok = block->sky_.buildFromFaceNames(d3d,
                                             up, dn, fr, bk, lf, rt,
                                             d3d->bitDepth());
    if ((ok & 0xff) == 0)
        g_logger.logMessage(3, "SKY: *ERROR* failed loading %s", tok[1]);
    else
        g_logger.logMessage(1, "SKY: %s loaded", tok[1]);
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
            rec.at->kind_ = THEME_KIND_NONE;
            return;
        }
        rec.at->kind_  = THEME_KIND_MODEL;
        rec.at->pMesh_ = mesh;
        rec.at->wrapper_.setMesh(mesh);
        rec.at->animTable_.load(tok[2]);
        if (is(tok[3], "nomovestates"))
            rec.at->bNoMoveStates_ = 1;
        return;
    }
    if (is(tok[0], "field")) {
        open(rec, slot);
        if (slot) rec.at->kind_ = THEME_KIND_FIELD;
        return;
    }
    if (is(tok[0], "billboard")) {
        if (ntok <= 1)
            return;
        open(rec, slot);
        if (slot) {
            rec.at->kind_ = THEME_KIND_BILLBOARD;
            rec.at->flBillboardScale_ = atof_f(tok[1]);
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
                block->images_[k.slot] = loadTexture(tok[1], tok[2]);
            return;
        }
    }
    for (int c = 0; c < THEME_COLOR_COUNT; c++) {
        if (is(tok[0], kTextColorKeywords[c])) {
            if (inEnvironment && ntok > 2) {
                char *end;
                block->textColors_[c].color1 = (uint32_t)strtol(tok[1], &end, 16);
                block->textColors_[c].color2 = (uint32_t)strtol(tok[2], &end, 16);
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
            block->flSideHeight_ = atof_f(tok[1]);
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
                rec->dwSubObjectCount_ = sub.count;
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
    if (ntok <= 6 || slot == NULL || rec->pMesh_ == NULL)
        return;
    rec->bExplode_ = 1;
    rec->explode_.allocateExplodeBuffers(rec->pMesh_);
    Gen_FillGaussianField(&rec->explode_, atof_f(tok[1]), atof_f(tok[2]));
    rec->explode_.storeExplodeScaledCount(atof_f(tok[3]));

    const double a = (double)-1.5707963705062866f;
    const double c = cos(a), s = sin(a);
    const float x = atof_f(tok[4]), y = atof_f(tok[5]), z = atof_f(tok[6]);
    float out[3] = { x, (float)(c * y + s * z), (float)(-s * y + c * z) };
    const float w = 1.0f;
    if (w != 1.0f)
        for (float &v : out) v /= w;
    rec->flExplodeDir_[0] = out[0];
    rec->flExplodeDir_[1] = out[1];
    rec->flExplodeDir_[2] = out[2];
}

void ThemeParser::recordKeyword(ThemeObjectTypeSlot *slot, ThemeLevelObject *rec,
                                Cursor<SceneSubObject> &sub)
{
    if (is(tok[0], "texture")) {
        if (ntok <= 1)
            return;
        open(sub, rec);
        uint32_t alpha = is(tok[2], "alpha") ? 1 : 0;
        if (slot)
            sub.at->pTexture = g_textureManager.getOrLoad(d3d, tok[1], alpha, 0);
    } else if (is(tok[0], "position")) {
        if (ntok > 3 && slot) {
            rec->flPosX_ = atof_f(tok[1]); rec->flPosY_ = atof_f(tok[2]); rec->flPosZ_ = atof_f(tok[3]);
        }
    } else if (is(tok[0], "scale")) {
        if (ntok > 3 && slot) {
            rec->flScaleX_ = atof_f(tok[1]); rec->flScaleY_ = atof_f(tok[2]); rec->flScaleZ_ = atof_f(tok[3]);
        }
    } else if (is(tok[0], "rotate")) {
        if (ntok > 3 && slot) {
            rec->flRotRateX_ = atof_f(tok[1]); rec->flRotRateY_ = atof_f(tok[2]); rec->flRotRateZ_ = atof_f(tok[3]);
        }
    } else if (is(tok[0], "randomyangle")) {
        if (slot) rec->bRandomYAngle_ = 1;
    } else if (is(tok[0], "nozwrite")) {
        if (slot) rec->bNoZWrite_ = 1;
    } else if (is(tok[0], "noshadow")) {
        if (slot) rec->bNoShadow_ = 1;
    } else if (is(tok[0], "oscillate")) {
        if (ntok <= 2)
            return;
        if (slot) {
            rec->flOscillationAmplitude_ = atof_f(tok[1]);
            rec->flOscillationFrequency_ = atof_f(tok[2]);
        }
        // PRESERVED: no slot check from here on.
        if (is(tok[3], "random"))
            rec->bOscillateRandom_ = 1;
        rec->flOscillationPhase_ = (ntok < 5) ? 0.0f : atof_f(tok[4]);
    } else if (is(tok[0], "pump")) {
        if (ntok > 4 && slot)
            for (int i = 0; i < 4; i++)
                rec->flPump_[i] = atof_f(tok[1 + i]);
    } else if (is(tok[0], "lit")) {
        if (slot) rec->bLit_ = 1;
    } else if (is(tok[0], "specular")) {
        if (slot) rec->bSpecular_ = 1;
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
    uint32_t v;
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

bool ThemeAssetBlock::themeLoad(Game *game, RenderDevice *d3d,
                       char *path)
{
    release();
    {
        FogState fog = d3d->fog();
        fog.enable = false;
        d3d->SetFog(fog);
    }

    std::ifstream file(path);  // text mode: the CRT folds CRLF
    if (!file)
        return false;

    s_parser.run(game, d3d, this, file);

    theme_struct_dump_if_enabled(path);
    strcpy(themeName_, path);  // PRESERVED: unbounded
    return true;
}

/* The load, timed.  The time goes only to our log, never into game state. */
bool ThemeAssetBlock::load(Game *game, RenderDevice *d3d, char *path)
{
    const unsigned long long freq = sysdev::perfFrequency();
    const unsigned long long t0 = sysdev::perfCounter();

    bool ok = themeLoad(game, d3d, path);

    const unsigned long long t1 = sysdev::perfCounter();
    double ms = (double)(t1 - t0) * 1000.0 / (double)freq;
    g_logger.write("theme: %s %s in %.2f ms\n", path, ok ? "loaded" : "NOT opened", ms);
    return ok;
}

/* The theme sound table's lifecycle.  ReleaseAll clears the name and the
 * enabled flag of all 100 entries between two log lines, leaving the other
 * fields, and returns 0. */
int ThemeSoundTable::releaseAll()
{
    g_logger.logMessage(1, "TSM: trying to release all sounds");
    for (int i = 0; i < THEME_SOUND_COUNT; i++) {
        entries_[i].enabled = 0;
        entries_[i].name[0] = 0;
    }
    g_logger.logMessage(1, "TSM: all sounds released");
    return 0;
}

ThemeSoundTable::ThemeSoundTable()
{
    unknown8_  = 0;
    releaseAll();
}

ThemeSoundTable::~ThemeSoundTable()
{
}
