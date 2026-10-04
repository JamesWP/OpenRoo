#include "prof.h"
#include <chrono>
#include <string.h>
#include <vector>

namespace prof {

static bool              g_on;
static std::vector<Node> g_nodes;
static int               g_open = -1;   // the innermost open scope

static double now_ms()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

void setEnabled(bool on)
{
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
    n.accMs += now_ms() - n.startMs;
    n.accCalls++;
    g_open = n.parent;

    if (n.parent >= 0)
        return;
    // A root ended: that was the frame.
    for (Node &m : g_nodes) {
        m.ms    = m.accMs;
        m.calls = m.accCalls;
        m.avgMs += (m.ms - m.avgMs) * 0.05;
        m.maxMs  = m.ms > m.maxMs ? m.ms : m.maxMs * 0.98;
        m.accMs = 0;
        m.accCalls = 0;
    }
}

}  // namespace prof
