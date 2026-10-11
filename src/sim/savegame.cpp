#include "savegame.h"
#include "frontend.h"
#include "saveslots.h"
#include "session.h"
#include "world.h"
#include "player.h"

  unsigned int  
SaveGame_StoreIntoSaveSlot(FrontEnd *frontEnd, const Session *session, const World *world,
                           unsigned int slotArg)
{
    SaveSlot *S    = frontEnd->saveSlots()->slot((unsigned char)slotArg);

    // FORMAT: the slot stores level + 1, and restore does not subtract it
    // back: the index loads as stored.
    S->levelIndex          = (unsigned char)(session->levelIndex() + 1);
    S->elapsedGameTime     = (unsigned int)(long long)session->totalPlayTime();
    S->completionNumerator = (unsigned int)world->player()->completionNumerator();
    S->totalScore          = (unsigned int)world->player()->score();
    S->livesRemaining      = (unsigned char)world->player()->lives();
    S->inUse               = 1;
    return 1;
}

  unsigned int  
SaveGame_RestoreFromSaveSlot(FrontEnd *frontEnd, Session *session, World *world,
                             unsigned int slotArg)
{
    SaveSlot *S = frontEnd->saveSlots()->slot((unsigned char)slotArg);

    session->setLevelIndex(S->levelIndex);
    // DETERMINISM: the stored time is unsigned; widened exactly.
    session->setTotalPlayTime((double)(unsigned long long)S->elapsedGameTime);
    world->player()->setCompletionNumerator(S->completionNumerator);
    world->player()->setScore(S->totalScore);
    world->player()->setLives(S->livesRemaining);
    return 1;
}
