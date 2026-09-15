/* GAMETICK_PLAN.md Band B — the menu node stack, all three functions:
 *
 *     Game::PushMenuNodeOnStack        0x0041ebd0
 *     Game::PopMenuNodeFromStack       0x0041ec00
 *     Game::RewindMenuStackToRootNode  0x0041edd0
 *
 * ─── Why one cycle ───────────────────────────────────────────────────────
 *
 * They are the push, the pop and the reset of ONE array.  Callee lists, read
 * from Ghidra this session per the plan's standing rule:
 *
 *   PushMenuNodeOnStack        (none — a true leaf)
 *   PopMenuNodeFromStack       (none — a true leaf)
 *   RewindMenuStackToRootNode  PushMenuNodeOnStack 0x0041ebd0
 *
 * So the family is closed: the only game callee any of them has is another
 * member.  Taking the push without the rewind would leave the rewind calling
 * a UD2 stub; taking the rewind without the push would mean our rewind
 * calling back into the game binary, which the no-callback rule forbids.
 * Our rewind calls our push directly, so the original's one call site at
 * 0x0041EDE5 becomes dead code behind its own UD2 stub.
 *
 * ─── `this` is the MENU base, not `Game` ─────────────────────────────────
 *
 * All three take the menu sub-object, Game+0x175518.  Every offset below is
 * relative to THAT.  The map is `karoo-hooks/menu.cpp`'s, derived
 * independently from the navigator `NavigateMenuTree 0x0041ec40` and
 * `Game::HandleKeypress` — two unrelated decompiles agreeing on the depth
 * byte, the stack array, the current-node byte and the cursor:
 *
 *   +0x18     leave-menu flag   (DWORD)     Game + 0x175530
 *   +0x1d     cursor            (BYTE)      Game + 0x175535
 *   +0x1e     saved cursor[node]            Game + 0x175536 + node
 *   +0x2001d  stack depth       (BYTE)      Game + 0x195535
 *   +0x2001e  node stack[]                  Game + 0x195536 + i
 *   +0x2021c  current node      (BYTE)      Game + 0x195734
 *
 * ─── Read from the LISTING ───────────────────────────────────────────────
 *
 * PushMenuNodeOnStack  0x0041ebd0, __thiscall, RET 4:
 *
 *   0041ebd6  MOV AL,byte [ECX+0x2001d]              depth
 *   0041ebdc  MOV byte [EAX+ECX+0x2001e],DL          stack[depth] = node
 *   0041ebe3  MOV AL,byte [ECX+0x2001d]              RE-READ the depth
 *   0041ebe9  INC AL
 *   0041ebeb  MOV byte [ECX+0x2001d],AL
 *
 * The depth byte is loaded twice, not held in a register across the store.
 * Transcribed literally; it cannot differ here, but it is what the code does.
 * No bounds check: the array is 0x1fe bytes to the current-node byte and the
 * depth is a byte, so a runaway push walks past it.  Preserved.
 *
 * PopMenuNodeFromStack  0x0041ec00, __thiscall, RET 0:
 *
 *   0041ec01  MOV DL,byte [ECX+0x2001d]              depth
 *   0041ec09  AND EAX,0xff                           EAX = depth (zero-ext)
 *   0041ec0e  DEC DL                                 depth-1, NOT yet stored
 *   0041ec10  MOV AL,byte [EAX+ECX+0x2001d]          <- base 0x2001d, not 1e
 *   0041ec17  MOV byte [ECX+0x2001d],DL              store depth-1
 *   0041ec21  MOV byte [ECX+0x2021c],AL              current node = popped
 *   0041ec31  MOV DL,byte [EDX+ECX+0x1e]             saved cursor[node]
 *   0041ec35  MOV byte [ECX+0x1d],DL
 *
 * DEFECT TO PRESERVE: the load at 0x0041ec10 indexes from the DEPTH BYTE
 * ITSELF (+0x2001d), one below the stack array, using the UN-decremented
 * depth.  The two errors cancel and it reads stack[depth-1], the correct
 * element.  Both halves are transcribed exactly as they stand — "fixing"
 * either one alone changes behaviour, and fixing both is a no-op, which is
 * precisely why neither is touched.
 *
 * Also unguarded: a pop at depth 0 reads the depth byte itself as a node id
 * and underflows the depth to 0xff.  Preserved.
 *
 * RewindMenuStackToRootNode  0x0041edd0, __thiscall, RET 0:
 *
 *   depth = 0; cursor = 0; current node = 0; leave-menu flag (DWORD) = 0;
 *   then push(0)
 *
 * so it ends at depth 1 with node 0 on the stack.  The root IS an entry,
 * which is why the navigator's back-out test is "depth < 2" rather than
 * "depth == 0".
 *
 * ─── Controls ────────────────────────────────────────────────────────────
 *
 * NEITHER control can fail the suite for the right reason.  Both were run and
 * both are recorded here, because the *reasons* are the finding.
 *
 * KAROO_SIM_FX=stacktop   the pop reads stack[depth] instead of stack[depth-1]
 *                         — the correctly-based array indexed by the
 *                         un-decremented depth, so it returns one entry PAST
 *                         the top.  This is the only single-sided edit that
 *                         changes anything: correcting the base AND the index
 *                         together is a no-op (see above), so a control built
 *                         that way would prove nothing.
 *
 *                         `replaytest.py --headless` gives **16/16**.  Not
 *                         weak coverage — structural, and the diag says so:
 *                         `bridge01` makes 6 pushes, 2 pops and 2 rewinds,
 *                         maxdepth 3, and BOTH pops sit at a transition where
 *                         the popped node is immediately overwritten — one at
 *                         the load-slot commit (same millisecond as a rewind)
 *                         and one at the final teardown, 44 ms before the
 *                         end-state dump.  No asserted field ever reads what
 *                         the pop returned.  Same category as `nocostfix` and
 *                         `keepobjects`: live code, unobservable output.
 *
 * KAROO_SIM_FX=menuroot   REJECTED, and kept only as the record of why.  The
 *                         rewind seeds the cursor at 1 instead of 0, so every
 *                         menu opens on the second item.  It gives **0/16** —
 *                         which looks like the strongest control in the plan
 *                         and is worth nothing.  Every asserted field in every
 *                         recording still matches the baseline EXACTLY; the
 *                         only difference is `frames_run` +1.  Our own menu
 *                         driver (`menu.cpp`) routes by breadth-first search
 *                         from wherever the cursor actually is, so it simply
 *                         re-routes around the moved cursor and spends one
 *                         extra pulse getting there.  The recording still
 *                         loads the right slot and plays the same game.
 *
 *                         `frames_run` +1 with nothing else moving is the
 *                         SAME signature CLAUDE.md documents for a music-on
 *                         config.  A control whose whole effect is one extra
 *                         menu frame is a timing artefact, not a divergence,
 *                         and the pass column cannot tell them apart — the
 *                         `bridgeaxis` lesson in the other direction: there a
 *                         healthy 15/16 hid a crash, here a perfect 0/16 hides
 *                         a no-op.  Read the failing FIELDS, not the count.
 *
 * KAROO_MENUSTACK_DIAG=1  logs the first push, pop and rewind and then EVERY
 *                         call with the running push/pop/rewind counts and the
 *                         deepest depth reached.  Every call, not a sample:
 *                         the family is called under a dozen times in a whole
 *                         recording, which is itself the reason `stacktop`
 *                         cannot be seen.
 */

#include <windows.h>
#include <string.h>

#include "log.h"
#include "menutree.h"

/* ─── FX / diag ──────────────────────────────────────────────────────────── */

static int s_fx_menuroot = 0;
static int s_fx_stacktop = 0;
static int s_diag        = 0;
static int s_init        = 0;

static unsigned s_pushes   = 0;
static unsigned s_pops     = 0;
static unsigned s_rewinds  = 0;
static unsigned s_maxdepth = 0;
static int s_logged_push   = 0;
static int s_logged_pop    = 0;
static int s_logged_rewind = 0;

static void fx_init(void)
{
    char buf[64];
    DWORD n;

    if (s_init)
        return;
    s_init = 1;

    n = GetEnvironmentVariableA("KAROO_SIM_FX", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) {
        if (strcmp(buf, "menuroot") == 0) {
            s_fx_menuroot = 1;
            log_write("menustack: KAROO_SIM_FX=menuroot -- the rewind seeds "
                      "the cursor at 1, so every menu opens on the second "
                      "item\n");
        } else if (strcmp(buf, "stacktop") == 0) {
            s_fx_stacktop = 1;
            log_write("menustack: KAROO_SIM_FX=stacktop -- the pop reads one "
                      "entry PAST the top of the stack\n");
        }
    }

    n = GetEnvironmentVariableA("KAROO_MENUSTACK_DIAG", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "0") != 0)
        s_diag = 1;
}

static void diag_census(void)
{
    if (!s_diag)
        return;
    /* Every call, not a sampled census: the whole family is called a few
     * dozen times in a recording, so a 1-in-500 sample would report nothing.
     * That scarcity is itself the finding -- see the plan. */
    log_write("menustack: DIAG push=%u pop=%u rewind=%u maxdepth=%u\n",
              s_pushes, s_pops, s_rewinds, s_maxdepth);
}

/* ═══ 0x0041ebd0 -- Game::PushMenuNodeOnStack ══════════════════════════════ */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PushMenuNodeOnStack(MenuTree *self, unsigned int nodeArg)
{
    self->push((unsigned char)nodeArg);
}

void MenuTree::push(unsigned char node)
{
    unsigned char depth;

    fx_init();

    depth = depth_;
    stack_[depth] = node;
    /* the original re-loads the depth byte here rather than reusing it */
    depth_ = (unsigned char)(depth_ + 1);

    s_pushes++;
    if (depth_ > s_maxdepth)
        s_maxdepth = depth_;
    if (s_diag && !s_logged_push) {
        s_logged_push = 1;
        log_write("menustack: first push node=%u at depth=%u -> depth=%u\n",
                  node, depth, depth_);
    }
    diag_census();
}

/* ═══ 0x0041ec00 -- Game::PopMenuNodeFromStack ═════════════════════════════ */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PopMenuNodeFromStack(MenuTree *self)
{
    self->pop();
}

void MenuTree::pop()
{
    unsigned char depth;
    unsigned char node;

    fx_init();

    depth = depth_;
    /* base the DEPTH BYTE, index the UN-decremented depth: the preserved
     * off-by-one pair that together read stack[depth-1].  See the header. */
    node = s_fx_stacktop ? stack_[depth] : (&depth_)[depth];
    depth_  = (unsigned char)(depth - 1);
    node_   = node;
    cursor_ = savedCursor_[node];

    s_pops++;
    if (s_diag && !s_logged_pop) {
        s_logged_pop = 1;
        log_write("menustack: first pop at depth=%u -> node=%u cursor=%u "
                  "depth=%u\n", depth, node, cursor_, depth_);
    }
    diag_census();
}

/* ═══ 0x0041edd0 -- Game::RewindMenuStackToRootNode ════════════════════════ */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_RewindMenuStackToRootNode(MenuTree *self)
{
    self->rewind();
}

void MenuTree::rewind()
{
    fx_init();

    depth_  = 0;
    cursor_ = s_fx_menuroot ? 1 : 0;
    node_   = 0;
    leave_  = 0;

    s_rewinds++;
    if (s_diag && !s_logged_rewind) {
        s_logged_rewind = 1;
        log_write("menustack: first rewind -- depth/cursor/node cleared, "
                  "pushing root\n");
    }

    push(0);
}

/* ═══ NavigateMenuTree 0x0041ec40, 1 E8 site (0x00418E3D; was menunav.cpp) ═══
 *
 * ─── NavigateMenuTree(this = Game+0x175518, int now), RET 4 ─────────────
 *
 * Every key poll goes through hooks_GetAsyncKeyState: the original hoists
 * the IAT pointer into EDI (0x41EC58 / 0x41ED9E), which patch.py already
 * redirects, so calling the hook here is the same input path.
 *
 *   +0x04 = 0 (changed flag) on entry.
 *   Lock timer +0x14 set: if (unsigned)(now - __ftol(double +0x0c)) > 200
 *     clear it; poll UP, DOWN, ESC, ENTER for their side effect only (the
 *     originals discard the result -- it primes GetAsyncKeyState's "pressed
 *     since last call" bit, so the polls are kept, in order).
 *   Unlocked:
 *     ENTER (debounce != 0x0d): child = children[node][cursor];
 *       saved[node] = cursor; cursor = 0; changed = 1; push(node);
 *       node = child; debounce = 0x0d
 *     ESC   (debounce != 0x1b): depth <= 1 ? leave = 1 : (pop, changed = 1);
 *       debounce = 0x1b
 *     UP    (debounce != 0x26): cursor = cursor ? cursor-1 : count-1;
 *       debounce = 0x26; then polls LEFT and RIGHT (discarded)
 *     DOWN  (debounce != 0x28): cursor+1 < count ? cursor+1 : 0 (signed
 *       compare of cursor against count-1, JGE); debounce = 0x28; LEFT, RIGHT
 *   Finally debounce is cleared when its own key reads up.
 *
 *   NOTE the ENTER path's `changed = 1` store is EBP, set to 1 on the ENTER
 *   branch AND on its fall-through -- so ESC's `changed`/`leave` stores are
 *   always 1.  Nothing here depends on it, but the listing does it that way.
 *
 *
 * Control: KAROO_SIM_FX=menuwrap -- DOWN at the last entry stays put instead
 * of wrapping to 0.  A navigation change: a recording that wraps the menu
 * would land on a different node.
 */
extern "C" __declspec(dllexport) SHORT WINAPI hooks_GetAsyncKeyState(int vKey);

#define KEY(k)  hooks_GetAsyncKeyState(k)

static int s_fx_menuwrap = -1;

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_NavigateMenuTree(MenuTree *self, int now)
{
    self->navigate(now);
}

void MenuTree::navigate(int now)
{
#define CHANGED changed_
#define LOCK    lock_
#define LEAVE   leave_
#define DEB     lastKey_
#define CUR     cursor_
#define DEPTH   depth_
#define NODE    node_
#define COUNT(n) childCount_[n]

    if (s_fx_menuwrap < 0) {
        char e[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", e, sizeof(e));
        s_fx_menuwrap = (n > 0 && n < sizeof(e) && strcmp(e, "menuwrap") == 0);
        if (s_fx_menuwrap)
            log_write("menunav: KAROO_SIM_FX=menuwrap -- DOWN does not wrap\n");
    }

    CHANGED = 0;
    if (LOCK != 0) {
        unsigned int t = (unsigned int)(long long)lockStart_;
        if ((unsigned int)now - t > 200)
            LOCK = 0;
        KEY(0x26);
        KEY(0x28);
        KEY(0x1b);
        KEY(0x0d);
    } else {
        if (DEB != 0x0d && KEY(0x0d) != 0) {
            unsigned char child = children_[NODE * CHILD_STRIDE + CUR];
            savedCursor_[NODE] = CUR;
            CUR = 0;
            CHANGED = 1;
            push(NODE);
            NODE = child;
            DEB = 0x0d;
        }
        if (DEB != 0x1b && KEY(0x1b) != 0) {
            if (DEPTH > 1) {
                pop();
                CHANGED = 1;
            } else {
                LEAVE = 1;
            }
            DEB = 0x1b;
        }
        if (DEB != 0x26 && KEY(0x26) != 0) {
            if (CUR > 0)
                CUR = (unsigned char)(CUR - 1);
            else
                CUR = (unsigned char)(COUNT(NODE) - 1);
            DEB = 0x26;
            KEY(0x25);
            KEY(0x27);
        }
        if (DEB != 0x28 && KEY(0x28) != 0) {
            if ((int)CUR < (int)COUNT(NODE) - 1)
                CUR = (unsigned char)(CUR + 1);
            else if (!s_fx_menuwrap)
                CUR = 0;
            DEB = 0x28;
            KEY(0x25);
            KEY(0x27);
        }
    }
    if (KEY(DEB) == 0)
        DEB = 0;
#undef CHANGED
#undef LOCK
#undef LEAVE
#undef DEB
#undef CUR
#undef DEPTH
#undef NODE
#undef COUNT
}

