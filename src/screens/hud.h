/* hud.h -- the in-level HUD, the frame counter, the captions and the logo
 * (hud.cpp), drawn over the scene by RenderGameFrame. */
#pragma once

class World;
class Session;
class ScriptPlayer;

/* The side panels, vitality needle, radar, timer and counters, in play. */
void Hud_Draw(World *world, Session *session, unsigned char state, unsigned w, unsigned h,
              float W, float H, float hudH);

/* The frame counter, while F1 is held. */
void Hud_DrawFps(float W, float H);

/* The captions and the timed-effect icons down the left edge. */
void Hud_DrawMessages(World *world, ScriptPlayer *scriptPlayer, unsigned char state, float W,
                      float H, float pad);

/* The Ka'roo logo, on the menus only. */
void Hud_DrawLogo(unsigned char state, float H, float hudH, float pad);
