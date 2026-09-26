/* ASSET_PLAN.md Phase 4 (second cycle) — the .leo reader.
 *
 *   0x4235f0  OpenExtraObjectsFile(this, const char *name) -> BOOL
 *             __thiscall, ret 4.  UD2-stubbed.
 *
 * Level3DExtraObjects: the per-level list of extra scene objects (models,
 * billboards, particle systems, sounds).  Phase 0's asset log attributed all
 * 81 .leo opens in a level-report run to the fopen site inside this function.
 *
 * ─── It is the same parser as the .jjs reader ─────────────────────────────
 *
 * Character-at-a-time fgetc, ';' terminates an entry, the entry is
 * NUL-terminated and "\n" appended, and ONE MORE CHARACTER is consumed and
 * discarded after the ';'.  Identical in shape to
 * ReadInstructionScriptForLevel (0x41d720, src/ui/scriptplayer.cpp), down to
 * the 16-bit-masked accumulation index.
 *
 * The one structural difference is what happens to a finished entry: the .jjs
 * reader COPIES it into a 1000-byte record, while this one HANDS IT to
 * ParseExtraObjectEntry (0x423700) and keeps no copy.
 *
 * ─── The entry handler is ours too (ENDGAME_PLAN E1) ────────────────────
 *
 *   0x423700  ParseExtraObjectEntry  -> parseEntry     builds one record
 *   0x424df0  (blend name lookup)    -> blendFromName  "ZERO".."BOTHINVSRCALPHA" = D3DBLEND 1..13
 *   0x425170  (store src/dest blend) -> setBlend
 *   0x425250  (address mode lookup)  -> addressFromName "WRAP".."BORDER" = D3DTADDRESS 1..4
 *
 * An entry is split on " ,\t\n;".  Words are skipped until one names a kind
 * (Sound, Particle, Model, Billboard); no kind, no record.  Each kind reads
 * its fields in order and stops at the first missing word, keeping what it
 * has.  Particles, Models and Billboards may end in a path the object
 * follows: SPLINE_DYNAMIC|SPLINE_STATIC <ms per lap> then x,y,z triples (the
 * Water01 ray, 10 points, 40 s).  Shipped content: 255 Model, 9 Particle,
 * 13 Sound, no Billboard; 5 spline entries, all Model, all DYNAMIC.
 *
 * Oracle: KAROO_LEO_RECDUMP (recDump below) over a level-report run, against
 * tests/leo/records.txt -- captured from the ORIGINAL handler before this
 * replacement, 32 loads, raw-byte hash per record.
 *
 * Game bugs kept (marked BUG): a textured Billboard bumps the object count
 * twice, so its spline lands in the next record; destBlend is left unwritten
 * when srcBlend is 0; the spline point count is unbounded; atof/atoi/strcpy
 * are handed a missing (NULL) word exactly where the original hands it one;
 * Sound and Particle build "<GameDir>\(null)" from a missing name.
 *
 * ─── The two counters are different things ────────────────────────────────
 *
 *   this + 0x0c      WORD, incremented HERE, once per ';' -- entries SEEN
 *   this + 0xf400e   WORD, incremented by the handler -- objects BUILT
 *
 * They diverge whenever an entry is malformed enough that the handler bails
 * before committing an object, so neither is a substitute for the other.
 * Both are zeroed before the parse.
 *
 * ─── Defects and oddities preserved deliberately ──────────────────────────
 *
 * 1. The accumulation index is masked to 16 bits against a ~984-byte stack
 *    buffer, so a longer entry smashes the original's frame.  As in
 *    scriptplayer.cpp the replacement uses a 64 KB buffer: identical for every
 *    input the original survives, merely safe beyond it.  Measured over the
 *    shipped content: 33 .leo files, 459 entries, longest 270 bytes.
 * 2. The EOF flag is tested before each read, so the -1 from the read that
 *    hits EOF is stored as 0xFF into the pending entry.
 * 3. A trailing entry with no ';' is discarded -- the handler never sees it.
 * 4. The entry count is bumped BEFORE the handler runs, so it counts entries
 *    submitted, including any the handler rejects.
 * 5. A failed open returns 0 with both counters already zeroed.
 * 6. The path is built with sprintf into a stack buffer with no bounds check.
 *
 * ─── Visual proof ─────────────────────────────────────────────────────────
 *
 * KAROO_LEO_FX=nomodels drops every entry whose kind word is "Model" (tested
 * in parseEntry), so the level's extra models vanish while its particles and
 * sounds stay.  Until 2026-09-23 it tested whether the entry TEXT began with
 * "Model", which missed every entry preceded by whitespace -- 19 of 26 in
 * Forest\Start.leo, the boxes and bush-trees James saw survive it.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "extraobjects.h"
#include "static.h"
#include "soundmanager.h"
#include "gamestr.h"
#include "gamelog.h"
#include "gameglobals.h"
#include <stdlib.h>
#include <stdlib.h>


#define LEO_LOG_FIRST    6

static char s_entry[0x10000];      /* the 16-bit index's full range */

static bool fx_nomodels(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (GetEnvironmentVariableA("KAROO_LEO_FX", buf, sizeof(buf)))
            cached = (lstrcmpiA(buf, "nomodels") == 0);
        log_write("leo: FX mode = %s\n", cached ? "nomodels" : "off");
    }
    return cached != 0;
}

/* KAROO_LEO_RECDUMP=<path> -- appends every record the entry handler built,
 * field by field, plus an FNV-1a hash over each record.
 * Records 0..objectCount INCLUSIVE: the one past the end is where a textured
 * Billboard's spline lands (ParseExtraObjectEntry bumps the count twice).
 * Every byte outside the three path strings is hashed raw, stale data from
 * earlier levels included -- deliberately: the load order is deterministic,
 * so a replacement that writes one byte the original did not is caught.
 * The path strings are hashed (and printed) up to their terminator with the
 * game directory stripped, so the dump is the same in every checkout.
 * The oracle for the ParseExtraObjectEntry replacement: captured from the
 * original, now tests/levelreport/LeoRecords.txt. */

/* `s` with a leading "<game dir>\" removed, compared case-insensitively.
 * The paths are built from g_gameDir, so that is the prefix to strip. */
static const char *strip_game_dir(const char *s)
{
    size_t n = strlen(g_gameDir);
    return (_strnicmp(s, g_gameDir, n) == 0 && s[n] == '\\') ? s + n + 1 : s;
}

static void fnv(unsigned long *h, const void *p, size_t n)
{
    for (size_t k = 0; k < n; k++) {
        *h ^= ((const unsigned char *)p)[k];
        *h *= 16777619UL;
    }
}

/* A path field: its game-dir-relative text and one terminator. */
static void fnv_path(unsigned long *h, const char *field, size_t size)
{
    const char *s = strip_game_dir(field);
    fnv(h, s, strnlen(s, size - (size_t)(s - field)) + 1);
}

void ExtraObjects::recDump(const char *path)
{
    char out[MAX_PATH];
    if (!GetEnvironmentVariableA("KAROO_LEO_RECDUMP", out, sizeof(out)))
        return;
    FILE *f = fopen(out, "ab");
    if (!f)
        return;
    fprintf(f, "== %s objects=%u entries=%u\n", path, objectCount_, entries_);
    for (unsigned i = 0; i <= objectCount_ && i < RECORD_MAX; i++) {
        const ExtraObjectRecord *r = &records_[i];
        const unsigned char *b = (const unsigned char *)r;
        unsigned long h = 2166136261UL;
        fnv_path(&h, r->file, sizeof(r->file));
        fnv(&h, b + offsetof(ExtraObjectRecord, position),
            offsetof(ExtraObjectRecord, animationFile) - offsetof(ExtraObjectRecord, position));
        fnv_path(&h, r->animationFile, sizeof(r->animationFile));
        fnv_path(&h, r->textureFile, sizeof(r->textureFile));
        fnv(&h, b + offsetof(ExtraObjectRecord, lit),
            sizeof(*r) - offsetof(ExtraObjectRecord, lit));

        fprintf(f, "[%u] hash=%08lx kind=%u file=%.64s pos=%g,%g,%g v2=%g,%g,%g\n",
                i, h, r->kind, strip_game_dir(r->file),
                r->position[0], r->position[1], r->position[2],
                r->field_10c[0], r->field_10c[1], r->field_10c[2]);
        fprintf(f, "    anim=%.64s tex=%.64s lit=%d blend=%d,%d addr=%u size=%g\n",
                strip_game_dir(r->animationFile), strip_game_dir(r->textureFile),
                r->lit, r->srcBlend, r->destBlend, r->textureAddress, r->billboardSize);
        fprintf(f, "    spline=%u time=%d points=%u sound=%g\n",
                r->splineMode, r->splineTime, r->splinePointCount, r->soundParam);
        for (unsigned p = 0; p < r->splinePointCount && p < 0x100; p++)
            fprintf(f, "      %g,%g,%g\n", r->splinePoints[p][0],
                    r->splinePoints[p][1], r->splinePoints[p][2]);
    }
    fclose(f);
}

extern "C" __declspec(dllexport) int __attribute__((thiscall))
Leo_OpenExtraObjectsFile(ExtraObjects *self, const char *name)
{
    return self->openFile(name);
}

/* The counters are ExtraObjects fields: entries_ (+0xc, entries seen) and
 * objectCount_ (+0xf400e, objects the handler built). */
int ExtraObjects::openFile(const char *name)
{
    char path[512];
    FILE *fp;
    unsigned idx = 0;
    unsigned long s_hash = 2166136261UL;
    static int logged = 0;

    objectCount_ = 0;
    entries_    = 0;

    sprintf(path, "%s\\Level3DExtraObjects\\%s.leo", g_gameDir, name);

    fp = fopen(path, "r");
    if (fp == NULL)
        return 0;                          /* defect 5 */

    while (!feof(fp)) {
        int c = fgetc(fp);                 /* defect 2 */

        if ((char)c == ';') {
            s_entry[idx & 0xffff] = '\0';
            strcat(s_entry, "\n");
            idx = 0;

            entries_ =
                (WORD)(entries_ + 1);   /* defect 4 */

            for (const char *q = s_entry; *q; q++) {
                s_hash ^= (unsigned char)*q;
                s_hash *= 16777619UL;
            }

            parseEntry(s_entry);

            fgetc(fp);                     /* the discarded character */
        } else {
            s_entry[idx & 0xffff] = (char)c;   /* defect 1 */
            idx++;
        }
    }

    fclose(fp);

    recDump(path);

    /* KAROO_LEO_DUMP=<path> -- entry count and an FNV-1a 32 hash over every
     * entry text handed to the handler, for comparison against an independent
     * Python split of the same file.  ASSET_PLAN.md Phase 4. */
    {
        char dump[MAX_PATH];
        if (GetEnvironmentVariableA("KAROO_LEO_DUMP", dump, sizeof(dump))) {
            HANDLE h = CreateFileA(dump, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                                   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                char line[768];
                int len = wsprintfA(line, "%s entries=%u objects=%u hash=%08lx\r\n",
                                    path,
                                    (unsigned)entries_,
                                    (unsigned)objectCount_,
                                    s_hash);
                DWORD w = 0;
                WriteFile(h, line, (DWORD)len, &w, NULL);
                CloseHandle(h);
            }
        }
    }

    if (logged < LEO_LOG_FIRST) {
        logged++;
        log_write("leo: '%s' entries=%u objects=%u\n", path,
                  (unsigned)entries_,
                  (unsigned)objectCount_);
    }
    return 1;
}

/* ═══ 0x00425210 -- Level3DExtraObjects::ReleaseExtraObjectSoundBuffers ════
 * (was levelparse.cpp).  Halts and releases the first 255 records' sounds
 * -- not the 256th, which the ctor never zeroes either.  Its diag is
 * KAROO_LEVELPARSE_DIAG's, read here as levelparse.cpp reads it; the
 * running release count is what that file's DIAG lines print. */
static int      s_rel_diag       = -1;
static unsigned s_released       = 0;
static int      s_logged_release = 0;

static void diag_init(void)
{
    char buf[64];
    DWORD n;

    if (s_rel_diag >= 0)
        return;
    n = GetEnvironmentVariableA("KAROO_LEVELPARSE_DIAG", buf, sizeof(buf));
    s_rel_diag = (n > 0 && n < sizeof(buf) && strcmp(buf, "0") != 0);
}

unsigned ExtraObjects::releasedCount()
{
    return s_released;
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Leo_ReleaseExtraObjectSoundBuffers(ExtraObjects *self)
{
    self->releaseSounds();
}

void ExtraObjects::releaseSounds()
{
    diag_init();

    for (int k = 0; k < RELEASE_COUNT; k++) {
        ExtraObjectRecord *r = &records_[k];
        /* the handle is re-read after HaltPlayback, exactly as the original */
        if (r->sound != 0) {
            CStatic_HaltPlayback(r->sound);
            soundManager_->releaseStaticForOwner(r->sound, 1);
            r->sound = 0;

            s_released++;
            if (s_rel_diag && !s_logged_release) {
                s_logged_release = 1;
                log_write("extraobjects: first extra-object sound release "
                          "(record %d of %d)\n", k, (int)RELEASE_COUNT);
            }
        }
    }
}

/* ─── ParseExtraObjectEntry 0x423700 and its helpers ────────────────────── */

static const char LEO_DELIMS[] = " ,\t\n;";

static char *leo_tok(void) { return strtok(NULL, LEO_DELIMS); }

/* KAROO_LEO_FX=pathrev: store every spline's points in reverse order, so a
 * path-following object (the Water01 ray) swims its loop backwards.  Only
 * this parser can produce it; the record dump names the reordered points. */
static bool fx_pathrev(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = GetEnvironmentVariableA("KAROO_LEO_FX", buf, sizeof(buf))
                 && lstrcmpiA(buf, "pathrev") == 0;
    }
    return cached != 0;
}

/* 0x424df0: D3DBLEND by name, 0 for anything else (NONE included). */
int ExtraObjects::blendFromName(const char *name)
{
    static const char *const names[] = {
        "ZERO", "ONE", "SRCCOLOR", "INVSRCCOLOR", "SRCALPHA", "INVSRCALPHA",
        "DESTALPHA", "INVDESTALPHA", "DESTCOLOR", "INVDESTCOLOR",
        "SRCALPHASAT", "BOTHSRCALPHA", "BOTHINVSRCALPHA",
    };
    for (int i = 0; i < 13; i++)
        if (strcmp(name, names[i]) == 0)
            return i + 1;
    return 0;
}

/* 0x425250: D3DTEXTUREADDRESS by name, 0 for NULL or anything else. */
unsigned int ExtraObjects::addressFromName(const char *name)
{
    static const char *const names[] = { "WRAP", "MIRROR", "CLAMP", "BORDER" };
    if (name == NULL)
        return 0;
    for (int i = 0; i < 4; i++)
        if (strcmp(name, names[i]) == 0)
            return i + 1;
    return 0;
}

/* 0x425170.  BUG kept: destBlend is not written when srcBlend is 0. */
void ExtraObjects::setBlend(const char *src, const char *dest)
{
    current()->srcBlend = blendFromName(src);
    if (current()->srcBlend != 0)
        current()->destBlend = blendFromName(dest);
}

/* Position then the second vector, six floats; false at the first missing
 * word (after atof has been handed it, as the original does). */
bool ExtraObjects::readSixFloats()
{
    for (int i = 0; i < 6; i++) {
        char *t = leo_tok();
        float v = (float)atof(t);
        if (i < 3) current()->position[i] = v;
        else       current()->field_10c[i - 3] = v;
        if (t == NULL)
            return false;
    }
    return true;
}

/* The spline block, the same in all three kinds.  `mode` is the word that
 * may name it; the caller has already fetched it (or passes NULL). */
void ExtraObjects::parseSpline(const char *mode)
{
    ExtraObjectRecord *r = current();
    r->splineMode = 0;
    if (mode != NULL) {
        if (strcmp(mode, "SPLINE_DYNAMIC") == 0)
            r->splineMode = 1;
        if (strcmp(mode, "SPLINE_STATIC") == 0)
            r->splineMode = 2;
        GameLog_LogMessage(&g_logger, 1, "LEO: Spline-mode:%d", r->splineMode);
    }
    if (r->splineMode == 0)
        return;

    char *more = NULL;
    if (mode != NULL) {
        more = leo_tok();
        r->splineTime = atoi(more);
        GameLog_LogMessage(&g_logger, 1, "LEO: Spline-time:%d", r->splineTime);
    }
    /* Every attempt bumps the count, the failing last one included, and the
     * count is decremented once after.  BUG: unbounded against 0x100. */
    r->splinePointCount = 0;
    while (more != NULL) {
        unsigned n = r->splinePointCount;
        char *t = leo_tok();
        more = NULL;
        if (t != NULL) {
            r->splinePoints[n][0] = (float)atof(t);
            t = leo_tok();
            if (t != NULL) {
                r->splinePoints[n][1] = (float)atof(t);
                more = leo_tok();
                if (more != NULL)
                    r->splinePoints[n][2] = (float)atof(more);
            }
        }
        r->splinePointCount++;
    }
    r->splinePointCount--;
    if (fx_pathrev())
        for (unsigned i = 0, j = r->splinePointCount - 1u; r->splinePointCount && i < j; i++, j--)
            for (int k = 0; k < 3; k++) {
                float tmp = r->splinePoints[i][k];
                r->splinePoints[i][k] = r->splinePoints[j][k];
                r->splinePoints[j][k] = tmp;
            }
    GameLog_LogMessage(&g_logger, 1, "LEO: Spline-points:%d", r->splinePointCount);
}

/* Sound <file> x y z [param] */
void ExtraObjects::parseSound()
{
    GameLog_LogMessage(&g_logger, 1, "LEO: Sound noticed, Index=%d", objectCount_);
    current()->kind = EXTRA_SOUND;
    char *z = NULL;
    char *name = leo_tok();
    sprintf(current()->file, "%s\\%s", g_gameDir, name);
    if (name != NULL) {
        char *t = leo_tok();
        current()->position[0] = (float)atof(t);
        if (t != NULL) {
            t = leo_tok();
            current()->position[1] = (float)atof(t);
            if (t != NULL) {
                z = leo_tok();
                current()->position[2] = (float)atof(z);
            }
        }
    }
    current()->soundParam = 0.0;
    if (z != NULL) {
        char *t = leo_tok();
        if (t != NULL)
            current()->soundParam = atof(t);
    }
}

/* Particle <file> pos v2 <src> <dest> <texture> [spline] */
void ExtraObjects::parseParticle()
{
    GameLog_LogMessage(&g_logger, 1, "LEO: Particle-System noticed, Index=%d", objectCount_);
    current()->kind = EXTRA_PARTICLE;
    char *tex = NULL;
    char *name = leo_tok();
    sprintf(current()->file, "%s\\%s", g_gameDir, name);
    GameLog_LogMessage(&g_logger, 1, "LEO: Particle-Filename:%s", current()->file);
    if (name != NULL && readSixFloats()) {
        char src[0x100];
        strcpy(src, leo_tok());
        char *dest = leo_tok();
        setBlend(src, dest);
        if (dest != NULL) {
            tex = leo_tok();
            sprintf(current()->textureFile, "%s\\%s", g_gameDir, tex);
            GameLog_LogMessage(&g_logger, 1, "LEO: Particle-Texture-Filename:%s",
                               current()->textureFile);
        }
    }
    parseSpline(tex != NULL ? leo_tok() : NULL);
}

/* Model <file> pos v2 [ANI <file>] <src> <dest> <LIT|...> <texture> [address] [spline] */
void ExtraObjects::parseModel()
{
    GameLog_LogMessage(&g_logger, 1, "LEO: Model noticed, Index=%d", objectCount_);
    current()->kind = EXTRA_MODEL;
    char *tex = NULL;
    char *name = leo_tok();
    strcpy(current()->file, name);                  /* no GameDir prefix */
    GameLog_LogMessage(&g_logger, 1, "LEO: Model-Filename:%s", current()->file);
    if (name != NULL && readSixFloats()) {
        char *w = leo_tok();
        bool more = true;
        if (strcmp(w, "ANI") == 0) {
            char *ani = leo_tok();
            strcpy(current()->animationFile, ani);
            GameLog_LogMessage(&g_logger, 1, "LEO: model-animation, filename:%s",
                               current()->animationFile);
            more = (ani != NULL);
            if (more)
                w = leo_tok();
        } else {
            memset(current()->animationFile, 0, sizeof(current()->animationFile));
            GameLog_LogMessage(&g_logger, 1, "LEO: no model-animation");
        }
        if (more && w != NULL) {
            char src[0x100];
            strcpy(src, w);
            char *dest = leo_tok();
            setBlend(src, dest);
            if (dest != NULL) {
                current()->lit = 0;
                w = leo_tok();
                if (strcmp(w, "LIT") == 0)
                    current()->lit = 1;
                GameLog_LogMessage(&g_logger, 1, "LEO: Model lit=%d", current()->lit);
                if (w != NULL) {
                    tex = leo_tok();
                    strcpy(current()->textureFile, tex);
                    GameLog_LogMessage(&g_logger, 1, "LEO: Model-Texture-Filename:%s",
                                       current()->textureFile);
                }
            }
        }
    }
    /* The address word is optional: when it is not one, it is the spline's. */
    current()->textureAddress = 0;
    char *w = NULL;
    if (tex != NULL) {
        w = leo_tok();
        unsigned int a = addressFromName(w);
        if (a == 0) {
            current()->textureAddress = 0;
            GameLog_LogMessage(&g_logger, 1, "LEO: no tex-address");
        } else {
            current()->textureAddress = a;
            GameLog_LogMessage(&g_logger, 1, "LEO: texture-address caption:%s value:%d", w, a);
            w = leo_tok();
        }
    }
    parseSpline(w);
}

/* Billboard pos size <src> <dest> <texture> [spline] */
void ExtraObjects::parseBillboard()
{
    GameLog_LogMessage(&g_logger, 1, "LEO: Billboard noticed, Index=%d", objectCount_);
    current()->kind = EXTRA_BILLBOARD;
    char *tex = NULL;
    bool ok = true;
    for (int i = 0; i < 3 && ok; i++) {
        char *t = leo_tok();
        current()->position[i] = (float)atof(t);
        ok = (t != NULL);
    }
    if (ok) {
        char *t = leo_tok();
        current()->billboardSize = (float)atof(t);
        GameLog_LogMessage(&g_logger, 1, "LEO: Billboard-Size.z=:%f",
                           (double)current()->billboardSize);
        if (t != NULL) {
            char src[0x100];
            strcpy(src, leo_tok());
            char *dest = leo_tok();
            setBlend(src, dest);
            if (dest != NULL) {
                tex = leo_tok();
                strcpy(current()->textureFile, tex);
                GameLog_LogMessage(&g_logger, 1, "LEO: Model-Texture-Filename:%s",
                                   current()->textureFile);
                objectCount_++;        /* BUG: bumped again below; the spline
                                          lands in the next record */
            }
        }
    }
    parseSpline(tex != NULL ? leo_tok() : NULL);
}

/* 0x423700.  Always returns 1; the record is committed by bumping the count,
 * which an entry with no kind word never reaches. */
int ExtraObjects::parseEntry(const char *entry)
{
    char buf[1000];                 /* as the original; shipped max is 270 */
    strcpy(buf, entry);
    for (char *w = strtok(buf, LEO_DELIMS); w != NULL; w = leo_tok()) {
        if (strcmp(w, "Sound") == 0)          parseSound();
        else if (strcmp(w, "Particle") == 0)  parseParticle();
        else if (strcmp(w, "Model") == 0) {
            if (fx_nomodels())
                return 1;               /* KAROO_LEO_FX=nomodels: no record */
            parseModel();
        }
        else if (strcmp(w, "Billboard") == 0) parseBillboard();
        else continue;
        objectCount_++;
        return 1;
    }
    return 1;
}

/* ─── Construction and teardown: 0x423560, 0x4235e0, 0x4235c0 ──────────────
 *
 * ExtraObjects is embedded in Game (+0x48b98): Game::Load constructs it and
 * Game::Destruct destroys it; two SEH funclets also jump to the dtor.  The
 * scalar dtor is vtable slot 0 only, and nothing deletes an embedded member,
 * so its free is reimplemented but unreached -- on the game heap, the
 * LinkedList precedent. */
static void *const g_LeoVtable[1] = { (void *)&Leo_ScalarDestructor };

void ExtraObjects::construct()
{
    /* The EH-vector init of every record's splinePoints runs 0x424de0 on
     * each point, which does nothing: omitted. */
    vtable_ = g_LeoVtable;
    objectCount_ = 0;
    for (int i = 0; i < RELEASE_COUNT; ++i)     /* 255: record 255 keeps its */
        records_[i].sound = NULL;
}

void ExtraObjects::destruct()
{
    vtable_ = g_LeoVtable;
}

extern "C" __declspec(dllexport) ExtraObjects *__attribute__((thiscall))
Leo_Construct(ExtraObjects *self)
{
    self->construct();
    return self;
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Leo_Destruct(ExtraObjects *self)
{
    self->destruct();
}

extern "C" __declspec(dllexport) ExtraObjects *__attribute__((thiscall))
Leo_ScalarDestructor(ExtraObjects *self, unsigned char flags)
{
    self->destruct();
    if (flags & 1)
        free(self);
    return self;
}
