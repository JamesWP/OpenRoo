#include "simlog.h"
#include "sysdev.h"

namespace simbot {

SimLog::SimLog()
{
    const std::string path = sysdev::getEnv("KAROO_SIMBOT_LOG");
    if (!path.empty())
        out_.open(sysdev::nativePath(path));
}

void SimLog::write(unsigned long frame, const char *event, const std::string &fields)
{
    if (!on())
        return;
    out_ << "{\"frame\":" << frame << ",\"event\":\"" << event << '"'
         << (fields.empty() ? "" : ",") << fields << "}\n";
    out_.flush();
}

std::string SimLog::quote(const std::string &s)
{
    std::string q = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\')
            q += '\\';
        q += c;
    }
    return q + '"';
}

}  // namespace simbot
