/* ASSET_PLAN.md Phase 5 -- the .thm reader, observing only.
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
    const char *kindNote = (r.dwKind <= 4) ? "" : "  SUSPICIOUS dwKind";
    log_write("THEME_STRUCT:   %s[%d] dwKind=%lu%s pMesh=%p%s subobj=%lu%s\n",
              slotName, i, (unsigned long)r.dwKind, kindNote, (void *)r.pMesh,
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
 * integer, a float that is NaN, a dwKind that is not 0..4) before trusting
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
        const LoadedImage &img = block->sky.faces[f].base;
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
