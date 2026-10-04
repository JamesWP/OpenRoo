#include <ctype.h>
#include "sysdev.h"

namespace sysdev {

int compareNoCase(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d || !*a) return d;
    }
    return 0;
}

int compareNoCase(const char *a, const char *b)
{
    return compareNoCase(a, b, (size_t)-1);
}

}  // namespace sysdev
