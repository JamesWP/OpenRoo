/* Small portable stand-ins for what game code used to take from the Win32
 * headers: the path buffer size, interlocked counters and the virtual-key
 * codes of the debug keys. */
#pragma once

#include <atomic>

enum { kMaxPath = 260 };

/* A counter or once-flag shared between threads, zero at static init. */
typedef std::atomic<long> AtomicInt;

/* Return the new value, as InterlockedIncrement did. */
inline long atomicIncrement(AtomicInt *a) { return a->fetch_add(1) + 1; }
/* Returns the previous value, as InterlockedExchange did. */
inline long atomicExchange(AtomicInt *a, long v) { return a->exchange(v); }
inline void atomicAdd(AtomicInt *a, long v) { a->fetch_add(v); }

/* Virtual-key codes for hooks_GetAsyncKeyState. */
enum { kKeyF1 = 0x70, kKeyF3 = 0x72, kKeyF4 = 0x73 };
