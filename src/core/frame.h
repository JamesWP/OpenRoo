/* One frame of the game: the app's message loop calls Core_RunFrame whenever
 * no movie is playing.  A frame is an update, then a draw of what the update
 * left:
 *
 *   update  a pending level entry (prepared, and the frame ends there); the
 *           clock; when time has passed, GameTick, the camera's focus on the
 *           player, the camera and the 3D sound listener;
 *   draw    the sequencer's (sequencer/drawframe.h).
 */
#pragma once
#include "drawframe.h"

class Game;
class Renderer;
struct MenuArt;
class ProgableControl;

/* The whole frame, inside the debug UI's frame and the profiler's "frame"
 * scope. */
void Core_RunFrame(Game *g, Renderer *renderer, const MenuArt *art, ProgableControl *progCtrl);

/* The update half.  False when the frame draws nothing: it prepared a level
 * entry, or the tick started one. */
bool Core_UpdateFrame(Game *g, Renderer *renderer, ProgableControl *progCtrl, FrameTime *t);
