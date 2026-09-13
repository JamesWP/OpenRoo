/* Per-frame state checksum (REPLAY_PLAN.md Stage A2).
 *
 * The fixed timestep from Stage A is only useful if it actually makes the
 * simulation reproducible.  This hashes the state that the simulation advances
 * each frame and writes one line per frame, so two runs can be diffed byte for
 * byte.  A difference means real time is still reaching the simulation from
 * somewhere other than 0x00404040.
 *
 * What gets hashed, and why those things:
 *
 *   - Particle rings.  Every live ParticleNode's position, velocity, life and
 *     diffuse.  These are integrated against dt by our own generator and
 *     environment ticks, so they are the most dt-sensitive state in the game
 *     and — unlike the player — they are live on the main menu, which is the
 *     only scene `launch.sh --skip-launcher` currently reaches.
 *   - Game fields, when GameGlobal (0x0046c498) is non-null.  These are the
 *     REPLAY_PLAN.md assertion surface.  Stage A2 only needs them to be
 *     *stable*, not correctly interpreted — the gem/score reading is still
 *     unconfirmed and is Stage B's job — so they are hashed as raw bytes and
 *     logged under neutral names.
 *
 * Floats are hashed as raw bytes on purpose: bit-exact is the property we are
 * testing, and a tolerance would hide exactly the drift we are looking for.
 *
 * Ordering is part of the hash.  Systems are folded in the order the game ticks
 * them and nodes in ring order, both of which are deterministic within a run;
 * if scene construction order ever varied, that would itself be a determinism
 * bug worth catching here.
 */
#include "determinism.h"
#include "particles.h"
#include "log.h"
#include "game.h"
#include <stdio.h>


/* Assertion-surface offsets.  Widths are from the CalculateLevelScore
 * (0x0041A760) decompile — +0x04224D and +0x170A64 are bytes, not dwords, as
 * the plan's table implied.  Meanings are still unconfirmed in game (Stage B),
 * so these are hashed as opaque bytes and logged under neutral names: a wrong
 * label must not be able to mislead a determinism result.
 *
 * Five of them are Player fields (player.h: +0x25, +0x23d, +0x22c, +0x11f,
 * +0xef).  They stay as raw Game offsets deliberately: this table hashes
 * byte ranges, and routing it through named accessors would reintroduce
 * exactly the labels it is built to avoid. */
static const struct { DWORD off; DWORD len; const char *tag; } GAME_FIELDS[] = {
    { 0x1751ee, 12, "pos"     },  /* three floats — player position (unconfirmed) */
    { 0x175406,  4, "f175406" },  /* gems collected (unconfirmed) */
    { 0x04224d,  1, "f04224d" },  /* foes killed — byte */
    { 0x2ab595,  4, "f2ab595" },  /* elapsed ms */
    { 0x1753f5,  4, "f1753f5" },  /* running score */
    { 0x170a64,  1, "f170a64" },  /* health — byte */
    { 0x1752e8,  4, "f1752e8" },  /* death / time-out flag (unconfirmed) */
    { 0x1752b8,  4, "f1752b8" },  /* level-complete flag (unconfirmed) */
};

static int      g_on = -1;
static HANDLE   g_fh = INVALID_HANDLE_VALUE;
static DWORD    g_frame;
static DWORD    g_hash = 2166136261u;   /* FNV-1a offset basis */
/* Sub-hashes, so a divergence names the field it came from rather than just
 * the frame.  pos / vel / life / diffuse, in that order. */
#define SUB_N 4
static const char *SUB_TAG[SUB_N] = { "pos", "vel", "life", "diff" };
static DWORD    g_sub[SUB_N] = { 2166136261u, 2166136261u, 2166136261u, 2166136261u };
static DWORD    g_nodes;
static DWORD    g_systems;

static void fold_into(DWORD *h, const void *p, size_t n)
{
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) {
        *h ^= b[i];
        *h *= 16777619u;
    }
}

static void fold(const void *p, size_t n) { fold_into(&g_hash, p, n); }

static void fold_sub(int sub, const void *p, size_t n)
{
    fold(p, n);
    fold_into(&g_sub[sub], p, n);
}

bool dethash_enabled(void)
{
    if (g_on < 0) {
        char path[MAX_PATH];
        g_on = 0;
        if (GetEnvironmentVariableA("KAROO_HASH_LOG", path, sizeof(path)) && path[0]) {
            g_fh = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            g_on = (g_fh != INVALID_HANDLE_VALUE);
            log_write("dethash: %s -> %s\n", path, g_on ? "recording" : "OPEN FAILED");
        }
    }
    return g_on > 0;
}

void dethash_particles(ParticleSystem *ps)
{
    if (!dethash_enabled() || !ps) return;

    g_systems++;
    /* Ring walk exactly as particles.cpp does it: empty when head == current,
     * otherwise head..current exclusive. */
    ParticleNode *n = ps->ring.pRingHead;
    for (DWORD guard = 0; n && n != ps->ring.pRingCurrent && guard <= ps->ring.dwRingCount;
         n = n->pNext, guard++) {
        fold_sub(0, &n->flX,       3 * sizeof(float));
        fold_sub(1, n->flVel,      3 * sizeof(float));
        fold_sub(2, &n->flLife,    sizeof(float));
        fold_sub(3, &n->dwDiffuse, sizeof(DWORD));
        g_nodes++;
    }
}

void dethash_frame_end(double virtual_seconds)
{
    if (!dethash_enabled()) return;

    char fields[256];
    int  fl = 0;
    const unsigned char *game = (const unsigned char *)Game::instance();
    if (game) {
        for (size_t i = 0; i < sizeof(GAME_FIELDS) / sizeof(GAME_FIELDS[0]); i++) {
            fold(game + GAME_FIELDS[i].off, GAME_FIELDS[i].len);
            if (fl >= (int)sizeof(fields) - 32) continue;
            if (GAME_FIELDS[i].len == 4)
                fl += snprintf(fields + fl, sizeof(fields) - fl, " %s=%08lx",
                               GAME_FIELDS[i].tag,
                               (unsigned long)*(const DWORD *)(game + GAME_FIELDS[i].off));
            else if (GAME_FIELDS[i].len == 1)
                fl += snprintf(fields + fl, sizeof(fields) - fl, " %s=%02x",
                               GAME_FIELDS[i].tag,
                               (unsigned)*(const BYTE *)(game + GAME_FIELDS[i].off));
        }
    }

    char subs[128];
    int  sl = 0;
    for (int i = 0; i < SUB_N; i++)
        sl += snprintf(subs + sl, sizeof(subs) - sl, " %s=%08lx",
                       SUB_TAG[i], (unsigned long)g_sub[i]);

    char line[640];
    int  n = snprintf(line, sizeof(line),
                      "%06lu t=%.6f hash=%08lx sys=%lu nodes=%lu game=%d%s%s\n",
                      (unsigned long)g_frame, virtual_seconds,
                      (unsigned long)g_hash, (unsigned long)g_systems,
                      (unsigned long)g_nodes, game ? 1 : 0, subs, fields);
    DWORD written = 0;
    if (n > 0) WriteFile(g_fh, line, (DWORD)n, &written, NULL);

    g_frame++;
    g_hash    = 2166136261u;   /* per-frame hash, not cumulative — a diff then
                                * points at the first frame that diverged */
    g_nodes   = 0;
    g_systems = 0;
    for (int i = 0; i < SUB_N; i++) g_sub[i] = 2166136261u;
}
