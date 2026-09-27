/* The per-frame pose builders (framepose.cpp), called by RenderGameFrame
 * before it places the camera and draws the actors: the player's pose into the
 * CameraFocus block (camera.h), and one FoePose per live foe.  Neither reads
 * t. */

#pragma once

#include <windows.h>

class Game;
struct CameraFocus;

/* One foe's pose. */
struct FoePose {
    unsigned char kind;  // the foe's kind
    float  pos[3];       // (U, H, -V)
    float  rot[3];       // (0, facing plus any turn in progress, 0)
    float  stepFrac;     // written only while stepping; stale otherwise
};

/* One record per live foe, in foe-list order; RenderGameFrame draws from it.
 */
extern FoePose g_foePoses[500];

void FramePose_Player(Game *g, double t, double dt, CameraFocus *out);
void FramePose_Foes(Game *g, double t, double dt, FoePose *out);
