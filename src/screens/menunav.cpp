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
    SaveSlot *S    = game->frontEnd()->saveSlots()->slot((unsigned char)slotArg);

    // FORMAT: the slot stores level + 1, and restore does not subtract it
    // back: the index loads as stored.
    S->levelIndex          = (unsigned char)(game->session()->levelIndex() + 1);
    S->elapsedGameTime     = (unsigned int)(long long)game->session()->totalPlayTime();
    S->completionNumerator = (unsigned int)game->world()->player()->completionNumerator();
    S->totalScore          = (unsigned int)game->world()->player()->score();
    S->livesRemaining      = (unsigned char)game->world()->player()->lives();
    S->inUse               = 1;
    return 1;
}

  unsigned int  
Sim_RestoreGameStateFromSaveSlot(Game *self, unsigned int slotArg)
{
    SaveSlot *S = self->frontEnd()->saveSlots()->slot((unsigned char)slotArg);

    self->session()->setLevelIndex(S->levelIndex);
    // DETERMINISM: the stored time is unsigned; widened exactly.
    self->session()->setTotalPlayTime((double)(unsigned long long)S->elapsedGameTime);
    self->world()->player()->setCompletionNumerator(S->completionNumerator);
    self->world()->player()->setScore(S->totalScore);
    self->world()->player()->setLives(S->livesRemaining);
    return 1;
}
