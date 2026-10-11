/* FORMAT: a recording, little-endian, decoded by tools/replay.py.
 *   header
 *     char   magic[4]      "KROO"
 *     u8     version       RECORD_VERSION
 *     u8     flags         bit 0: the seed was set
 *     double fixed_dt      KAROO_FIXED_DT at record time (0 = real clock)
 *     u32    seed          KAROO_SEED at record time
 *     u8     label_len
 *     char   label[label_len]   KAROO_RECORD_LABEL
 *     uvarint file_count   the save files (SavedGames/) the recording starts from;
 *                          tools/replaytest.py restores them before the run, the
 *                          game ignores them
 *             file_count x { uvarint name_len; char name[]; uvarint size; u8 data[size] }
 *   then one record per tick that took the keyboard (input::update):
 *     uvarint frame_delta  frames since the previous record (the first counts from 0)
 *     u8      flags        bit 0: keys changed
 *     uvarint key_count    if bit 0
 *             key_count x { uvarint scancode_delta; u8 value }
 *                          changes against the previous record's key array
 *                          (inputdev::Key scancodes, 0x80 while held),
 *                          scancode_delta from the previous entry (the first from 0)
 *   uvarint is LEB128.  The key array starts at zero, and holds through a
 *   frame with no record. */

#include <stdio.h>
#include <stdint.h>
#include <fstream>
#include <string>
#include "record.h"
#include "sysdev.h"
#include "inputdev.h"
#include "clock.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <iterator>

#define RECORD_VERSION  4

struct FrameRec {
    uint32_t frame;
    uint8_t  keys[inputdev::KEY_COUNT];
};

static int      g_mode = -1;  // 0 none, 1 record, 2 replay
static std::ofstream g_out;  // record
static std::ifstream g_in;   // replay
static FrameRec g_cur;   // the frame being accumulated (record)
static FrameRec g_play;  // the keys being served: the last record due (replay)
static FrameRec g_pending;  // read ahead, not yet due
static bool     g_have_pending;
static bool     g_finished;
static bool     g_keys_seen;
// Running state the per-frame deltas are against: the previous record's.
struct DeltaState {
    uint32_t frame = 0;
    uint8_t  keys[inputdev::KEY_COUNT] = {};
};
static DeltaState g_wr;  // record
static DeltaState g_rd;  // replay

/* KAROO_INPUT_DEBUG=1 logs the first few replayed key records. */
static int      g_dbg = -1;
bool input_debug(void)
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
    g_out.open(sysdev::nativePath(path), std::ios::binary);
    if (!g_out) {
        g_logger.write("record: cannot open %s for writing\n", path);
        g_mode = 0;
        return;
    }
    char label[256] = {};
    sysdev::getEnv("KAROO_RECORD_LABEL", label, sizeof(label));
    uint8_t hdr[19];
    std::copy_n("KROO", 4, hdr);
    hdr[4] = RECORD_VERSION;
    hdr[5] = g_seed_set ? 1 : 0;
    put_f64(hdr + 6, g_dt);
    put_u32(hdr + 14, g_seed);
    hdr[18] = (uint8_t)strlen(label);
    g_out.write((const char *)hdr, sizeof(hdr));
    g_out.write(label, hdr[18]);
    g_out.put(0);  // no bundled saves; tools/replaytest.py adds them
    g_out.flush();
    g_logger.write("record: recording to %s (dt=%.9f seed=%u%s)\n",
              path, g_dt, (unsigned)g_seed, g_seed_set ? "" : " UNSET");
    warn_determinism("recording");
}

static bool read_u8(uint8_t *v)
{
    int c = g_in.get();
    if (c == EOF) return false;
    *v = (uint8_t)c;
    return true;
}

static bool read_uvarint(uint32_t *v)
{
    *v = 0;
    for (int shift = 0; shift < 35; shift += 7) {
        uint8_t b;
        if (!read_u8(&b)) return false;
        *v |= (uint32_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) return true;
    }
    return false;
}

static void open_replay(const char *path)
{
    g_in.open(sysdev::nativePath(path), std::ios::binary);
    if (!g_in) {
        g_logger.write("record: cannot open %s for reading\n", path);
        g_mode = 0;
        return;
    }
    uint8_t hdr[19];
    if (!g_in.read((char *)hdr, sizeof(hdr)) ||
        memcmp(hdr, "KROO", 4) != 0) {
        g_logger.write("record: %s is not a recording\n", path);
        g_mode = 0;
        return;
    }
    uint32_t  ver  = hdr[4];
    double rdt  = get_f64(hdr + 6);
    uint32_t  seed = get_u32(hdr + 14);
    if (ver != RECORD_VERSION) {
        g_logger.write("record: %s is version %lu, this build reads %d\n",
                  path, (unsigned long)ver, RECORD_VERSION);
        g_mode = 0;
        return;
    }
    char label[256] = {};
    g_in.read(label, hdr[18]);
    // The bundled save files are for the test harness; skip them.
    uint32_t files;
    if (!read_uvarint(&files)) { g_mode = 0; return; }
    for (uint32_t i = 0; i < files; i++) {
        uint32_t len, size;
        if (!read_uvarint(&len)) { g_mode = 0; return; }
        g_in.ignore(len);
        if (!read_uvarint(&size)) { g_mode = 0; return; }
        g_in.ignore(size);
    }
    g_logger.write("record: replaying %s (recorded dt=%.9f seed=%u label='%s')\n",
              path, rdt, (unsigned)seed, label);
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
    g_mode = 0;
    read_env();

    const std::string recordPath = sysdev::getEnv("KAROO_RECORD");
    const std::string replayPath = sysdev::getEnv("KAROO_REPLAY");
    if (!recordPath.empty()) {
        g_mode = 1;
        open_record(recordPath.c_str());
    } else if (!replayPath.empty()) {
        g_mode = 2;
        open_replay(replayPath.c_str());
    }
}

bool record_recording(void)      { init(); return g_mode == 1; }
bool record_replaying(void)      { init(); return g_mode == 2; }
bool record_replay_finished(void){ return g_finished; }

static void put_uvarint(std::string &out, uint32_t v)
{
    while (v >= 0x80) { out += (char)((v & 0x7f) | 0x80); v >>= 7; }
    out += (char)v;
}

static void flush_frame(void)
{
    if (!g_keys_seen) return;  // the game did not take the keyboard this frame
    std::string buf;
    put_uvarint(buf, g_cur.frame - g_wr.frame);

    int changed = 0;
    for (int i = 0; i < inputdev::KEY_COUNT; i++)
        changed += g_cur.keys[i] != g_wr.keys[i];
    buf += (char)(changed ? 1 : 0);
    if (changed) {
        put_uvarint(buf, changed);
        int last = 0;
        for (int i = 0; i < inputdev::KEY_COUNT; i++) {
            if (g_cur.keys[i] == g_wr.keys[i]) continue;
            put_uvarint(buf, i - last);
            buf += (char)g_cur.keys[i];
            last = i;
        }
    }
    g_wr.frame = g_cur.frame;
    std::copy_n(g_cur.keys, inputdev::KEY_COUNT, g_wr.keys);

    // Flushed every frame: the process can end without the stream closing the file.
    g_out.write(buf.data(), buf.size());
    g_out.flush();

    g_keys_seen = false;
}

void record_keys(const uint8_t *keys)
{
    if (!record_recording()) return;
    g_cur.frame = clock_frame();
    std::copy_n(keys, inputdev::KEY_COUNT, g_cur.keys);
    g_keys_seen = true;
}

static bool read_one(FrameRec *r)
{
    uint32_t delta, n;
    uint8_t flags;
    if (!read_uvarint(&delta) || !read_u8(&flags)) return false;
    g_rd.frame += delta;
    if (flags & 1) {
        if (!read_uvarint(&n)) return false;
        uint32_t key = 0;
        for (uint32_t i = 0; i < n; i++) {
            if (!read_uvarint(&delta)) return false;
            key += delta;
            if (key >= (uint32_t)inputdev::KEY_COUNT || !read_u8(&g_rd.keys[key])) return false;
        }
    }
    r->frame = g_rd.frame;
    std::copy_n(g_rd.keys, inputdev::KEY_COUNT, r->keys);
    return true;
}

/* Records are served by frame index, not in sequence.  A frame the game did
 * not take the keyboard in writes no record, so consuming records in order
 * would shift every later frame.  A record applies from the frame it was
 * recorded on, and holds until the next.  One whose frame has already passed
 * is consumed rather than stalling the replay. */
static void load_frame(void)
{
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
        if (input_debug()) {
            static int served = 0;
            for (int i = 0; i < inputdev::KEY_COUNT && served < 12; i++)
                if (g_play.keys[i] & 0x80) {
                    g_logger.write("replaydbg: frame %u serving rec-frame %u scancode %d\n",
                                   now, g_play.frame, i);
                    served++;
                    break;
                }
        }
    }
}

void replay_keys(uint8_t *keys)
{
    std::copy_n(g_play.keys, inputdev::KEY_COUNT, keys);
}

void record_frame_boundary(void)
{
    init();
    if (g_mode == 1)      flush_frame();
    else if (g_mode == 2) load_frame();
}
