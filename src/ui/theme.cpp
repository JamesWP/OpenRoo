/* ASSET_PLAN.md Phase 5 -- the .thm loader.
 *
 * Two halves.  The top of this file is the oracle built before the
 * replacement: an observing-only re-reader (KAROO_THEME_DIAG) and a dump of
 * the populated block (KAROO_THEME_STRUCT_DIAG).  The bottom is the
 * replacement itself -- Theme_Load (0x0040c110) and the helpers only it
 * reaches: Theme_ReleaseBlock 0x0040bf30, Theme_ReleaseSlot 0x0043b720,
 * Theme_RegisterSound 0x004113e0 and ThemeSound_Add 0x004402d0.  Its other
 * callees were replaced in the same batch in their owners' files (model.cpp,
 * scenetexture.cpp, sky.cpp, shadowmesh.cpp).  The loader now opens the .thm
 * with OUR CRT, so assetio.cpp no longer sees it; it calls the two diag hooks
 * itself.
 *
 * ThemeFileLoader 0x0040c110 is a LINE-oriented parser around a switch on the
 * BRACE-NESTING DEPTH.  Its full shape is on its plate comment in Ghidra and in
 * ASSET_PLAN.md; the short version, which this file implements:
 *
 *   fgets(line, 0x100, f)                 -- text mode, so CRLF is already LF
 *   skip leading ' ' and '\t'
 *   drop the line if it now starts with "//" or '\n'
 *   strtok on " \t\n" into <= 16 slots of 0x100 bytes
 *   switch (depth) { 0..3 }, token[0] lowercased then strcmp'd
 *   '{' raises the depth, '}' lowers it; there is no other state
 *
 * ─── What this file is for, and what it is not ────────────────────────────
 *
 * It is the C++ half of Phase 5's oracle.  tools/thmparse.py implements the
 * same grammar in Python, independently, and accounts for every line of all
 * seven shipped themes.  This reader runs inside the game, on whatever theme
 * the game actually opens, and logs the same (depth, keyword, args) stream and
 * the same FNV-1a 32 rolling hash, so the two can be diffed.
 *
 * It does NOT observe the original's own line stream.  The original reads via
 * CrtFgetsLine 0x450743 from a FILE we hand back untouched, and intercepting
 * that would mean a per-site E8 patch on the one call at 0x0040C19F plus a new
 * gamecrt callback -- machinery for a diagnostic that retires the moment the
 * replacement lands.  So what this proves is narrower than "we agree with the
 * original", and is stated that way on purpose:
 *
 *   PROVEN  -- our C++ tokenizer and table walk agree with the validated
 *              Python oracle, on the real files, at runtime, including the
 *              text-mode CRLF handling and the 0x100 / 16-slot limits.
 *   NOT PROVEN -- that the game's CRT hands its own parser the same bytes.
 *              The only known way that could differ is text-mode translation,
 *              and both sides call the same CRT's fopen with the same "r".
 *
 * The defects below are the original's and are reproduced deliberately:
 *   - the 0x100 line buffer splits a longer line; the tail is parsed as a new
 *     line, not discarded;
 *   - there are 16 token slots, and tokens past the 16th are dropped here
 *     where the original would run off the end of its token area;
 *   - the whitespace skip runs BEFORE the comment test, so "   // x" is a
 *     comment but "Model x // y" yields "//" and "y" as ordinary tokens;
 *   - an unrecognised token[0] is silently ignored at every depth.  There is
 *     no error path in the original, which is why a count of unaccounted
 *     lines is the single most useful number this reader can print.
 *
 * KAROO_THEME_DIAG=1        one line per record, plus a per-file summary
 * KAROO_THEME_DIAG=summary  the summary only (record count, hash, unaccounted)
 *
 * Read by VALUE, never by presence -- see CLAUDE.md.  CONTROLS.md carries it.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "theme.h"
#include "log.h"
#include "game.h"
#include "direct3d.h"
#include "gamelog.h"
#include "gamestr.h"
#include "gameglobals.h"
#include "model.h"
#include "scenetexture.h"
#include "generators.h"
#include <math.h>

/* ─── The four depth tables ───────────────────────────────────────────────
 *
 * Source order is the original's if/else chain order.  tools/thmparse.py
 * --check-cpp parses these arrays out of this file and asserts they match its
 * own, so the two halves of the oracle cannot drift apart silently.
 */

static const char *const kDepth0[] = {
    "exit", "john", "catcher", "catcherfx", "thrower", "throwerfx",
    "paraglide", "paraglidefx", "bridge", "collfx", "protection",
    "protectionfx", "ammunition", "bomb", "explosion", "crystal", "crystalfx",
    "speed", "speedfx", "freeze", "life", "time", "switch", "surprise",
    "elevator", "platform", "plate", "side", "glue", "destructfield",
    "destructfieldfx", "jumppad", "teleporter", "slide", "ice", "obstacle",
    "obstaclefx", "stair", "environment", "{",
    NULL
};

static const char *const kDepth1[] = {
    "model", "nomovestates", "field", "billboard", "particlesystem",
    "movable1", "movable2",
    /* image pairs: a texture name, and an optional literal `alpha` in token[2] */
    "hud", "menu", "edge", "pointer", "radar", "freeze", "inversecontrol",
    "protection", "slowdwon", "speed",
    /* the 26 colour pairs */
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
    "fog", "sky", "sideheight", "sound", "{", "}",
    NULL
};

static const char *const kDepth2[] = {
    "texture", "position", "scale", "rotate", "randomyangle", "nozwrite",
    "noshadow", "oscillate", "random", "pump", "lit", "specular", "explode",
    "{", "}",
    NULL
};

static const char *const kDepth3[] = {
    "srcblend", "destblend", "condition", "flash", "pulse", "wobble", "turn",
    "environment", "scroll", "textureadress", "}",
    NULL
};

static const char *const *const kTables[4] = {
    kDepth0, kDepth1, kDepth2, kDepth3
};

/* ─── Tokenizer ───────────────────────────────────────────────────────────
 *
 * The original's exact limits.  LINE_MAX is fgets' n; TOKEN_SLOTS is the
 * 0x1000-byte token area divided by the 0x100 slot stride.
 */
#define LINE_MAX     0x100
#define TOKEN_SLOTS  16
#define TOKEN_MAX    0x100

static int mode_verbose(void)
{
    char v[32];
    DWORD n = GetEnvironmentVariableA("KAROO_THEME_DIAG", v, sizeof(v));
    if (n == 0 || n >= sizeof(v))
        return 0;                         /* unset -> inert */
    if (lstrcmpiA(v, "summary") == 0)
        return 1;                         /* on, summary only */
    return 2;                             /* on, one line per record */
}

static unsigned fnv1a(const unsigned char *p, unsigned n, unsigned h)
{
    for (unsigned i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x01000193u;
    }
    return h;
}

static char lower_one(char c)
{
    /* CrtStrLwr 0x4505b7's C-locale loop, byte for byte: strictly between
     * '@' and '[' on a SIGNED char compare.  Not tolower(), which is
     * locale-dependent and would also fold bytes >= 0x80. */
    return (c > '@' && c < '[') ? (char)(c + ' ') : c;
}

static void lower_inplace(char *s)
{
    for (; *s; s++)
        *s = lower_one(*s);
}

static int table_has(const char *const *tab, const char *kw)
{
    for (int i = 0; tab[i] != NULL; i++)
        if (strcmp(tab[i], kw) == 0)
            return 1;
    return 0;
}

/* ─── The reader ──────────────────────────────────────────────────────────
 *
 * Reads the whole file, collapses CRLF exactly as the CRT's text mode does
 * below fgets, then re-splits into fgets-sized chunks so the 0x100 truncation
 * behaves as the original's does.
 */
static void theme_scan(const char *path, int verbose)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        log_write("THEME: cannot reopen %s (%lu)\n", path,
                  (unsigned long)GetLastError());
        return;
    }
    DWORD size = GetFileSize(h, NULL);
    char *raw = (char *)malloc(size + 1);
    if (raw == NULL) {
        CloseHandle(h);
        log_write("THEME: out of memory for %s\n", path);
        return;
    }
    DWORD got = 0;
    ReadFile(h, raw, size, &got, NULL);
    CloseHandle(h);
    raw[got] = '\0';

    /* Text mode: CRLF -> LF.  Every shipped .thm is CRLF and the delimiter set
     * is " \t\n" with no '\r', so without this nothing matches at all.  That
     * is not a hypothetical -- it is how the Python oracle first failed. */
    DWORD n = 0;
    for (DWORD i = 0; i < got; i++) {
        if (raw[i] == '\r' && i + 1 < got && raw[i + 1] == '\n')
            continue;
        raw[n++] = raw[i];
    }
    raw[n] = '\0';

    int      depth = 0;
    unsigned records = 0, unaccounted = 0, hash = 0x811c9dc5u;
    int      lineno = 0;

    DWORD pos = 0;
    while (pos < n) {
        /* fgets(buf, LINE_MAX, f): at most LINE_MAX-1 chars, stopping after a
         * '\n'.  A longer line comes back in pieces, and each piece is parsed
         * as though it were a line of its own. */
        DWORD end = pos;
        while (end < n && raw[end] != '\n')
            end++;
        if (end < n)
            end++;                                  /* keep the '\n' */
        if (end - pos > LINE_MAX - 1)
            end = pos + (LINE_MAX - 1);

        char line[LINE_MAX];
        DWORD len = end - pos;
        memcpy(line, raw + pos, len);
        line[len] = '\0';
        pos = end;
        lineno++;

        char *s = line;
        while (*s == ' ' || *s == '\t')
            s++;
        if (s[0] == '\0' || s[0] == '\n' || (s[0] == '/' && s[1] == '/'))
            continue;

        /* strtok on " \t\n" */
        char *tok[TOKEN_SLOTS];
        int   ntok = 0;
        for (char *p = s; *p != '\0' && ntok < TOKEN_SLOTS; ) {
            while (*p == ' ' || *p == '\t' || *p == '\n')
                p++;
            if (*p == '\0')
                break;
            tok[ntok++] = p;
            while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n')
                p++;
            if (*p != '\0')
                *p++ = '\0';
        }
        if (ntok == 0)
            continue;

        char kw[TOKEN_MAX];
        lstrcpynA(kw, tok[0], sizeof(kw));
        lower_inplace(kw);

        if (!table_has(kTables[depth], kw)) {
            unaccounted++;
            log_write("THEME: %s:%d depth %d UNACCOUNTED '%s'\n",
                      path, lineno, depth, kw);
            continue;
        }

        /* The record, in tools/thmparse.py's byte-for-byte form:
         *   depth 0x1f keyword 0x1f arg 0x1f arg ... 0x1e
         * so the two hashes are directly comparable. */
        char rec[LINE_MAX * 2];
        int  rl = 0;
        rl += snprintf(rec + rl, sizeof(rec) - rl, "%d\x1f%s\x1f", depth, kw);
        for (int i = 1; i < ntok && rl < (int)sizeof(rec) - 2; i++)
            rl += snprintf(rec + rl, sizeof(rec) - rl, "%s%s",
                           tok[i], (i + 1 < ntok) ? "\x1f" : "");
        if (rl < (int)sizeof(rec) - 1)
            rec[rl++] = '\x1e';
        hash = fnv1a((const unsigned char *)rec, (unsigned)rl, hash);
        records++;

        if (verbose >= 2) {
            /* One log_write per record: log.cpp stamps every call, so emitting
             * the arguments separately would interleave a timestamp between
             * each of them. */
            char out[LINE_MAX * 2];
            int  ol = snprintf(out, sizeof(out), "THEME: %d  %-34s", depth, kw);
            for (int i = 1; i < ntok && ol < (int)sizeof(out) - 1; i++)
                ol += snprintf(out + ol, sizeof(out) - ol, " %s", tok[i]);
            log_write("%s\n", out);
        }

        if (kw[0] == '{' && kw[1] == '\0') {
            depth++;
            if (depth > 3) {
                log_write("THEME: %s:%d depth ran past 3 -- clamped\n",
                          path, lineno);
                depth = 3;
            }
        } else if (kw[0] == '}' && kw[1] == '\0') {
            depth--;
            if (depth < 0) {
                log_write("THEME: %s:%d unbalanced '}'\n", path, lineno);
                depth = 0;
            }
        }
    }

    if (depth != 0)
        log_write("THEME: %s ends at depth %d -- unclosed block\n", path, depth);

    log_write("THEME: %s  %u records  fnv1a 0x%08x  %u unaccounted%s\n",
              path, records, hash, unaccounted,
              (unaccounted == 0 && depth == 0) ? "  OK" : "  MISMATCH");
    free(raw);
}

static int ends_with_thm(const char *path)
{
    size_t n = strlen(path);
    return n >= 4 && lstrcmpiA(path + n - 4, ".thm") == 0;
}

/* The one .thm currently open, keyed by FILE* rather than "last path opened":
 * ThemeFileLoader opens .ani and .par files mid-parse through the same hook,
 * and those must neither clear this nor trigger the dump. */
static void *s_thmFp = NULL;
static char  s_thmPath[MAX_PATH] = "";

void theme_diag_on_open(const char *path, void *fp)
{
    if (path == NULL || !ends_with_thm(path))
        return;

    if (fp != NULL) {
        s_thmFp = fp;
        lstrcpynA(s_thmPath, path, sizeof(s_thmPath));
    }

    int verbose = mode_verbose();
    if (verbose == 0)
        return;
    theme_scan(path, verbose);
}

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
    return !(f != f) && f > -1.0e6f && f < 1.0e6f;   /* f != f catches NaN */
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
                  r.explode.nVertexCount, r.explode.flExplodeScaledCount,
                  (r.pMesh != NULL && r.explode.nVertexCount > 0) ? "" : "  SUSPICIOUS explode");
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

/* Reads the live theme block at its fixed address, DAT_0046c890 -- no patch,
 * no allocation, this DLL and Karoo.exe share one address space.  Prints a
 * plausibility report, not a correctness proof: the point is to catch a
 * struct offset that is simply wrong (a pointer that looks like a small
 * integer, a float that is NaN, a kind that is not 0..4) before trusting
 * this layout for anything that writes.  Gated by KAROO_THEME_STRUCT_DIAG,
 * read by value per CLAUDE.md; fires once per real .thm close, i.e. after
 * the game's own ThemeFileLoader has fully populated the block. */
static void theme_struct_dump(const char *path)
{
    const ThemeAssetBlock *block = (const ThemeAssetBlock *)0x46c890;

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
        const LoadedImage &img = block->sky.Textures[f].base;
        const char *name = img.ImageName;
        int nameOk = name != NULL && (ULONG_PTR)name >= 0x10000;
        log_write("THEME_STRUCT: sky.faces[%d] surface=%p%s name=%p \"%.63s\"%s\n",
                  f, (void *)img.pTextureSurface,
                  ptr_plausible(img.pTextureSurface) ? "" : "  SUSPICIOUS",
                  (void *)name, nameOk ? name : "",
                  (name == NULL || nameOk) ? "" : "  SUSPICIOUS name");
    }
}

void theme_diag_on_close(void *fp)
{
    if (fp == NULL || fp != s_thmFp)
        return;
    s_thmFp = NULL;

    if (!struct_diag_enabled())
        return;
    theme_struct_dump(s_thmPath);
}

/* ═══ The loader (0x0040c110) and its helpers ═══════════════════════════════
 *
 * Written from the decompile and, where Ghidra gave up (the fog handler),
 * the listing.  The grammar is the one the oracle above implements; what
 * follows is what each keyword WRITES.  Record fields are ThemeLevelObject's
 * (theme.h); `rec` is the record the last model/field/billboard/
 * particlesystem line opened, `sub` the sub-object the last depth-2 texture
 * line opened.
 *
 * DEFECTS KEPT, beyond the tokenizer's (see the top of this file):
 *   - the record and sub-object cursors are never bounds-checked: a ninth
 *     record in a block runs into the next slot, a ninth texture into bLit;
 *   - oscillate's `random` flag and phase, and depth-3 `environment` and
 *     `textureadress`, write through the record with no NULL-slot check, so
 *     inside `environment { }` they would fault near address 0;
 *   - `particlesystem` loads the .par even when there is no slot to keep it
 *     in (inside `environment`), and leaks it;
 *   - the model and texture caches lowercase the token buffers in place.
 * One deliberate difference: tokens past the 16th are dropped, where the
 * original wrote them past its token area (no shipped theme has one).  The
 * token COUNT still counts them, as the original's did.
 */

/* Sub-objects of the block (ShadowMesh, WrapperObject, AnimTable, sky
 * textures) are handed to their owners by address.  Unlike the idiom in
 * doublesoundbuff.h, these really are misaligned -- the game packed the
 * block, and e.g. records sit at odd offsets -- which x86 tolerates and the
 * owners have always been called with.  Hence one suppression for the
 * loader, rather than at every call. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"

typedef void *(__attribute__((thiscall)) *theme_scalar_dtor_fn)(void *self, unsigned int flags);

static void delete_via_vtable(void *obj)
{
    theme_scalar_dtor_fn dtor = **(theme_scalar_dtor_fn **)obj;
    dtor(obj, 1);
}

/* 0x0043b720 */
extern "C" __declspec(dllexport) void __attribute__((fastcall))
Theme_ReleaseSlot(ThemeObjectTypeSlot *slot)
{
    for (int i = 0; i < 8; i++) {
        ThemeLevelObject &r = slot->records[i];
        ShadowMesh_Release(&r.explode);
        Wrapper_ReleaseSnapshot(&r.wrapper);
        for (DWORD k = 0; k < r.dwInstanceCount; k++) {
            if (r.pParticleSystems[k] != NULL) {
                delete_via_vtable(r.pParticleSystems[k]);
                r.pParticleSystems[k] = NULL;
            }
        }
    }
    memset(slot, 0, sizeof(*slot));
}

/* 0x0040bf30.  BUG KEPT: EXPLOSION's slot is not in the list, so its
 * particle systems and explode buffers are never released -- though the
 * memset below still zeroes it.  The order is the original's. */
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
    TextureManager_ReleaseAll(GG_TEXTURE_MANAGER);
    ModelManager_ClearReleaseFree(GG_MODEL_MANAGER);
    for (ThemeObjectType t : kReleaseOrder)
        Theme_ReleaseSlot(&block->slots[t]);
    for (int f = 0; f < 6; f++)
        Texture_ReleaseD3DTexture(&block->sky.Textures[f]);
    memset(block, 0, sizeof(*block));
}

/* 0x004402d0.  "NONE" (case-exact, on the raw wave name) disables the entry
 * and does not log.  The path buffer is 256 bytes and the sprintf is
 * unbounded, as in the original. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
ThemeSound_Add(ThemeSoundTable *self, unsigned int id, const char *waveName,
               DWORD arg3, DWORD arg4)
{
    char path[256];
    sprintf(path, GS_THEME_SOUND_PATH, GS_GAME_DIR, waveName);

    SoundAssetName &e = self->entries[id & 0xffff];
    if (strcmp(waveName, GS_THEME_SOUND_NONE) == 0) {
        e.enabled = 0;
        return 0;
    }
    GameLog_LogMessage(GG_LOGGER, 1, GS_THEME_SOUND_ADD, id & 0xffff, path);
    strcpy(e.name, path);
    e.unknown104 = arg4;
    e.unknown108 = arg3;
    e.enabled    = 1;
    return 0;
}

/* 0x004113e0.  The event ids, in the original's test order.  An unknown
 * event registers nothing and returns false. */
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

/* ─── The parser ─────────────────────────────────────────────────────────── */

enum { FOG_NONE = 0, FOG_EXP = 1, FOG_EXP2 = 2, FOG_LINEAR = 3 };

struct KeywordValue { const char *name; DWORD value; };

/* srcblend's list; destblend's is the same minus its last entry. */
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

/* Look `tok` up (lowercased in place first); *out untouched on a miss. */
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

struct ThemeParser {
    Game            *game;
    Direct3D        *d3d;
    ThemeAssetBlock *block;
    GameLogger      *logger;

    int   depth         = 0;
    ThemeObjectTypeSlot *slot = NULL;  /* NULL outside an object block */
    bool  inEnvironment = false;
    int   record        = -1;
    int   sub           = -1;

    char     tok[TOKEN_SLOTS][TOKEN_MAX];
    unsigned ntok;

    /* Unchecked on purpose: see "DEFECTS KEPT".  With slot == NULL these
     * point near address 0, as the original's arithmetic did. */
    ThemeLevelObject *rec() const
    {
        return reinterpret_cast<ThemeLevelObject *>(
            reinterpret_cast<BYTE *>(slot) + offsetof(ThemeObjectTypeSlot, records)
            + record * (int)sizeof(ThemeLevelObject));
    }
    SceneSubObject *subobj() const
    {
        return reinterpret_cast<SceneSubObject *>(
            reinterpret_cast<BYTE *>(rec()) + offsetof(ThemeLevelObject, pSubObjects)
            + sub * (int)sizeof(SceneSubObject));
    }

    SceneTexture *loadTexture(char *name, char *alphaTok)
    {
        DWORD alpha = is(alphaTok, "alpha") ? 1 : 0;
        return TextureManager_GetOrLoad(GG_TEXTURE_MANAGER, d3d->pDD4, d3d->pDevice,
                                        name, alpha, 0, 0);
    }

    void line();
    void depth0();
    void depth1();
    void depth2();
    void depth3();
    void fog();
    void sky();
    void particleSystem();
    void explode();
};

void ThemeParser::depth0()
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
    } else if (is(tok[0], "{")) {
        depth = 1;
    } else {
        slot = NULL;
    }
}

void ThemeParser::particleSystem()
{
    if (ntok <= 1)
        return;
    record++;
    ParticleSystem *ps = Particle_LoadFromFile(tok[1], logger);
    if (ps == NULL) {
        if (slot) rec()->kind = THEME_KIND_NONE;
        return;
    }
    if (slot) {
        rec()->kind = THEME_KIND_PARTICLESYSTEM;
        rec()->pParticleSystems[0] = ps;
    }
    if (is(tok[2], "movable1") && slot) rec()->dwMovableType = 1;
    if (is(tok[2], "movable2") && slot) rec()->dwMovableType = 2;

    unsigned count = (unsigned)atoi(tok[3]);
    if (count < 2 || count > 16) {
        if (slot) rec()->dwInstanceCount = 1;
    } else {
        for (unsigned i = 1; i < count; i++)
            if (slot) rec()->pParticleSystems[i] = Particle_CloneSystem(ps);
        if (slot) rec()->dwInstanceCount = count;
    }
}

void ThemeParser::fog()
{
    if (!inEnvironment || ntok <= 3)
        return;
    IDirect3DDevice3 *dev = d3d->pDevice;
    dev->SetRenderState(D3DRENDERSTATE_FOGENABLE, 1);
    block->bFogEnabled = 1;

    DWORD mode = FOG_NONE;
    lookup(tok[1], kFogModes, 4, &mode);
    dev->SetRenderState(D3DRENDERSTATE_FOGTABLEMODE, mode);

    char *colourTok;
    if (mode == FOG_LINEAR) {
        dev->SetRenderState(D3DRENDERSTATE_FOGTABLESTART, float_bits(atof_f(tok[2])));
        dev->SetRenderState(D3DRENDERSTATE_FOGTABLEEND,   float_bits(atof_f(tok[3])));
        colourTok = tok[4];
    } else {
        dev->SetRenderState(D3DRENDERSTATE_FOGTABLEDENSITY, float_bits(atof_f(tok[2])));
        colourTok = tok[3];
    }
    char *end;
    dev->SetRenderState(D3DRENDERSTATE_FOGCOLOR, (DWORD)strtol(colourTok, &end, 16));
}

void ThemeParser::sky()
{
    if (!inEnvironment || ntok <= 1)
        return;
    /* 0x100 each and unbounded, as the original's stack buffers were. */
    char up[0x100], dn[0x100], fr[0x100], bk[0x100], lf[0x100], rt[0x100];
    sprintf(up, GS_THEME_SKY_UP, tok[1]);
    sprintf(dn, GS_THEME_SKY_DN, tok[1]);
    sprintf(fr, GS_THEME_SKY_FR, tok[1]);
    sprintf(bk, GS_THEME_SKY_BK, tok[1]);
    sprintf(lf, GS_THEME_SKY_LF, tok[1]);
    sprintf(rt, GS_THEME_SKY_RT, tok[1]);
    unsigned int ok = Sky_BuildFromFaceNames(&block->sky, d3d->pDD4, d3d->pDevice,
                                             up, dn, fr, bk, lf, rt,
                                             d3d->pSelectedMode->dwBitDepth);
    if (logger != NULL) {
        if ((ok & 0xff) == 0)
            GameLog_LogMessage(logger, 3, GS_THEME_SKY_FAILED, tok[1]);
        else
            GameLog_LogMessage(logger, 1, GS_THEME_SKY_LOADED, tok[1]);
    }
}

void ThemeParser::depth1()
{
    if (is(tok[0], "model")) {
        if (slot == NULL || ntok <= 1)
            return;
        record++;
        CFaktMesh *mesh = ModelManager_FindOrImport(GG_MODEL_MANAGER, tok[1]);
        if (mesh == NULL) {
            rec()->kind = THEME_KIND_NONE;
            return;
        }
        rec()->kind  = THEME_KIND_MODEL;
        rec()->pMesh = mesh;
        Wrapper_SetMesh(&rec()->wrapper, mesh);
        Ani_LoadAnimationFile(&rec()->animTable, tok[2], logger);
        if (is(tok[3], "nomovestates"))
            rec()->bNoMoveStates = 1;
        return;
    }
    if (is(tok[0], "field")) {
        record++;
        if (slot) rec()->kind = THEME_KIND_FIELD;
        return;
    }
    if (is(tok[0], "billboard")) {
        if (ntok <= 1)
            return;
        record++;
        if (slot) {
            rec()->kind = THEME_KIND_BILLBOARD;
            rec()->flBillboardScale = atof_f(tok[1]);
        }
        return;
    }
    if (is(tok[0], "particlesystem")) {
        particleSystem();
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
        fog();
    } else if (is(tok[0], "sky")) {
        sky();
    } else if (is(tok[0], "sideheight")) {
        if (inEnvironment && ntok > 1)
            block->flSideHeight = atof_f(tok[1]);
    } else if (is(tok[0], "sound")) {
        if (inEnvironment && ntok > 2)
            Theme_RegisterSound(game, tok[1], tok[2]);
    } else if (is(tok[0], "{")) {
        depth = 2;
        if (slot) {
            rec()->flScaleX = 1.0f;
            rec()->flScaleY = 1.0f;
            rec()->flScaleZ = 1.0f;
        }
    } else if (is(tok[0], "}")) {
        if (slot)
            slot->dwInstanceCount = record + 1;
        slot = NULL;
        inEnvironment = false;
        record = -1;
        depth = 0;
    }
}

/* The explode direction: (t4, t5, t6, 1) times an X rotation by the float
 * -pi/2, as a row vector, then divided by w (always 1).  Computed in double
 * with cos()/sin(); the original's x87 chain differs only in the last bits.
 * The original also formats "VECTOR(%f, %f, %f)\n" into a stack buffer
 * nothing reads; that is omitted. */
void ThemeParser::explode()
{
    if (ntok <= 6 || slot == NULL || rec()->pMesh == NULL)
        return;
    ThemeLevelObject *r = rec();
    r->bExplode = 1;
    ShadowMesh_AllocateExplodeBuffers(&r->explode, r->pMesh);
    Gen_FillGaussianField(&r->explode, atof_f(tok[1]), atof_f(tok[2]));
    ShadowMesh_StoreExplodeScaledCount(&r->explode, atof_f(tok[3]));

    const double a = (double)-1.5707963705062866f;
    const double c = cos(a), s = sin(a);
    const float x = atof_f(tok[4]), y = atof_f(tok[5]), z = atof_f(tok[6]);
    float out[3] = { x, (float)(c * y + s * z), (float)(-s * y + c * z) };
    const float w = 1.0f;
    if (w != 1.0f)
        for (float &v : out) v /= w;
    r->flExplodeDir[0] = out[0];
    r->flExplodeDir[1] = out[1];
    r->flExplodeDir[2] = out[2];
}

void ThemeParser::depth2()
{
    ThemeLevelObject *r = rec();
    if (is(tok[0], "texture")) {
        if (ntok <= 1)
            return;
        sub++;
        DWORD alpha = is(tok[2], "alpha") ? 1 : 0;
        if (slot)
            subobj()->pTexture = TextureManager_GetOrLoad(GG_TEXTURE_MANAGER, d3d->pDD4,
                                                          d3d->pDevice, tok[1], alpha, 0, 0);
    } else if (is(tok[0], "position")) {
        if (ntok > 3 && slot) {
            r->flPosX = atof_f(tok[1]); r->flPosY = atof_f(tok[2]); r->flPosZ = atof_f(tok[3]);
        }
    } else if (is(tok[0], "scale")) {
        if (ntok > 3 && slot) {
            r->flScaleX = atof_f(tok[1]); r->flScaleY = atof_f(tok[2]); r->flScaleZ = atof_f(tok[3]);
        }
    } else if (is(tok[0], "rotate")) {
        if (ntok > 3 && slot) {
            r->flRotRateX = atof_f(tok[1]); r->flRotRateY = atof_f(tok[2]); r->flRotRateZ = atof_f(tok[3]);
        }
    } else if (is(tok[0], "randomyangle")) {
        if (slot) r->bRandomYAngle = 1;
    } else if (is(tok[0], "nozwrite")) {
        if (slot) r->bNoZWrite = 1;
    } else if (is(tok[0], "noshadow")) {
        if (slot) r->bNoShadow = 1;
    } else if (is(tok[0], "oscillate")) {
        if (ntok <= 2)
            return;
        if (slot) {
            r->flOscillationAmplitude = atof_f(tok[1]);
            r->flOscillationFrequency = atof_f(tok[2]);
        }
        /* No slot check from here on (DEFECTS KEPT). */
        if (is(tok[3], "random"))
            r->bOscillateRandom = 1;
        r->flOscillationPhase = (ntok < 5) ? 0.0f : atof_f(tok[4]);
    } else if (is(tok[0], "pump")) {
        if (ntok > 4 && slot)
            for (int i = 0; i < 4; i++)
                r->flPump[i] = atof_f(tok[1 + i]);
    } else if (is(tok[0], "lit")) {
        if (slot) r->bLit = 1;
    } else if (is(tok[0], "specular")) {
        if (slot) r->bSpecular = 1;
    } else if (is(tok[0], "explode")) {
        explode();
    } else if (is(tok[0], "{")) {
        depth = 3;
    } else if (is(tok[0], "}")) {
        if (slot)
            r->dwSubObjectCount = sub + 1;
        sub = -1;
        depth = 1;
    }
}

void ThemeParser::depth3()
{
    SceneSubObject *so = subobj();
    DWORD v;
    if (is(tok[0], "srcblend")) {
        if (ntok > 1 && lookup(tok[1], kBlends, 12, &v) && slot)
            so->dwBlendSrc = v;
    } else if (is(tok[0], "destblend")) {
        if (ntok > 1 && lookup(tok[1], kBlends, 11, &v) && slot)
            so->dwBlendDst = v;
    } else if (is(tok[0], "condition")) {
        if (ntok > 1 && lookup(tok[1], kConditions, 6, &v) && slot)
            so->dwVisibilityGate = v;
    } else if (is(tok[0], "flash")) {
        if (ntok > 3 && slot) {
            so->effect = SUBOBJ_EFFECT_FLASH;
            for (int i = 0; i < 3; i++) so->flEffectParams[i] = atof_f(tok[1 + i]);
        }
    } else if (is(tok[0], "pulse")) {
        if (ntok > 1 && slot) {
            so->effect = SUBOBJ_EFFECT_PULSE;
            for (int i = 0; i < 2; i++) so->flEffectParams[i] = atof_f(tok[1 + i]);
        }
    } else if (is(tok[0], "wobble")) {
        if (ntok > 3 && slot) {
            so->effect = SUBOBJ_EFFECT_WOBBLE;
            for (int i = 0; i < 3; i++) so->flEffectParams[i] = atof_f(tok[1 + i]);
        }
    } else if (is(tok[0], "turn")) {
        if (ntok > 1 && slot) {
            so->effect = SUBOBJ_EFFECT_TURN;
            so->flEffectParams[0] = atof_f(tok[1]);
        }
    } else if (is(tok[0], "environment")) {
        so->effect = SUBOBJ_EFFECT_ENVIRONMENT;          /* no slot check */
    } else if (is(tok[0], "scroll")) {
        if (ntok > 2 && slot) {
            so->effect = SUBOBJ_EFFECT_SCROLL;
            for (int i = 0; i < 2; i++) so->flEffectParams[i] = atof_f(tok[1 + i]);
        }
    } else if (is(tok[0], "textureadress")) {
        if (ntok > 1 && lookup(tok[1], kTextureAddress, 4, &v))
            so->dwTexAddress = v;                        /* no slot check */
    } else if (is(tok[0], "}")) {
        depth = 2;
    }
}

void ThemeParser::line()
{
    switch (depth) {
    case 0: depth0(); break;
    case 1: depth1(); break;
    case 2: depth2(); break;
    case 3: depth3(); break;
    }
}

/* One parser per call; 4 KB of tokens, so not on the stack. */
static ThemeParser s_parser;

extern "C" __declspec(dllexport) bool __cdecl
Theme_Load(Game *game, Direct3D *d3d, ThemeAssetBlock *block, char *path,
           GameLogger *logger)
{
    Theme_ReleaseBlock(block);
    d3d->pDevice->SetRenderState(D3DRENDERSTATE_FOGENABLE, 0);

    FILE *fp = fopen(path, "r");      /* TEXT mode: the CRT folds CRLF */
    if (fp == NULL)
        return false;
    theme_diag_on_open(path, fp);

    ThemeParser &p = s_parser;
    p = ThemeParser();
    p.game = game; p.d3d = d3d; p.block = block; p.logger = logger;

    char buf[LINE_MAX];
    while (!feof(fp)) {
        if (fgets(buf, LINE_MAX, fp) == NULL)
            continue;
        memset(p.tok, 0, sizeof(p.tok));
        p.ntok = 0;

        char *s = buf;
        while (*s == ' ' || *s == '\t')
            s++;
        if ((s[0] == '/' && s[1] == '/') || s[0] == '\n')
            continue;
        for (char *t = strtok(s, " \t\n"); t != NULL; t = strtok(NULL, " \t\n")) {
            if (p.ntok < TOKEN_SLOTS)
                strcpy(p.tok[p.ntok], t);
            p.ntok++;
        }
        p.line();
    }

    theme_diag_on_close(fp);
    fclose(fp);
    strcpy(block->themeName, path);   /* unbounded, as the original */
    return true;
}

#pragma GCC diagnostic pop
