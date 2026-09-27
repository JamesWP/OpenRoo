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

    // FORMAT: the slot stores level + 1, and restore does not subtract it
    // back: the index loads as stored.
    S->levelIndex          = (unsigned char)(game->levelIndex() + 1);
    S->elapsedGameTime     = (unsigned int)(long long)game->totalPlayTime();
    S->completionNumerator = (unsigned int)game->player()->completionNumerator();
    S->totalScore          = (unsigned int)game->player()->score();
    S->livesRemaining      = (unsigned char)game->player()->lives();
    S->inUse               = 1;
    return 1;
}

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_RestoreGameStateFromSaveSlot(Game *self, unsigned int slotArg)
{
    SaveSlot *S = self->saveSlots()->slot((unsigned char)slotArg);

    self->setLevelIndex(S->levelIndex);
    // DETERMINISM: the stored time is unsigned; widened exactly.
    self->setTotalPlayTime((double)(unsigned long long)S->elapsedGameTime);
    self->player()->setCompletionNumerator(S->completionNumerator);
    self->player()->setScore(S->totalScore);
    self->player()->setLives(S->livesRemaining);
    // The low byte is 1; the rest is what the game left there, the slot's base
    // address, kept as byte arithmetic.
    return ((unsigned int)(unsigned long)((unsigned char *)self + (slotArg & 0xff) * 0x2a)
            & 0xffffff00u) | 1u;
}
