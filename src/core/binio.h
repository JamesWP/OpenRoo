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
