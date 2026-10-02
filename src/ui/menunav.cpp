#include <windows.h>
#include <string.h>
#include "logger.h"
#include "game.h"
#include "menunav.h"
#include "player.h"
#include "menutree.h"

  unsigned int  
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

  unsigned int  
Sim_RestoreGameStateFromSaveSlot(Game *self, unsigned int slotArg)
{
    SaveSlot *S = self->saveSlots()->slot((unsigned char)slotArg);

    self->setLevelIndex(S->levelIndex);
    // DETERMINISM: the stored time is unsigned; widened exactly.
    self->setTotalPlayTime((double)(unsigned long long)S->elapsedGameTime);
    self->player()->setCompletionNumerator(S->completionNumerator);
    self->player()->setScore(S->totalScore);
    self->player()->setLives(S->livesRemaining);
    return 1;
}
