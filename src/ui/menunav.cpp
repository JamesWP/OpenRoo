/* GAMETICK_PLAN.md Band B reopened — HandleKeypress's own leaves.
 *
 *   StoreGameStateIntoSaveSlot    0x00419de0  1 E8 site (0x00419ABE)
 *   RestoreGameStateFromSaveSlot  0x00419e50  1 E8 site (0x004199A0)
 *
 * Both are called only from HandleKeypress.  Transcribed from the
 * LISTINGS.  menu.cpp DRIVES the navigator (it synthesises the key presses)
 * but holds no absolute-address call to it -- checked before stubbing.
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
 *
 * NavigateMenuTree is MenuTree::navigate, in menutree.cpp.
 */
#include <windows.h>
#include <string.h>
#include "log.h"
#include "game.h"
#include "menunav.h"
#include "player.h"
#include "menutree.h"

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_StoreGameStateIntoSaveSlot(Game *self, unsigned int slotArg)
{
    Game     *game = (Game *)self;
    SaveSlot *S    = game->saveSlots()->slot((unsigned char)slotArg);

    S->levelIndex          = (unsigned char)(game->levelIndex() + 1);
    S->elapsedGameTime     = (unsigned int)(long long)game->totalPlayTime();
    S->completionNumerator = (unsigned int)game->player()->fieldD8();
    S->totalScore          = (unsigned int)game->player()->score();
    S->livesRemaining      = (unsigned char)game->player()->lives();
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
