/* hud.h -- the in-level HUD, the frame counter, the captions and the logo
 * (hud.cpp), drawn over the scene by RenderGameFrame. */
#pragma once
#include "playstate.h"

class World;
class Session;
class ScriptPlayer;
class Renderer;
class ThemeAssetBlock;
class RenderDevice;
class TextRenderer;
class Texture;

/* The side panels, vitality needle, radar, timer and counters, in play. */
void Hud_Draw(Renderer *renderer, const World *world, Session *session, GameState state,
              unsigned w, unsigned h, float W, float H, float hudH);

/* The frame counter, while F1 is held. */
void Hud_DrawFps(RenderDevice *d3d, TextRenderer *font, float W, float H);

/* The captions and the timed-effect icons down the left edge. */
void Hud_DrawMessages(RenderDevice *d3d, TextRenderer *font, const ThemeAssetBlock *theme,
                      const World *world, ScriptPlayer *scriptPlayer, GameState state, float W, float H, float pad);

/* The Ka'roo logo, on the menus only. */
void Hud_DrawLogo(RenderDevice *d3d, Texture *logo, GameState state, float H, float hudH, float pad);
