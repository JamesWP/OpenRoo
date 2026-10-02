/* Heap poisoning, for finding reads of uninitialised memory.  Built only with
 * -DKAROO_HEAP_POISON=ON, which wraps the CRT's malloc and realloc (and so
 * operator new) at link time; tools/find_uninit.py drives it.
 *
 * Every malloc and every realloc counts one allocation, numbered from 0 in
 * call order.  The recordings are deterministic, so a number names the same
 * allocation from run to run.
 *
 *   KAROO_POISON_ALLOC=<byte>  fill value; unset leaves the heap alone
 *   KAROO_POISON_FROM/TO=<n>   poison only allocations in [FROM, TO)
 *   KAROO_POISON_OFF_FROM/TO   within one, poison only these byte offsets and
 *                              zero the rest
 *   KAROO_POISON_LOG=<path>    append one line per poisoned block: index, size,
 *                              image base, then the return addresses found on
 *                              the stack (the build has no frame pointers, so
 *                              the stack is scanned for words that follow a
 *                              call instruction).
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
void *__real_malloc(size_t);
void *__real_realloc(void *, size_t);

static int  s_fill = -2;                     /* -2: not read yet, -1: off */
static long s_from = 0, s_to = 0x7fffffff, s_next = 0;
static long s_off_from = 0, s_off_to = 0x7fffffff;
static const char *s_log;

static long env_long(const char *name, long fallback)
{
    const char *v = getenv(name);
    return v ? atol(v) : fallback;
}

static void init(void)
{
    if (s_fill != -2) return;
    s_fill     = (int)env_long("KAROO_POISON_ALLOC", -1);
    s_from     = env_long("KAROO_POISON_FROM", 0);
    s_to       = env_long("KAROO_POISON_TO", 0x7fffffff);
    s_off_from = env_long("KAROO_POISON_OFF_FROM", 0);
    s_off_to   = env_long("KAROO_POISON_OFF_TO", 0x7fffffff);
    s_log      = getenv("KAROO_POISON_LOG");
}

static void log_block(void *p, size_t n, long index)
{
    if (!s_log) return;
    void *ret[12];
    int count = 0;
    unsigned long *sp = (unsigned long *)&count;
    for (int i = 0; i < 400 && count < 12; i++) {
        unsigned long v = sp[i];
        if (v >= 0x401005 && v < 0x1000000 && *(unsigned char *)(v - 5) == 0xE8)
            ret[count++] = (void *)v;
    }
    FILE *f = fopen(s_log, "a");
    if (!f) return;
    fprintf(f, "%ld size=%lu p=%p base=%p", index, (unsigned long)n, p,
            (void *)GetModuleHandleA(NULL));
    for (int i = 0; i < count; i++) fprintf(f, " %p", ret[i]);
    fputc('\n', f);
    fclose(f);
}

/* Fill [from, n) of a fresh block; with an offset window, only that window is
 * poisoned and the rest is zero, so one run can isolate a few bytes. */
static void poison(unsigned char *p, size_t from, size_t n, long index)
{
    if (s_fill < 0 || index < s_from || index >= s_to) return;
    if (s_off_from == 0 && s_off_to == 0x7fffffff) {
        memset(p + from, s_fill, n - from);
    } else {
        memset(p + from, 0, n - from);
        size_t lo = (size_t)s_off_from > from ? (size_t)s_off_from : from;
        size_t hi = (size_t)s_off_to < n ? (size_t)s_off_to : n;
        if (lo < hi) memset(p + lo, s_fill, hi - lo);
    }
    log_block(p, n, index);
}

void *__wrap_malloc(size_t n)
{
    init();
    long index = s_next++;
    void *p = __real_malloc(n);
    if (p) poison((unsigned char *)p, 0, n, index);
    return p;
}

void *__wrap_realloc(void *p, size_t n)
{
    init();
    size_t old = p ? _msize(p) : 0;
    long index = s_next++;
    void *r = __real_realloc(p, n);
    if (r && n > old) poison((unsigned char *)r, old, n, index);
    return r;
}
}
