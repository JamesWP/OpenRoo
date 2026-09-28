/* The end-of-level score. */
#pragma once

class Game;

/* Computes the six tally components (count and score of each) and the level
 * and total scores.  endReason 3 is a completed level, the only one that earns
 * the time bonus. */
extern "C" __declspec(dllexport) void  
Score_CalculateLevelScore(Game *self, char endReason);
