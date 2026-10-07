/* Opt-in driver diagnostics, on with KAROO_GLDIAG=1: a debug context whose
 * messages go to the log, and wall-clock plus GPU timing of the clear and the
 * swap.  A call that took longer than KAROO_GLDIAG_MS (default 8) on the CPU
 * is logged with the GPU time of its query, so a stall inside the driver
 * (CPU long, GPU short) can be told from slow GPU work. */
#pragma once

namespace gldiag {

/* Whether the context should be a debug context. */
bool wanted();

/* After the context is current and the functions are loaded. */
void init();

/* Brackets one GL call worth timing; `what` is a string literal.  Free when
 * diagnostics are off. */
class Timed {
public:
    explicit Timed(const char *what);
    ~Timed();
    Timed(const Timed &) = delete;
    Timed &operator=(const Timed &) = delete;
private:
    const char *what_;
    double      startMs_;
    unsigned    query_;
};

}  // namespace gldiag
