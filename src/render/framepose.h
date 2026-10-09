/* The per-frame pose builders (framepose.cpp), called by RenderGameFrame
 * before it places the camera and draws the actors: the player's pose into the
 * CameraFocus block (camera.h), and one FoePose per live foe.  Neither reads
 * t. */

#pragma once

 

class Game;
struct CameraFocus;

/* One foe's pose, 0x1d bytes. */
class FoePose {
public:
     

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
 
 
    const float  *pos() const      { return pos_; }
    const float  *rot() const      { return &rotX_; }
 

private:
    unsigned char kind_;  // the foe's kind
    float  pos_[3];       // (U, H, -V)
    float  rotX_;         // always 0
    float  rotY_;         // facing, plus any turn in progress
    float  rotZ_;         // always 0
    float  stepFrac_;     // written only while stepping; stale otherwise

     
};

 
/* One record per live foe, in foe-list order; RenderGameFrame draws from it.
 */
extern FoePose g_foePoses[500];

 
  void  
FramePose_Player(Game *g, double t, double dt, CameraFocus *out);
  void  
FramePose_Foes(Game *g, double t, double dt, FoePose *out);
