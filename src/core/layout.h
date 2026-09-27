/* Layout checks for classes whose byte layout is fixed: sub-objects of the
 * Game, tiles, objects of a set size.  What is checked is where each field
 * sits relative to the address the code uses for the object (the Game pointer,
 * the tile pointer, the allocation), and nothing else.  Padding is never
 * checked, so a layout can be reshaped (a gap split into a field, a struct
 * re-rooted) without a check changing.
 *
 *     class __attribute__((packed)) LiftObject {
 *     public:
 *         static const int ORIGIN = 0;   // our first byte, relative to the
 *         ...                            // object's address
 *     private:
 *         KAROO_LAYOUT_REGISTER(LiftObject);
 *         double now_;
 *         ...
 *     };
 *
 *     KAROO_LAYOUT_CHECKS(LiftObject)
 *     {
 *         KAROO_LAYOUT_AT(now_, 0x04);
 *         KAROO_LAYOUT_SIZE(0x4c);
 *     }
 *
 * Each check is a static_assert, so a misplaced field fails the build.  It is
 * also recorded at run time in a LayoutReport, and KAROO_LAYOUT_REGISTER puts
 * the class's check function on a global list before main(), for a test runner
 * to walk.
 *
 * KNOWN GAP: no runner walks that list at present, so only the static_asserts
 * check anything; the run-time half is dead weight until one is restored or
 * the registry is dropped. */

#pragma once

#include <stddef.h>

namespace karoo {

/* The one rule: our offset, shifted by where our struct starts, must land on
 * the object's offset. */
constexpr bool layoutAt(long origin, size_t ourOffset, long gameOffset)
{
    return origin + (long)ourOffset == gameOffset;
}

/* The results of one class's checks. */
struct LayoutReport {
    int         checked      = 0;
    int         failed       = 0;
    const char *firstFailure = nullptr;

    void expect(bool ok, const char *what)
    {
        ++checked;
        if (!ok) {
            ++failed;
            if (firstFailure == nullptr)
                firstFailure = what;
        }
    }
};

/* One registered class.  KAROO_LAYOUT_REGISTER's static member constructs it
 * before main(), and it links itself onto the list. */
struct LayoutSuite {
    const char  *name;
    void       (*run)(LayoutReport &);
    LayoutSuite *next;

    LayoutSuite(const char *n, void (*r)(LayoutReport &))
        : name(n), run(r), next(head())
    {
        head() = this;
    }

    // Constant-initialised, so it is valid before any registrar runs.
    static LayoutSuite *&head()
    {
        static LayoutSuite *h = nullptr;
        return h;
    }
};

}

/* Inside the class body (private).  Declares the check function and the
 * registrar; static inline makes one registrar however many files include the
 * header. */
#define KAROO_LAYOUT_REGISTER(Class)                                        \
    typedef Class KarooLayoutSelf;                                          \
    static void karooCheckLayout(karoo::LayoutReport &karooReport_);        \
    static inline karoo::LayoutSuite karooLayoutSuite_{#Class,              \
                                                       &karooCheckLayout}

/* After the class: opens the check function's definition. */
#define KAROO_LAYOUT_CHECKS(Class)                                          \
    inline void Class::karooCheckLayout(karoo::LayoutReport &karooReport_)

/* member must be at gameOffset from the object's address.
 *
 * -Winvalid-offsetof is silenced around these two statements only.  A class
 * derived from a packed base (Bomb : MovableEntity) is not standard-layout, so
 * offsetof on it is only conditionally supported; GCC supports it and places
 * the derived members straight after the base, which is what the asserts
 * check.  The pragma cannot go inside an expression. */
#define KAROO_LAYOUT_AT(member, gameOffset)                                 \
    _Pragma("GCC diagnostic push")                                          \
    _Pragma("GCC diagnostic ignored \"-Winvalid-offsetof\"")                \
    static_assert(karoo::layoutAt(KarooLayoutSelf::ORIGIN,                  \
                                  offsetof(KarooLayoutSelf, member),        \
                                  (gameOffset)),                            \
                  #member " must be at game offset " #gameOffset);          \
    karooReport_.expect(karoo::layoutAt(KarooLayoutSelf::ORIGIN,            \
                                        offsetof(KarooLayoutSelf, member),  \
                                        (gameOffset)),                      \
                        #member " at " #gameOffset);                        \
    _Pragma("GCC diagnostic pop")

/* Only where the size is relied on: an object of a set size. */
#define KAROO_LAYOUT_SIZE(size)                                             \
    static_assert(sizeof(KarooLayoutSelf) == (size),                        \
                  "sizeof must be " #size);                                 \
    karooReport_.expect(sizeof(KarooLayoutSelf) == (size), "sizeof == " #size)
