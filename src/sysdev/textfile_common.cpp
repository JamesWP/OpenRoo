#include "sysdev.h"

namespace sysdev {

TextFile::TextFile(const char *path)
{
    std::string contents;
    open_ = readTextFile(path, contents);
    if (open_)
        str(contents);
    else
        setstate(std::ios::failbit);
}

}  // namespace sysdev
