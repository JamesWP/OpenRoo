#include "sysdev.h"

namespace sysdev {

/* The system takes the game's names as they are: backslashes, any case. */
std::string nativePath(const char *path)
{
    return path;
}

}  // namespace sysdev
