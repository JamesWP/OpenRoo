/* Raw binary reads and writes on C++ streams, for the game's file formats.
 * Each returns whether the whole count went through; a short read leaves
 * what was read in place. */

#pragma once

#include <istream>
#include <ostream>
#include <stddef.h>

inline bool readBytes(std::istream &in, void *dst, size_t size)
{
    in.read(static_cast<char *>(dst), (std::streamsize)size);
    return (size_t)in.gcount() == size;
}

inline bool writeBytes(std::ostream &out, const void *src, size_t size)
{
    out.write(static_cast<const char *>(src), (std::streamsize)size);
    return (bool)out;
}

/* printf-style formatted text to a stream, for the dumps whose layouts are
 * fixed by printf conversions. */
#include <stdarg.h>
#include <stdio.h>
#include <vector>

inline void printTo(std::ostream &out, const char *fmt, ...)
{
    char small[512];
    va_list ap;
    va_start(ap, fmt);
    va_list again;
    va_copy(again, ap);
    int n = vsnprintf(small, sizeof(small), fmt, ap);
    va_end(ap);
    if (n >= 0 && (size_t)n < sizeof(small)) {
        out.write(small, n);
    } else if (n >= 0) {
        std::vector<char> big((size_t)n + 1);
        vsnprintf(big.data(), big.size(), fmt, again);
        out.write(big.data(), n);
    }
    va_end(again);
}
