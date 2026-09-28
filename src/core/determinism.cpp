/* The determinism hash.  Each frame, the state the simulation advances is
 * folded into an FNV-1a hash, and one line per frame goes to KAROO_HASH_LOG,
 * so that two runs can be compared byte for byte.
 *
 * Two things are hashed:
 *   - every live particle's position, velocity, life and colour.  They are
 *     integrated against dt, so they are the state most sensitive to timing,
 *     and they are live on the menus as well as in a level;
 *   - a few Game fields, while a Game exists, as raw bytes.
 *
 * Floats are hashed as their bytes: bit-exactness is the property under test,
 * and a tolerance would hide the drift it looks for.  Order is part of the
 * hash: systems in the order the game ticks them, nodes in ring order. */

#include "determinism.h"
#include "particles.h"
#include "log.h"
#include "game.h"
#include <stdio.h>

/* The Game fields hashed, by offset and width.  They are logged under their
 * offsets rather than their meanings, so a wrong label cannot mislead a
 * determinism result; five are Player fields, reached through the Game. */
static const struct { DWORD off; DWORD len; const char *tag; } GAME_FIELDS[] = {
    { 0x1751ee, 12, "pos"     },  // three floats: the player's position
    { 0x175406,  4, "f175406" },  // gems collected
    { 0x04224d,  1, "f04224d" },  // foes killed, a byte
    { 0x2ab595,  4, "f2ab595" },  // elapsed ms
    { 0x1753f5,  4, "f1753f5" },  // running score
    { 0x170a64,  1, "f170a64" },  // vitality, a byte
    { 0x1752e8,  4, "f1752e8" },  // the move state and the low three bytes of falling
    { 0x1752b8,  4, "f1752b8" },  // level-complete flag
};

static int      g_on = -1;
static HANDLE   g_fh = INVALID_HANDLE_VALUE;
static DWORD    g_frame;
static DWORD    g_hash = 2166136261u;  // the FNV-1a offset basis

/* Per-quantity hashes, so a divergence names what diverged: position,
 * velocity, life, colour. */
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
    // The ring as particles.cpp walks it: head up to, not including, current.
    ParticleNode *n = ps->ring().pRingHead;
    for (DWORD guard = 0; n && n != ps->ring().pRingCurrent && guard <= ps->ring().dwRingCount;
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
    g_hash    = 2166136261u;  // per frame, so a diff points at the first frame that differs
    g_nodes   = 0;
    g_systems = 0;
    for (int i = 0; i < SUB_N; i++) g_sub[i] = 2166136261u;
}
