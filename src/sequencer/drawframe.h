/* The draw half of a frame (drawframe.cpp): the order everything is drawn
 * in.  core's frame driver calls it after the update. */
#pragma once

class Game;
class Renderer;
struct MenuArt;
class ProgableControl;

/* The frame's times, in clock milliseconds: now, the time since the clock
 * last advanced, and the time since the last GameTick. */
struct FrameTime {
    double now;
    double elapsed;
    double dt;
};

/* Draws the frame from the state the update left. */
void Seq_DrawFrame(Game *g, Renderer *renderer, const MenuArt *art, ProgableControl *progCtrl,
                   const FrameTime &t);
