/* Little-endian field access for on-disk records: the files are read and
 * written a byte at a time, so the in-memory structs keep natural layout. */
#pragma once

#include <string.h>

inline void put_u8(unsigned char *&p, unsigned char v) { *p++ = v; }

inline void put_u16(unsigned char *&p, unsigned short v)
{
    *p++ = (unsigned char)v;
    *p++ = (unsigned char)(v >> 8);
}

inline void put_u32(unsigned char *&p, unsigned int v)
{
    *p++ = (unsigned char)v;
    *p++ = (unsigned char)(v >> 8);
    *p++ = (unsigned char)(v >> 16);
    *p++ = (unsigned char)(v >> 24);
}

inline void put_bytes(unsigned char *&p, const void *src, unsigned int n)
{
    memcpy(p, src, n);
    p += n;
}

inline unsigned char get_u8(const unsigned char *&p) { return *p++; }

inline unsigned short get_u16(const unsigned char *&p)
{
    unsigned short v = (unsigned short)(p[0] | (p[1] << 8));
    p += 2;
    return v;
}

inline unsigned int get_u32(const unsigned char *&p)
{
    unsigned int v = (unsigned int)p[0] | ((unsigned int)p[1] << 8)
                   | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
    p += 4;
    return v;
}

inline void get_bytes(const unsigned char *&p, void *dst, unsigned int n)
{
    memcpy(dst, p, n);
    p += n;
}
