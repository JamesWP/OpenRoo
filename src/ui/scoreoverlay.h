/* The score overlays: the level-complete screen, the game-over score and the
 * high-score table. */
#pragma once
#include <windows.h>

class Game;
class RenderDevice;
class TextRenderer;

/* The level-complete screen (menu node 0x28).  game is the theme argument the
 * screen dispatch passes to every screen. */
void Menu_RenderLevelComplete(Game *g, void *game, RenderDevice *d3d,
                              TextRenderer *text, DWORD ms);

/* The in-game overlays the frame renderer draws. */
void Score_DrawHighScoreTable(Game *g, void *game, RenderDevice *d3d,
                              TextRenderer *text, int n);
void Score_DrawGameOverScore(Game *g, void *game, RenderDevice *d3d,
                             TextRenderer *text, int n);
