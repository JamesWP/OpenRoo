/* A frame profiler: PROF_SCOPE("name") times the enclosing block and files it
 * under whichever scope is open, so the profile mirrors the update and draw
 * code's own call tree.
 *
 * Scopes with the same name under the same parent merge (a loop's iterations
 * add up, with a call count).  The tree persists between frames; each node
 * keeps its last frame's time and a running average.  Names must be string
 * literals.
 *
 * Off until setEnabled(true), when a scope costs one branch.  A frame is the
 * outermost scope: its end rolls the averages.  Timing is wall-clock only and
 * never touches game state. */
#pragma once

namespace prof {

struct Node {
    const char *name;
    int         parent;       // -1 for a root
    double      ms;           // the last frame, summed over its calls
    double      avgMs;        // smoothed
    double      selfAvgMs;    // avgMs less the time in its children
    double      maxMs;        // slowest frame in the last second or so
    unsigned    calls;        // in the last frame
    unsigned    framesIdle;   // frames since it last ran
    // Per-frame working state.
    double      accMs;
    double      childMs;      // time in children this frame
    unsigned    accCalls;
    double      startMs;
};

void setEnabled(bool on);

/* Slow-frame detection.  KAROO_SLOWFRAME_MS=<ms> turns the profiler on for the
 * whole run (the debug UI can no longer switch it off) and keeps the last
 * kSlowKeep frames whose "frame" scope took longer than that, each with the
 * scopes that took 0.5 ms or more.  Frames that prepared a level are not kept:
 * a level load is slow by design.  dumpSlowFrames() writes them to the log;
 * the app calls it on exit. */
void initSlowFrames();
void dumpSlowFrames();
bool enabled();

/* The nodes in first-seen order, so a parent always precedes its children. */
const Node *nodes(unsigned *count);

/* Whether a node has run lately: a scope that is not entered (no bridges in
 * this level, the menu while playing) drops out of view after a second or so. */
bool recent(const Node &n);

/* Drops the tree, so renamed or stale scopes go. */
void reset();

class Scope {
public:
    explicit Scope(const char *name) { if (enabled()) begin(name); }
    ~Scope() { if (node_ >= 0) end(); }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
private:
    void begin(const char *name);
    void end();
    int node_ = -1;
};

}  // namespace prof

#define PROF_CAT2(a, b) a##b
#define PROF_CAT(a, b) PROF_CAT2(a, b)
#define PROF_SCOPE(name) prof::Scope PROF_CAT(prof_scope_, __LINE__)(name)
