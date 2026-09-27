/* The score overlays: the level-complete screen, the game-over score and the
 * high-score table. */
#pragma once
#include <windows.h>

class Game;
class RenderDevice;
class TextRenderer;
class ThemeAssetBlock;

/* The level-complete screen (menu node 0x28). */
void Menu_RenderLevelComplete(Game *g, ThemeAssetBlock *theme, RenderDevice *d3d,
                              TextRenderer *text, DWORD ms);

/* The in-game overlays the frame renderer draws. */
void Score_DrawHighScoreTable(Game *g, ThemeAssetBlock *theme, RenderDevice *d3d,
                              TextRenderer *text, int n);
void Score_DrawGameOverScore(Game *g, ThemeAssetBlock *theme, RenderDevice *d3d,
                             TextRenderer *text, int n);
