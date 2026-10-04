#include <algorithm>
#include <filesystem>
#include <map>
#include <mutex>
#include "sysdev.h"

namespace fs = std::filesystem;

namespace sysdev {

/* The component of dir whose name matches name ignoring case, or none. */
static bool findNoCase(const fs::path &dir, const std::string &name, std::string &found)
{
    std::error_code ec;
    for (fs::directory_iterator it(dir.empty() ? fs::path(".") : dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::string n = it->path().filename().string();
        if (compareNoCase(n.c_str(), name.c_str()) == 0) {
            found = n;
            return true;
        }
    }
    return false;
}

std::string nativePath(const char *path)
{
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');

    std::error_code ec;
    if (fs::exists(p, ec))
        return p;

    /* Settled names are kept: the same few hundred files are opened over and
     * over. */
    static std::mutex lock;
    static std::map<std::string, std::string> cache;
    {
        std::lock_guard<std::mutex> g(lock);
        auto it = cache.find(p);
        if (it != cache.end() && fs::exists(it->second, ec))
            return it->second;
    }

    /* Walk the components, matching each against its directory ignoring case.
     * Once one is missing the rest is kept as given. */
    fs::path out;
    size_t pos = 0;
    bool missing = false;
    if (!p.empty() && p[0] == '/') {
        out = "/";
        pos = 1;
    }
    while (pos < p.size()) {
        size_t slash = p.find('/', pos);
        std::string part = p.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
        pos = slash == std::string::npos ? p.size() : slash + 1;
        if (part.empty() || part == ".") continue;
        if (!missing) {
            fs::path candidate = out / part;
            std::string found;
            if (part == ".." || fs::exists(candidate, ec))
                out = candidate;
            else if (findNoCase(out, part, found))
                out /= found;
            else {
                missing = true;
                out = candidate;
            }
        } else {
            out /= part;
        }
    }
    std::string result = out.empty() ? p : out.string();
    if (!missing) {
        std::lock_guard<std::mutex> g(lock);
        cache[p] = result;
    }
    return result;
}

}  // namespace sysdev
