#include "prof.h"
#include "logger.h"
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace prof {

static bool              g_on;
static std::vector<Node> g_nodes;
static int               g_open = -1;   // the innermost open scope

/* The ring of slow frames: where each spent its time, as one line of text. */
static const unsigned kSlowKeep = 64;
struct SlowFrame {
    unsigned frame;
    double   ms;
    char     text[320];
};
static double    g_slowMs;          // 0: detection off
static SlowFrame g_slow[kSlowKeep];
static unsigned  g_frames;          // "frame" scopes ended
static unsigned  g_slowCount;       // slow frames seen, kept or not

static void note_slow_frame(int root)
{
    const Node &f = g_nodes[root];
    SlowFrame &s = g_slow[g_slowCount++ % kSlowKeep];
    s.frame = g_frames;
    s.ms    = f.accMs;
    size_t len = 0;
    s.text[0] = 0;
    for (const Node &m : g_nodes)
        if (m.accMs >= 0.5 && len < sizeof(s.text))
            len += snprintf(s.text + len, sizeof(s.text) - len, " %s(%d)=%.1f",
                            m.name, m.parent, m.accMs);
}

static bool is_level_load(int root)
{
    for (const Node &m : g_nodes)
        if (m.parent == root && m.accMs > 0.0 && strcmp(m.name, "level prepare") == 0)
            return true;
    return false;
}

void initSlowFrames()
{
    const char *ms = getenv("KAROO_SLOWFRAME_MS");
    g_slowMs = ms ? atof(ms) : 0.0;
    if (g_slowMs > 0.0) {
        g_on = true;
        g_logger.write("prof: keeping frames slower than %.1f ms\n", g_slowMs);
    }
}

void dumpSlowFrames()
{
    if (g_slowMs <= 0.0)
        return;
    g_logger.write("prof: %u slow frame(s) of %u; last %u, as frame(parent)=ms:\n",
                   g_slowCount, g_frames, g_slowCount < kSlowKeep ? g_slowCount : kSlowKeep);
    const unsigned first = g_slowCount > kSlowKeep ? g_slowCount - kSlowKeep : 0;
    for (unsigned i = first; i < g_slowCount; i++) {
        const SlowFrame &s = g_slow[i % kSlowKeep];
        g_logger.write("prof: frame %u took %.1f ms:%s\n", s.frame, s.ms, s.text);
    }
}

static double now_ms()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

void setEnabled(bool on)
{
    if (g_slowMs > 0.0)
        return;  // slow-frame detection keeps the profiler on
    if (g_on && !on) {
        // Never leave a half-open frame behind.
        g_open = -1;
    }
    g_on = on;
}

bool enabled() { return g_on; }

const Node *nodes(unsigned *count)
{
    *count = (unsigned)g_nodes.size();
    return g_nodes.data();
}

bool recent(const Node &n) { return n.framesIdle < 60; }

void reset()
{
    g_nodes.clear();
    g_open = -1;
}

void Scope::begin(const char *name)
{
    int n = -1;
    for (size_t i = 0; i < g_nodes.size(); i++)
        if (g_nodes[i].parent == g_open && strcmp(g_nodes[i].name, name) == 0) {
            n = (int)i;
            break;
        }
    if (n < 0) {
        Node node = {};
        node.name   = name;
        node.parent = g_open;
        g_nodes.push_back(node);
        n = (int)g_nodes.size() - 1;
    }
    g_nodes[n].startMs = now_ms();
    node_  = n;
    // Remembered so end() can restore it.
    g_open = n;
}

void Scope::end()
{
    if (node_ >= (int)g_nodes.size() || g_open != node_) {
        node_ = -1;  // the tree was reset or disabled under us
        return;
    }
    Node &n = g_nodes[node_];
    const double took = now_ms() - n.startMs;
    n.accMs += took;
    n.accCalls++;
    g_open = n.parent;
    if (n.parent >= 0)
        g_nodes[n.parent].childMs += took;

    if (n.parent >= 0)
        return;
    // A root ended: that was the frame.
    if (strcmp(n.name, "frame") == 0) {
        g_frames++;
        if (g_slowMs > 0.0 && n.accMs > g_slowMs && !is_level_load(node_))
            note_slow_frame(node_);
    }
    for (Node &m : g_nodes) {
        m.ms    = m.accMs;
        m.calls = m.accCalls;
        m.framesIdle = m.calls ? 0 : m.framesIdle + 1;
        m.avgMs += (m.ms - m.avgMs) * 0.05;
        m.selfAvgMs += ((m.ms - m.childMs) - m.selfAvgMs) * 0.05;
        m.childMs = 0;
        m.maxMs  = m.ms > m.maxMs ? m.ms : m.maxMs * 0.98;
        m.accMs = 0;
        m.accCalls = 0;
    }
}

}  // namespace prof
