/* The sim log: KAROO_SIMBOT_LOG=<file>, one JSON object a line, each with
 * the frame and an event: the survey, a plan, a step started, done or
 * impossible, and each frame's work (work.h) when there was any. */
#pragma once

#include <fstream>
#include <string>

namespace simbot {

class SimLog {
public:
    SimLog();
    bool on() const { return out_.is_open(); }
    /* A line: the frame, the event, and fields, already JSON ("\"k\":1"). */
    void write(unsigned long frame, const char *event, const std::string &fields = "");
    static std::string quote(const std::string &s);

private:
    std::ofstream out_;
};

}  // namespace simbot
