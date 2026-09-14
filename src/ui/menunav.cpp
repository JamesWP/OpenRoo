/* GAMETICK_PLAN.md Band B reopened — HandleKeypress's own leaves.
 *
 *   NavigateMenuTree              0x0041ec40  1 E8 site (0x00418E3D)
 *   StoreGameStateIntoSaveSlot    0x00419de0  1 E8 site (0x00419ABE)
 *   RestoreGameStateFromSaveSlot  0x00419e50  1 E8 site (0x004199A0)
 *
 * All three are called only from HandleKeypress.  Transcribed from the
 * LISTINGS.  menu.cpp DRIVES the navigator (it synthesises the key presses)
 * but holds no absolute-address call to it -- checked before stubbing.
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
 * ─── The save-slot pair (slot stride 0x2a, table at Game+0x170aad) ──────
 *
 *   Store: +0x170ac1 = level+1 (byte); +0x170acb = __ftol(double +0x170a44);
 *          +0x170ac7 = +0x1752a1; +0x170ac3 = +0x1753f5;
 *          +0x170ac2 = low byte of lives; +0x170acf = 1.  Returns EAX = 1.
 *   Restore: level = +0x170ac1 (not decremented: the saver stored level+1);
 *          +0x170a44 = (double) of the UNSIGNED dword (FILD of a zero-extended
 *          qword, exact); +0x1752a1, +0x1753f5; lives = zero-extended byte.
 *          Returns AL = 1 over the slot address.
 *
 * Control: KAROO_SIM_FX=menuwrap -- DOWN at the last entry stays put instead
 * of wrapping to 0.  A navigation change: a recording that wraps the menu
 * would land on a different node.
 */
#include <windows.h>
#include <string.h>
#include "log.h"
#include "game.h"
#include "menunav.h"
#include "player.h"
#include "menutree.h"

extern "C" __declspec(dllexport) SHORT WINAPI hooks_GetAsyncKeyState(int vKey);

#define KEY(k)  hooks_GetAsyncKeyState(k)

static int s_fx = -1;

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

    if (s_fx < 0) {
        char e[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "menuwrap") == 0);
        if (s_fx)
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
            else if (!s_fx)
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

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_StoreGameStateIntoSaveSlot(Game *self, unsigned int slotArg)
{
    Game     *game = (Game *)self;
    SaveSlot *S    = game->saveSlots()->slot((unsigned char)slotArg);

    S->levelIndex          = (unsigned char)(game->levelIndex() + 1);
    S->elapsedGameTime     = (unsigned int)(long long)game->totalPlayTime();
    S->completionNumerator = (unsigned int)game->player()->fieldD8();
    S->totalScore          = (unsigned int)game->player()->field22c();
    S->livesRemaining      = (unsigned char)game->player()->field239();
    S->inUse               = 1;
    return 1;
}

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_RestoreGameStateFromSaveSlot(Game *self, unsigned int slotArg)
{
    unsigned char *B = (unsigned char *)self;
    SaveSlot *S = ((Game *)B)->saveSlots()->slot((unsigned char)slotArg);

    ((Game *)B)->setLevelIndex(S->levelIndex);
    ((Game *)B)->setTotalPlayTime((double)(unsigned long long)S->elapsedGameTime);
    *(unsigned int *)(B + 0x1752a1) = S->completionNumerator;
    *(unsigned int *)(B + 0x1753f5) = S->totalScore;
    *(unsigned int *)(B + 0x175402) = S->livesRemaining;
    /* EAX's low byte is 1; the rest is what the original left in it:
     * Game + slot*0x2a, the base it indexed every field from. */
    return ((unsigned int)(unsigned long)(B + (slotArg & 0xff) * 0x2a) & 0xffffff00u) | 1u;
}
