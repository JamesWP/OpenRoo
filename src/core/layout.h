/* Layout checks for classes that overlay game-owned memory.
 *
 * What we rely on is where each field sits relative to the address the GAME
 * uses for the object -- the Game pointer, the `tileBase + (v + u*100)*0x7f`
 * tile pointer, the pointer operator new returned.  That is what is checked,
 * and nothing else: padding sizes never are, so a layout may be reshaped (a
 * gap split into a new field, a struct re-rooted) without any check changing.
 *
 * Usage, gtest-style -- nothing else needs to know the class exists:
 *
 *     class __attribute__((packed)) LiftObject {
 *     public:
 *         static const int ORIGIN = 0;   // our first byte, relative to the
 *         ...                            // game's address for the object
 *     private:
 *         KAROO_LAYOUT_REGISTER(LiftObject);
 *         double now_;                   // +0x04
 *         ...
 *     };
 *
 *     KAROO_LAYOUT_CHECKS(LiftObject)
 *     {
 *         KAROO_LAYOUT_AT(now_, 0x04);
 *         KAROO_LAYOUT_SIZE(0x4c);
 *     }
 *
 * Each check is BOTH
 *   - a static_assert, so the DLL build itself fails on a misplaced field;
 *   - a runtime result, recorded when layouttest.exe (`make check-layout`)
 *     runs the class's check function.
 * KAROO_LAYOUT_REGISTER adds a static registrar that puts the check function
 * on a global list before main(); layouttest.exe links every DLL object and
 * walks that list, so no test file or Makefile lists the classes.
 */
#pragma once

#include <stddef.h>

namespace karoo {

/* The one rule: our offset, shifted by where our struct starts, must land on
 * the game's offset.  Used by the macros and by layouttest's self-test. */
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

/* One registered class.  Constructed by KAROO_LAYOUT_REGISTER's static
 * member before main(); links itself onto the list. */
struct LayoutSuite {
    const char  *name;
    void       (*run)(LayoutReport &);
    LayoutSuite *next;

    LayoutSuite(const char *n, void (*r)(LayoutReport &))
        : name(n), run(r), next(head())
    {
        head() = this;
    }

    /* Constant-initialised, so it is valid before any registrar runs. */
    static LayoutSuite *&head()
    {
        static LayoutSuite *h = nullptr;
        return h;
    }
};

}  // namespace karoo

/* Inside the class body (private).  Declares the check function and the
 * registrar; `static inline` means one registrar however many files include
 * the header, and none if no linked file does. */
#define KAROO_LAYOUT_REGISTER(Class)                                        \
    typedef Class KarooLayoutSelf;                                          \
    static void karooCheckLayout(karoo::LayoutReport &karooReport_);        \
    static inline karoo::LayoutSuite karooLayoutSuite_{#Class,              \
                                                       &karooCheckLayout}

/* After the class: opens the check function's definition. */
#define KAROO_LAYOUT_CHECKS(Class)                                          \
    inline void Class::karooCheckLayout(karoo::LayoutReport &karooReport_)

/* `member` must be at `gameOffset` from the game's address for the object. */
#define KAROO_LAYOUT_AT(member, gameOffset)                                 \
    static_assert(karoo::layoutAt(KarooLayoutSelf::ORIGIN,                  \
                                  offsetof(KarooLayoutSelf, member),        \
                                  (gameOffset)),                            \
                  #member " must be at game offset " #gameOffset);          \
    karooReport_.expect(karoo::layoutAt(KarooLayoutSelf::ORIGIN,            \
                                        offsetof(KarooLayoutSelf, member),  \
                                        (gameOffset)),                      \
                        #member " at " #gameOffset)

/* Only where the size is relied on -- an object the game allocates. */
#define KAROO_LAYOUT_SIZE(size)                                             \
    static_assert(sizeof(KarooLayoutSelf) == (size),                        \
                  "sizeof must be " #size);                                 \
    karooReport_.expect(sizeof(KarooLayoutSelf) == (size), "sizeof == " #size)
