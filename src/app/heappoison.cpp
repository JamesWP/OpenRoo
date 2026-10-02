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
 *   KAROO_POISON_GUARD=<n>     put an n-byte guard after each allocation in the
 *                              FROM/TO window, and a fixed one before it; check
 *                              them on free, every 4096 allocations and at exit.
 *                              A trampled guard is logged as a GUARD line with
 *                              the allocation's index, size and call sites.
 *   KAROO_POISON_FREE=<byte>   fill a guarded block with this on free, and keep
 *                              it (never reuse it); a later write into it is
 *                              logged as a FREED_WRITE line
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
void __real_free(void *);
void __wrap_free(void *);
static void check_all(void);

static int  s_fill = -2;                     /* -2: not read yet, -1: off */
static long s_from = 0, s_to = 0x7fffffff, s_next = 0;
static long s_off_from = 0, s_off_to = 0x7fffffff;
static const char *s_log;
static long s_guard = 0;
static int  s_free_fill = -1;

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
    s_guard    = env_long("KAROO_POISON_GUARD", 0);
    s_free_fill = (int)env_long("KAROO_POISON_FREE", -1);
    if (s_guard > 0) atexit(check_all);
}

static unsigned long image_lo, image_hi;

static int capture(void **ret, int max)
{
    if (!image_lo) {                            /* the exe's own image only: v - 5 must be readable */
        char *base = (char *)GetModuleHandleA(NULL);
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
        image_lo = (unsigned long)base + 0x1005;
        image_hi = (unsigned long)base + nt->OptionalHeader.SizeOfImage;
    }
    int count = 0;
    unsigned long *sp = (unsigned long *)&count;
    for (int i = 0; i < 400 && count < max; i++) {
        unsigned long v = sp[i];
        if (v >= image_lo && v < image_hi && *(unsigned char *)(v - 5) == 0xE8)
            ret[count++] = (void *)v;
    }
    return count;
}

static void log_block(void *p, size_t n, long index)
{
    if (!s_log) return;
    static void *ret[12];
    int count = capture(ret, 12);
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


/* Guarded blocks: [Header][user bytes][tail guard].  The header ends in a front
 * guard; free() tells a guarded block from any other by the magic before it. */
#define GUARD_MAGIC 0x6755A4D1u
#define GUARD_BYTE  0xFD
#define FRONT_GUARD 28
struct Header {
    unsigned magic, size, index, tail;
    Header *prev, *next;
    void *site[4];
    unsigned char front[FRONT_GUARD];
    unsigned reported;
};
static Header *s_live, *s_dead;
static long s_ops;

static unsigned char *user_of(Header *h) { return (unsigned char *)(h + 1); }

static void report(Header *h, const char *side, long off, long bad)
{
    if (h->reported) return;
    h->reported = 1;
    if (!s_log) return;
    FILE *f = fopen(s_log, "a");
    if (!f) return;
    fprintf(f, "GUARD index=%u size=%u side=%s first_bad_offset=%ld bad_bytes=%ld base=%p sites",
            h->index, h->size, side, off, bad, (void *)GetModuleHandleA(NULL));
    for (int i = 0; i < 4; i++) fprintf(f, " %p", h->site[i]);
    fputc('\n', f);
    fclose(f);
}

static void check(Header *h)
{
    long bad = 0, first = -1;
    for (int i = 0; i < FRONT_GUARD; i++)
        if (h->front[i] != GUARD_BYTE) { if (!bad) first = i - FRONT_GUARD; bad++; }
    if (bad) report(h, "front", first, bad);
    bad = 0;
    unsigned char *t = user_of(h) + h->size;
    for (unsigned i = 0; i < h->tail; i++)
        if (t[i] != GUARD_BYTE) { if (!bad) first = (long)i; bad++; }
    if (bad) report(h, "tail", first, bad);
}

static void check_dead(Header *h)
{
    if (h->reported) return;
    const unsigned char *u = user_of(h);
    for (unsigned i = 0; i < h->size; i++)
        if (u[i] != (unsigned char)s_free_fill) {
            h->reported = 1;
            if (!s_log) return;
            FILE *f = fopen(s_log, "a");
            if (!f) return;
            fprintf(f, "FREED_WRITE index=%u size=%u first_bad_offset=%u base=%p sites",
                    h->index, h->size, i, (void *)GetModuleHandleA(NULL));
            for (int k = 0; k < 4; k++) fprintf(f, " %p", h->site[k]);
            fputc('\n', f);
            fclose(f);
            return;
        }
}

static void check_all(void)
{
    for (Header *h = s_live; h; h = h->next) check(h);
    for (Header *h = s_dead; h; h = h->next) check_dead(h);
}

static void unlink_block(Header *h)
{
    if (h->prev) h->prev->next = h->next; else s_live = h->next;
    if (h->next) h->next->prev = h->prev;
}

/* Guarded blocks are found by pointer, in an open-addressed table: reading
 * the bytes before an arbitrary pointer can fault (a big block sits at the
 * start of its own mapping). */
#define TABLE_SIZE (1 << 17)
static Header *s_table[TABLE_SIZE];
static unsigned slot_of(void *p) { return ((unsigned long)p >> 3) * 2654435761u >> 15 & (TABLE_SIZE - 1); }

static void table_add(Header *h)
{
    unsigned i = slot_of(user_of(h));
    while (s_table[i] && s_table[i] != (Header *)1) i = (i + 1) & (TABLE_SIZE - 1);
    s_table[i] = h;
}

static Header *guarded(void *p)
{
    if (!p) return NULL;
    for (unsigned i = slot_of(p); s_table[i]; i = (i + 1) & (TABLE_SIZE - 1))
        if (s_table[i] != (Header *)1 && user_of(s_table[i]) == p) return s_table[i];
    return NULL;
}

static void table_remove(Header *h)
{
    unsigned i = slot_of(user_of(h));
    while (s_table[i] != h) i = (i + 1) & (TABLE_SIZE - 1);
    s_table[i] = (Header *)1;                   /* tombstone */
}

static void tick(void)
{
    if (s_guard > 0 && (++s_ops & 4095) == 0) check_all();
}

static void *guarded_alloc(size_t n, long index)
{
    Header *h = (Header *)__real_malloc(sizeof(Header) + n + s_guard);
    if (!h) return NULL;
    h->magic = GUARD_MAGIC; h->size = (unsigned)n; h->index = (unsigned)index;
    h->tail = (unsigned)s_guard; h->reported = 0;
    static void *ret[12];
    int c = capture(ret, 12);
    for (int i = 0; i < 4; i++) h->site[i] = 0;
    /* skip the wrapper's own frames: __wrap_malloc and operator new sit first */
    int skip = c > 6 ? 2 : 0;
    for (int i = 0; i < 4 && skip + i < c; i++) h->site[i] = ret[skip + i];
    memset(h->front, GUARD_BYTE, FRONT_GUARD);
    memset(user_of(h) + n, GUARD_BYTE, (size_t)s_guard);
    h->prev = NULL; h->next = s_live;
    if (s_live) s_live->prev = h;
    s_live = h;
    table_add(h);
    return user_of(h);
}

void *__wrap_malloc(size_t n)
{
    init();
    tick();
    long index = s_next++;
    void *p;
    if (s_guard > 0 && index >= s_from && index < s_to) p = guarded_alloc(n, index);
    else p = __real_malloc(n);
    if (p) poison((unsigned char *)p, 0, n, index);
    return p;
}

void *__wrap_realloc(void *p, size_t n)
{
    init();
    tick();
    if (!p) return __wrap_malloc(n);
    Header *h = guarded(p);
    if (h) {
        check(h);
        void *r = __wrap_malloc(n);
        if (!r) return NULL;
        memcpy(r, p, h->size < n ? h->size : n);
        __wrap_free(p);
        return r;
    }
    size_t old = _msize(p);
    long index = s_next++;
    void *r = __real_realloc(p, n);
    if (r && n > old) poison((unsigned char *)r, old, n, index);
    return r;
}

void __wrap_free(void *p)
{
    init();
    tick();
    Header *h = guarded(p);
    if (!h) { __real_free(p); return; }
    check(h);
    unlink_block(h);
    table_remove(h);
    if (s_free_fill >= 0) {                      /* quarantine: fill, keep, check later */
        memset(user_of(h), s_free_fill, h->size);
        h->reported = 0;
        h->prev = NULL; h->next = s_dead;
        if (s_dead) s_dead->prev = h;
        s_dead = h;
        return;
    }
    h->magic = 0;
    __real_free(h);
}
}
