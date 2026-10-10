/* The score overlays: the level-complete screen, the game-over score and the
 * high-score table. */
#pragma once
#include <stdint.h>

struct MenuArt;
class FrontEnd;
class Session;
class RenderDevice;
class TextRenderer;
class ThemeAssetBlock;

/* The level-complete screen (menu node 0x28). */
void Menu_RenderLevelComplete(const MenuArt *art, const FrontEnd *frontEnd, const Session *session,
                              ThemeAssetBlock *theme, RenderDevice *d3d, TextRenderer *text,
                              uint32_t ms);

/* The in-game overlays the frame renderer draws. */
void Score_DrawHighScoreTable(const MenuArt *art, const FrontEnd *frontEnd, ThemeAssetBlock *theme,
                              RenderDevice *d3d,
                              TextRenderer *text, int n);
void Score_DrawGameOverScore(const Session *session, ThemeAssetBlock *theme, RenderDevice *d3d,
                             TextRenderer *text, int n);
