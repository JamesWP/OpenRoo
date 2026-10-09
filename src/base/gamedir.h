/* The install directory the game's files are read from: set once at startup,
 * the root every game path is formatted against. */
#pragma once

const char *gameDir();
void        setGameDir(const char *dir);  // truncated to fit
