#include "gldiag.h"
#include "glapi.h"
#include "logger.h"
#include "sysdev.h"
#include <chrono>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

namespace gldiag {

namespace {

const unsigned kRing        = 16;  // queries in flight
const unsigned kMaxMessages = 200;

bool     g_on;
double   g_thresholdMs = 8.0;
unsigned g_messages;
unsigned g_calls;

struct Pending {
    unsigned    id;
    const char *what;
    double      cpuMs;
    double      busyMs;
    unsigned    call;
    bool        busy;
};
Pending g_ring[kRing];
unsigned g_head;                   // the next slot to use

double now_ms()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

void APIENTRY on_message(GLenum source, GLenum type, GLuint id, GLenum severity,
                         GLsizei length, const GLchar *message, const void *)
{
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION && type != GL_DEBUG_TYPE_PERFORMANCE)
        return;
    if (g_messages++ >= kMaxMessages) {
        if (g_messages == kMaxMessages + 1)
            g_logger.write("gldiag: further driver messages dropped\n");
        return;
    }
    g_logger.write("gldiag: call %u: driver message type 0x%x source 0x%x id %u "
                   "severity 0x%x: %.*s\n", g_calls, type, source, id, severity,
                   (int)length, message);
}

/* CPU time this thread has used: well under the wall time means it was
 * blocked or descheduled, close to it means the driver was working. */
double thread_cpu_ms()
{
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

/* Logs a finished query, if its call was a slow one. */
void collect(Pending &p)
{
    GLint ready = 0;
    gl.GetQueryObjectiv(p.id, GL_QUERY_RESULT_AVAILABLE, &ready);
    if (!ready)
        return;
    GLuint64 ns = 0;
    gl.GetQueryObjectui64v(p.id, GL_QUERY_RESULT, &ns);
    p.busy = false;
    if (p.cpuMs >= g_thresholdMs)
        g_logger.write("gldiag: call %u: %s took %.1f ms on the CPU (%.1f ms busy), %.2f ms on the GPU\n",
                       p.call, p.what, p.cpuMs, p.busyMs, (double)ns / 1e6);
}

}  // namespace

bool wanted()
{
    char v[4];
    return sysdev::getEnv("KAROO_GLDIAG", v, sizeof(v)) && v[0] == '1';
}

void init()
{
    if (!wanted())
        return;
    char ms[16];
    if (sysdev::getEnv("KAROO_GLDIAG_MS", ms, sizeof(ms)) && atof(ms) > 0.0)
        g_thresholdMs = atof(ms);
    if (gl.DebugMessageCallback) {
        gl.Enable(GL_DEBUG_OUTPUT);
        gl.Enable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        gl.DebugMessageCallback(on_message, NULL);
    }
    const bool timers = gl.GenQueries && gl.BeginQuery && gl.EndQuery
                        && gl.GetQueryObjectiv && gl.GetQueryObjectui64v;
    if (timers)
        for (Pending &p : g_ring)
            gl.GenQueries(1, &p.id);
    g_on = true;
    g_logger.write("gldiag: on (%s, %s), logging calls slower than %.1f ms\n",
                   gl.DebugMessageCallback ? "debug messages" : "no debug messages",
                   timers ? "timer queries" : "no timer queries", g_thresholdMs);
}

Timed::Timed(const char *what) : what_(what), startMs_(0), startBusyMs_(0), query_(0)
{
    if (!g_on)
        return;
    startMs_ = now_ms();
    startBusyMs_ = thread_cpu_ms();
    g_calls++;
    Pending &p = g_ring[g_head];
    if (p.id && !p.busy) {
        query_ = p.id;
        gl.BeginQuery(GL_TIME_ELAPSED, query_);
    }
}

Timed::~Timed()
{
    if (!g_on)
        return;
    if (query_)
        gl.EndQuery(GL_TIME_ELAPSED);
    const double cpuMs  = now_ms() - startMs_;
    const double busyMs = thread_cpu_ms() - startBusyMs_;
    // Only one GL_TIME_ELAPSED query can be active at a time, so an inner
    // Timed would have no query: they are used flat.
    Pending &p = g_ring[g_head];
    if (query_) {
        p.what  = what_;
        p.cpuMs  = cpuMs;
        p.busyMs = busyMs;
        p.call  = g_calls;
        p.busy  = true;
    } else if (cpuMs >= g_thresholdMs) {
        g_logger.write("gldiag: call %u: %s took %.1f ms on the CPU (%.1f ms busy)\n",
                       g_calls, what_, cpuMs, busyMs);
    }
    g_head = (g_head + 1) % kRing;
    for (Pending &q : g_ring)
        if (q.busy)
            collect(q);
}

}  // namespace gldiag
