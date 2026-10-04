#include <fstream>
#include "sysdev.h"

namespace sysdev {

bool readTextFile(const char *path, std::string &contents)
{
    std::ifstream in(nativePath(path).c_str(), std::ios::binary);
    if (!in)
        return false;
    std::ostringstream all;
    all << in.rdbuf();
    const std::string raw = all.str();

    contents.clear();
    contents.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); i++) {
        if (raw[i] == '\x1a')
            break;
        if (raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n')
            continue;
        contents += raw[i];
    }
    return true;
}

}  // namespace sysdev
