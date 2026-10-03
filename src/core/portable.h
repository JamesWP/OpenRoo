/* Small portable stand-ins for the helpers game code used to take from the Win32 headers:
 * path buffer size, case-insensitive compare, interlocked
 * counters and the virtual-key codes of the debug keys. */
#pragma once

#include <atomic>
#include <string.h>

#if !defined(_WIN32)
#include <strings.h>
#endif

enum { kMaxPath = 260 };

inline int strcaseCompare(const char *a, const char *b)
{
#if defined(_WIN32)
    return _stricmp(a, b);
#else
    return strcasecmp(a, b);
#endif
}

/* A counter or once-flag shared between threads, zero at static init. */
typedef std::atomic<long> AtomicInt;

/* Return the new value, as InterlockedIncrement did. */
inline long atomicIncrement(AtomicInt *a) { return a->fetch_add(1) + 1; }
/* Returns the previous value, as InterlockedExchange did. */
inline long atomicExchange(AtomicInt *a, long v) { return a->exchange(v); }
inline void atomicAdd(AtomicInt *a, long v) { a->fetch_add(v); }

/* Virtual-key codes for hooks_GetAsyncKeyState. */
enum { kKeyF1 = 0x70, kKeyF3 = 0x72, kKeyF4 = 0x73 };
