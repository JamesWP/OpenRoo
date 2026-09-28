/* The per-frame pose builders (framepose.cpp), called by RenderGameFrame
 * before it places the camera and draws the actors: the player's pose into the
 * CameraFocus block (camera.h), and one FoePose per live foe.  Neither reads
 * t. */

#pragma once

#include <windows.h>
#include "layout.h"

class Game;
struct CameraFocus;

/* One foe's pose, 0x1d bytes. */
class __attribute__((packed)) FoePose {
public:
    static const int ORIGIN = 0;

    /* The pose FramePose_Foes computes for one foe. */
    void set(unsigned char kind, float u, float h, float negV, float yaw)
    {
        kind_ = kind;
        pos_[0] = u; pos_[1] = h; pos_[2] = negV;
        rotX_ = 0.0f; rotY_ = yaw; rotZ_ = 0.0f;
    }
    void setStepFrac(float f) { stepFrac_ = f; }

    unsigned char kind() const     { return kind_; }
    float         stepFrac() const { return stepFrac_; }
    /* (U, H, -V), and the rotation triple (X, Y, Z) that follows it. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    const float  *pos() const      { return pos_; }
    const float  *rot() const      { return &rotX_; }
#pragma GCC diagnostic pop

private:
    unsigned char kind_;  // the foe's kind
    float  pos_[3];       // (U, H, -V)
    float  rotX_;         // always 0
    float  rotY_;         // facing, plus any turn in progress
    float  rotZ_;         // always 0
    float  stepFrac_;     // written only while stepping; stale otherwise

    KAROO_LAYOUT_REGISTER(FoePose);
};

KAROO_LAYOUT_CHECKS(FoePose)
{
    KAROO_LAYOUT_AT(pos_,      0x01);
    KAROO_LAYOUT_AT(rotX_,     0x0d);
    KAROO_LAYOUT_AT(rotY_,     0x11);
    KAROO_LAYOUT_AT(stepFrac_, 0x19);
    KAROO_LAYOUT_SIZE(0x1d);
}

/* One record per live foe, in foe-list order; RenderGameFrame draws from it.
 */
extern FoePose g_foePoses[500];

extern "C" {
__declspec(dllexport) void __cdecl
FramePose_Player(Game *g, double t, double dt, CameraFocus *out);
__declspec(dllexport) void __cdecl
FramePose_Foes(Game *g, double t, double dt, FoePose *out);
}
