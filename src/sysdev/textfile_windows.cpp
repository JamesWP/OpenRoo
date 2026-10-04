#include <fstream>
#include "sysdev.h"

namespace sysdev {

/* The C runtime's text mode does the translation. */
bool readTextFile(const char *path, std::string &contents)
{
    std::ifstream in(nativePath(path).c_str());
    if (!in)
        return false;
    std::ostringstream all;
    all << in.rdbuf();
    contents = all.str();
    return true;
}

}  // namespace sysdev
