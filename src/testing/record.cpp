/* Input recording / replay (REPLAY_PLAN.md Stages C and D).
 *
 * Format — little-endian, written with WriteFile, decoded by tools/replay.py:
 *
 *   header (80 bytes)
 *     char   magic[4]      "KROO"
 *     u32    version       RECORD_VERSION
 *     double fixed_dt      KAROO_FIXED_DT at record time (0 = real clock)
 *     u32    seed          KAROO_SEED at record time
 *     u32    flags         bit 0: seed was set
 *     char   label[56]     free text from KAROO_RECORD_LABEL, NUL-padded
 *   then one frame record, repeated:
 *     u32    frame_index
 *     u8     game_state
 *     u8     keys[256]
 *     u8     async_count
 *            async_count x { u8 vkey; u8 down }
 *
 * This differs from the plan in one place: the plan's header carries the level
 * name and the --seed-from slot.  Neither is reliably readable from the Game
 * object yet (the level-name field is not confirmed), so rather than write a
 * guess into a file format, the header carries a free-text label the caller
 * supplies via KAROO_RECORD_LABEL.  tools/replaytest.py can put the level there
 * because it is the thing that seeded the slot in the first place.
 *
 * `down` is stored as the sign bit of GetAsyncKeyState's return (0x8000),
 * which is the only bit the game's 16 call sites test.  The low "was pressed
 * since last call" bit is deliberately not replayed: it is consumed state, and
 * reproducing it from a recording would need the same call ordering, which the
 * frame boundary does not guarantee.
 */
#include "record.h"
#include "clock.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

#define RECORD_VERSION  1
#define ASYNC_MAX       64
#define HEADER_SIZE     80

struct FrameRec {
    DWORD frame;
    BYTE  game_state;
    BYTE  keys[256];
    BYTE  async_count;
    BYTE  async[ASYNC_MAX][2];   /* vkey, down */
};

static int      g_mode = -1;     /* 0 none, 1 record, 2 replay */
static HANDLE   g_fh   = INVALID_HANDLE_VALUE;
static FrameRec g_cur;           /* frame being accumulated (record) */
static FrameRec g_play;          /* frame being served (replay) */
static bool     g_have_play;
static bool     g_finished;
static BYTE     g_async_used[ASYNC_MAX];
static bool     g_keys_seen;

static double   g_dt;
static DWORD    g_seed;
static bool     g_seed_set;

static void read_env(void)
{
    char buf[64];
    g_dt = 0.0;
    if (GetEnvironmentVariableA("KAROO_FIXED_DT", buf, sizeof(buf)) && buf[0])
        g_dt = atof(buf);
    g_seed = 0; g_seed_set = false;
    if (GetEnvironmentVariableA("KAROO_SEED", buf, sizeof(buf)) && buf[0]) {
        g_seed = (DWORD)atoi(buf);
        g_seed_set = true;
    }
}

static void warn_determinism(const char *what)
{
    if (g_dt <= 0.0)
        log_write("record: WARNING — %s without KAROO_FIXED_DT; frames will not "
                  "line up (REPLAY_PLAN.md Stage A)\n", what);
    if (!g_seed_set)
        log_write("record: WARNING — %s without KAROO_SEED; the RNG tables will "
                  "differ (REPLAY_PLAN.md Stage A2)\n", what);
}

static void open_record(const char *path)
{
    g_fh = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_fh == INVALID_HANDLE_VALUE) {
        log_write("record: cannot open %s for writing\n", path);
        g_mode = 0;
        return;
    }
    BYTE hdr[HEADER_SIZE];
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr, "KROO", 4);
    *(DWORD  *)(hdr + 4)  = RECORD_VERSION;
    *(double *)(hdr + 8)  = g_dt;
    *(DWORD  *)(hdr + 16) = g_seed;
    *(DWORD  *)(hdr + 20) = g_seed_set ? 1u : 0u;
    GetEnvironmentVariableA("KAROO_RECORD_LABEL", (char *)hdr + 24, 56);
    DWORD w = 0;
    WriteFile(g_fh, hdr, sizeof(hdr), &w, NULL);
    log_write("record: recording to %s (dt=%.9f seed=%u%s)\n",
              path, g_dt, (unsigned)g_seed, g_seed_set ? "" : " UNSET");
    warn_determinism("recording");
}

static void open_replay(const char *path)
{
    g_fh = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_fh == INVALID_HANDLE_VALUE) {
        log_write("record: cannot open %s for reading\n", path);
        g_mode = 0;
        return;
    }
    BYTE hdr[HEADER_SIZE];
    DWORD got = 0;
    if (!ReadFile(g_fh, hdr, sizeof(hdr), &got, NULL) || got != sizeof(hdr) ||
        memcmp(hdr, "KROO", 4) != 0) {
        log_write("record: %s is not a recording\n", path);
        g_mode = 0;
        return;
    }
    DWORD  ver  = *(DWORD  *)(hdr + 4);
    double rdt  = *(double *)(hdr + 8);
    DWORD  seed = *(DWORD  *)(hdr + 16);
    if (ver != RECORD_VERSION) {
        log_write("record: %s is version %lu, this build reads %d\n",
                  path, (unsigned long)ver, RECORD_VERSION);
        g_mode = 0;
        return;
    }
    log_write("record: replaying %s (recorded dt=%.9f seed=%u label='%s')\n",
              path, rdt, (unsigned)seed, (const char *)hdr + 24);
    /* A replay under different determinism settings than the recording will
     * diverge; say so rather than let it look like a real mismatch. */
    if (rdt != g_dt)
        log_write("record: WARNING — replaying at dt=%.9f but recorded at %.9f\n",
                  g_dt, rdt);
    if (!g_seed_set || seed != g_seed)
        log_write("record: WARNING — replaying with seed=%u%s but recorded with %u\n",
                  (unsigned)g_seed, g_seed_set ? "" : " (unset)", (unsigned)seed);
}

static void init(void)
{
    if (g_mode >= 0) return;
    char path[MAX_PATH];
    g_mode = 0;
    read_env();

    if (GetEnvironmentVariableA("KAROO_RECORD", path, sizeof(path)) && path[0]) {
        g_mode = 1;
        open_record(path);
    } else if (GetEnvironmentVariableA("KAROO_REPLAY", path, sizeof(path)) && path[0]) {
        g_mode = 2;
        open_replay(path);
    }
}

bool record_recording(void)      { init(); return g_mode == 1; }
bool record_replaying(void)      { init(); return g_mode == 2; }
bool record_replay_finished(void){ return g_finished; }

/* ── recording ─────────────────────────────────────────────────────────── */

static void flush_frame(void)
{
    if (!g_keys_seen) return;         /* nothing ticked this frame */
    BYTE buf[4 + 1 + 256 + 1 + ASYNC_MAX * 2];
    int  n = 0;
    *(DWORD *)(buf + n) = g_cur.frame;      n += 4;
    buf[n++] = g_cur.game_state;
    memcpy(buf + n, g_cur.keys, 256);       n += 256;
    buf[n++] = g_cur.async_count;
    for (int i = 0; i < g_cur.async_count; i++) {
        buf[n++] = g_cur.async[i][0];
        buf[n++] = g_cur.async[i][1];
    }
    DWORD w = 0;
    WriteFile(g_fh, buf, n, &w, NULL);

    memset(&g_cur, 0, sizeof(g_cur));
    g_keys_seen = false;
}

void record_keys(unsigned short game_state, const BYTE *keys)
{
    if (!record_recording()) return;
    g_cur.frame      = clock_frame();
    g_cur.game_state = (BYTE)game_state;
    memcpy(g_cur.keys, keys, 256);
    g_keys_seen = true;
}

void record_async(int vkey, SHORT value)
{
    if (!record_recording()) return;
    if (g_cur.async_count >= ASYNC_MAX) return;
    g_cur.frame = clock_frame();
    g_cur.async[g_cur.async_count][0] = (BYTE)(vkey & 0xff);
    g_cur.async[g_cur.async_count][1] = (value & 0x8000) ? 1 : 0;
    g_cur.async_count++;
    g_keys_seen = true;               /* async-only frames are still frames */
}

/* ── replay ────────────────────────────────────────────────────────────── */

static bool read_exact(void *dst, DWORD n)
{
    DWORD got = 0;
    return ReadFile(g_fh, dst, n, &got, NULL) && got == n;
}

static void load_frame(void)
{
    g_have_play = false;
    if (g_finished) return;

    FrameRec r;
    memset(&r, 0, sizeof(r));
    if (!read_exact(&r.frame, 4) || !read_exact(&r.game_state, 1) ||
        !read_exact(r.keys, 256)  || !read_exact(&r.async_count, 1)) {
        g_finished = true;
        log_write("record: replay finished at frame %u\n", clock_frame());
        return;
    }
    if (r.async_count > ASYNC_MAX) { g_finished = true; return; }
    for (int i = 0; i < r.async_count; i++)
        if (!read_exact(r.async[i], 2)) { g_finished = true; return; }

    g_play = r;
    g_have_play = true;
    memset(g_async_used, 0, sizeof(g_async_used));
}

bool replay_keys(unsigned short *game_state, BYTE *keys)
{
    if (!record_replaying() || !g_have_play) return false;
    *game_state = g_play.game_state;
    memcpy(keys, g_play.keys, 256);
    return true;
}

bool replay_async(int vkey, SHORT *value)
{
    if (!record_replaying()) return false;
    if (!g_have_play) { *value = 0; return true; }
    for (int i = 0; i < g_play.async_count; i++) {
        if (g_async_used[i]) continue;
        if (g_play.async[i][0] != (BYTE)(vkey & 0xff)) continue;
        g_async_used[i] = 1;
        *value = g_play.async[i][1] ? (SHORT)0x8000 : (SHORT)0;
        return true;
    }
    *value = 0;   /* not queried in the recording at this frame */
    return true;
}

void record_frame_boundary(void)
{
    init();
    if (g_mode == 1)      flush_frame();
    else if (g_mode == 2) load_frame();
}

/* ── GetAsyncKeyState interception ─────────────────────────────────────── */

extern "C" {

/* Replaces the GetAsyncKeyState IAT slot (0x0045D1B0, 16 call sites). */
__declspec(dllexport) SHORT WINAPI hooks_GetAsyncKeyState(int vKey)
{
    SHORT v;
    if (record_replaying()) {
        replay_async(vKey, &v);
        return v;
    }
    v = GetAsyncKeyState(vKey);
    record_async(vKey, v);
    return v;
}

} // extern "C"
