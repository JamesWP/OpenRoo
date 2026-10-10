/* The top-level game state: which screen or mode the game is in.  Game holds
 * it (game.h); the screens take it as a parameter, so it lives in a header
 * below both.  The values are the original's; the save files and the replay
 * state dump see them as numbers.
 */
#pragma once

enum class GameState : unsigned char {
    MainMenu      = 0,
    Playing       = 1,
    GameOver      = 2,
    LevelComplete = 3,
    Flythrough    = 4,   // the level's intro flythrough and its "press enter" screen
    PauseMenu     = 5,   // the menu ESC opens over a level being played or flown through
    NameEntry     = 6,   // high-score name entry
    Quitting      = 7,
};
