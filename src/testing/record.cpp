/* FORMAT: a recording, little-endian, decoded by tools/replay.py.
 *   header (80 bytes)
 *     char   magic[4]      "KROO"
 *     u32    version       RECORD_VERSION
 *     double fixed_dt      KAROO_FIXED_DT at record time (0 = real clock)
 *     u32    seed          KAROO_SEED at record time
 *     u32    flags         bit 0: the seed was set
 *     char   label[56]     KAROO_RECORD_LABEL, NUL-padded
 *   then one record per frame that polled input:
 *     u32    frame_index
 *     u8     game_state
 *     u8     keys[256]
 *     u8     async_count
 *            async_count x { u8 vkey; u8 down }
 *
 * `down` is the sign bit of GetAsyncKeyState's answer, the only bit the game
 * tests.  The pressed-since-last-call bit is not replayed: it is consumed
 * state, and reproducing it would need the same call order within a frame. */

#include "portable.h"
#include <stdint.h>
#include <fstream>
#include "record.h"
#include "sysdev.h"
#include <stdio.h>
#include "inputdev.h"
#include "policy.h"
#include "menu.h"
#include "levelreport.h"
#include "clock.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <iterator>

#define RECORD_VERSION  1
#define ASYNC_MAX       64
#define HEADER_SIZE     80

struct FrameRec {
    uint32_t frame;
    uint8_t  game_state;
    uint8_t  keys[256];
    uint8_t  async_count;
    uint8_t  async[ASYNC_MAX][2];  // vkey, down
};

static int      g_mode = -1;  // 0 none, 1 record, 2 replay
static std::ofstream g_out;  // record
static std::ifstream g_in;   // replay
static FrameRec g_cur;   // the frame being accumulated (record)
static FrameRec g_play;  // the frame being served (replay)
static bool     g_have_play;
static FrameRec g_pending;  // read ahead, not yet due
static bool     g_have_pending;
static bool     g_finished;
static uint8_t     g_async_used[ASYNC_MAX];
static bool     g_keys_seen;

/* KAROO_INPUT_DEBUG=1 logs the first few replayed frames and key polls. */
static int      g_dbg = -1;
static bool input_debug(void)
{
    if (g_dbg < 0) {
        char b[8];
        g_dbg = (sysdev::getEnv("KAROO_INPUT_DEBUG", b, sizeof(b)) && b[0] && b[0] != '0');
    }
    return g_dbg > 0;
}

static double   g_dt;
static uint32_t    g_seed;
static bool     g_seed_set;

static void read_env(void)
{
    char buf[64];
    g_dt = 0.0;
    if (sysdev::getEnv("KAROO_FIXED_DT", buf, sizeof(buf)) && buf[0])
        g_dt = atof(buf);
    g_seed = 0; g_seed_set = false;
    if (sysdev::getEnv("KAROO_SEED", buf, sizeof(buf)) && buf[0]) {
        g_seed = (uint32_t)atoi(buf);
        g_seed_set = true;
    }
}

static void warn_determinism(const char *what)
{
    if (g_dt <= 0.0)
        g_logger.write("record: WARNING — %s without KAROO_FIXED_DT; frames will not "
                  "line up (REPLAY_PLAN.md Stage A)\n", what);
    if (!g_seed_set)
        g_logger.write("record: WARNING — %s without KAROO_SEED; the RNG tables will "
                  "differ (REPLAY_PLAN.md Stage A2)\n", what);
}

/* The file is little-endian whatever the host. */
static void put_u32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t get_u32(const uint8_t *p)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)p[i] << (8 * i);
    return v;
}

static void put_f64(uint8_t *p, double d)
{
    uint64_t bits;
    memcpy(&bits, &d, 8);
    put_u32(p, (uint32_t)bits);
    put_u32(p + 4, (uint32_t)(bits >> 32));
}

static double get_f64(const uint8_t *p)
{
    uint64_t bits = (uint64_t)get_u32(p) | ((uint64_t)get_u32(p + 4) << 32);
    double d;
    memcpy(&d, &bits, 8);
    return d;
}

static void open_record(const char *path)
{
    g_out.open(path, std::ios::binary);
    if (!g_out) {
        g_logger.write("record: cannot open %s for writing\n", path);
        g_mode = 0;
        return;
    }
    uint8_t hdr[HEADER_SIZE] = {};
    std::copy_n("KROO", 4, hdr);
    put_u32(hdr + 4, RECORD_VERSION);
    put_f64(hdr + 8, g_dt);
    put_u32(hdr + 16, g_seed);
    put_u32(hdr + 20, g_seed_set ? 1u : 0u);
    sysdev::getEnv("KAROO_RECORD_LABEL", (char *)hdr + 24, 56);
    g_out.write((const char *)hdr, sizeof(hdr));
    g_out.flush();
    g_logger.write("record: recording to %s (dt=%.9f seed=%u%s)\n",
              path, g_dt, (unsigned)g_seed, g_seed_set ? "" : " UNSET");
    warn_determinism("recording");
}

static void open_replay(const char *path)
{
    g_in.open(path, std::ios::binary);
    if (!g_in) {
        g_logger.write("record: cannot open %s for reading\n", path);
        g_mode = 0;
        return;
    }
    uint8_t hdr[HEADER_SIZE];
    if (!g_in.read((char *)hdr, sizeof(hdr)) ||
        memcmp(hdr, "KROO", 4) != 0) {
        g_logger.write("record: %s is not a recording\n", path);
        g_mode = 0;
        return;
    }
    uint32_t  ver  = get_u32(hdr + 4);
    double rdt  = get_f64(hdr + 8);
    uint32_t  seed = get_u32(hdr + 16);
    if (ver != RECORD_VERSION) {
        g_logger.write("record: %s is version %lu, this build reads %d\n",
                  path, (unsigned long)ver, RECORD_VERSION);
        g_mode = 0;
        return;
    }
    g_logger.write("record: replaying %s (recorded dt=%.9f seed=%u label='%s')\n",
              path, rdt, (unsigned)seed, (const char *)hdr + 24);
    // A replay under different determinism settings will diverge; say so, so
    // it does not look like a real mismatch.
    if (rdt != g_dt)
        g_logger.write("record: WARNING — replaying at dt=%.9f but recorded at %.9f\n",
                  g_dt, rdt);
    if (!g_seed_set || seed != g_seed)
        g_logger.write("record: WARNING — replaying with seed=%u%s but recorded with %u\n",
                  (unsigned)g_seed, g_seed_set ? "" : " (unset)", (unsigned)seed);
}

static void init(void)
{
    if (g_mode >= 0) return;
    char path[kMaxPath];
    g_mode = 0;
    read_env();

    if (sysdev::getEnv("KAROO_RECORD", path, sizeof(path)) && path[0]) {
        g_mode = 1;
        open_record(path);
    } else if (sysdev::getEnv("KAROO_REPLAY", path, sizeof(path)) && path[0]) {
        g_mode = 2;
        open_replay(path);
    }
}

bool record_recording(void)      { init(); return g_mode == 1; }
bool record_replaying(void)      { init(); return g_mode == 2; }
bool record_replay_finished(void){ return g_finished; }

static void flush_frame(void)
{
    if (!g_keys_seen) return;  // nothing polled input this frame
    uint8_t buf[4 + 1 + 256 + 1 + ASYNC_MAX * 2];
    int  n = 0;
    put_u32(buf + n, g_cur.frame);             n += 4;
    buf[n++] = g_cur.game_state;
    std::copy_n(g_cur.keys, 256, buf + n);       n += 256;
    buf[n++] = g_cur.async_count;
    for (int i = 0; i < g_cur.async_count; i++) {
        buf[n++] = g_cur.async[i][0];
        buf[n++] = g_cur.async[i][1];
    }
    // Flushed every frame: the process can end without the stream closing the file.
    g_out.write((const char *)buf, n);
    g_out.flush();

    g_cur = FrameRec();
    g_keys_seen = false;
}

void record_keys(unsigned short game_state, const uint8_t *keys)
{
    if (!record_recording()) return;
    g_cur.frame      = clock_frame();
    g_cur.game_state = (uint8_t)game_state;
    std::copy_n(keys, 256, g_cur.keys);
    g_keys_seen = true;
}

void record_async(int vkey, short value)
{
    if (!record_recording()) return;
    if (g_cur.async_count >= ASYNC_MAX) return;
    g_cur.frame = clock_frame();
    g_cur.async[g_cur.async_count][0] = (uint8_t)(vkey & 0xff);
    g_cur.async[g_cur.async_count][1] = (value & 0x8000) ? 1 : 0;
    g_cur.async_count++;
    g_keys_seen = true;  // a frame with only key polls is still a frame
}

static bool read_exact(void *dst, uint32_t n)
{
    return (bool)g_in.read((char *)dst, n);
}

static bool read_one(FrameRec *r)
{
    *r = FrameRec();
    uint8_t fbytes[4];
    if (!read_exact(fbytes, 4) || !read_exact(&r->game_state, 1) ||
        !read_exact(r->keys, 256)  || !read_exact(&r->async_count, 1))
        return false;
    r->frame = get_u32(fbytes);
    if (r->async_count > ASYNC_MAX) return false;
    for (int i = 0; i < r->async_count; i++)
        if (!read_exact(r->async[i], 2)) return false;
    return true;
}

/* Records are served by frame index, not in sequence.  A frame in which the
 * game polled no input writes no record, so consuming records in order would
 * shift every later frame.  A record applies only on the frame it was recorded
 * on; a frame with none gets no input, as happened.  A record whose frame has
 * already passed is consumed rather than stalling the replay. */
static void load_frame(void)
{
    g_have_play = false;
    unsigned now = clock_frame();

    if (!g_have_pending && !g_finished) {
        if (read_one(&g_pending)) {
            g_have_pending = true;
        } else {
            g_finished = true;
            g_logger.write("record: replay finished at frame %u\n", now);
        }
    }

    if (g_have_pending && g_pending.frame <= now) {
        g_play = g_pending;
        g_have_pending = false;
        g_have_play = true;
        std::fill(std::begin(g_async_used), std::end(g_async_used), 0);
    }
}

bool replay_keys(unsigned short *game_state, uint8_t *keys)
{
    if (!record_replaying()) return false;
    static int served = 0, empty = 0;
    if (!g_have_play) {
        if (input_debug() && ++empty <= 5)
            g_logger.write("replaydbg: frame %u — no record for this frame\n", clock_frame());
        return false;
    }
    if (input_debug()) {
        int held = -1;
        for (int i = 0; i < 256; i++) if (g_play.keys[i] & 0x80) { held = i; break; }
        if (held >= 0 && ++served <= 12)
            g_logger.write("replaydbg: frame %u serving rec-frame %u state=%u scancode %d\n",
                      clock_frame(), g_play.frame, g_play.game_state, held);
    }
    *game_state = g_play.game_state;
    std::copy_n(g_play.keys, 256, keys);
    return true;
}

bool replay_async(int vkey, short *value)
{
    if (!record_replaying()) return false;
    if (!g_have_play) { *value = 0; return true; }
    for (int i = 0; i < g_play.async_count; i++) {
        if (g_async_used[i]) continue;
        if (g_play.async[i][0] != (uint8_t)(vkey & 0xff)) continue;
        g_async_used[i] = 1;
        *value = g_play.async[i][1] ? (short)0x8000 : (short)0;
        return true;
    }
    *value = 0;  // not polled in the recording on this frame
    return true;
}

void record_frame_boundary(void)
{
    init();
    if (g_mode == 1)      flush_frame();
    else if (g_mode == 2) load_frame();
}

 

  short hooks_GetAsyncKeyState(int vKey)
{
    if (input_debug()) {  // which call sites actually execute
        static int n = 0;
        if (n < 40) {
            n++;
            g_logger.write("askdbg: frame %u vkey=0x%02X from ret=%p\n",
                      clock_frame(), vKey, __builtin_return_address(0));
        }
    }
    // The menu driver answers first: it is synthesising an edge the menu's
    // debounce depends on, which neither a recording nor the keyboard may
    // contradict.
    short mv;
    if (menu_async_override(vKey, &mv)) return mv;

    // The level-report trigger comes next: it fires before the first frame
    // boundary, so no recorded frame could answer it.
    if (levelreport_async_override(vKey, &mv)) return mv;

    short v;
    if (record_replaying() && !policy_in_control(clock_frame())) {
        replay_async(vKey, &v);
        return v;
    }
    // Under autoplay the recording's answers must not reach the game (the
    // prefix recording ends by quitting), but the real keyboard is still read,
    // so whoever is watching can still press Escape.
    v = inputdev::asyncKeyState(vKey);
    record_async(vKey, v);
    return v;
}
