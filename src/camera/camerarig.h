/* CameraRig: the camera, owned by Game.  It holds the camera's intent, which
 * simulation, the menus, the script player and checkpoints set, and its live
 * pose, which render's UpdateViewTransform eases every frame and which the
 * listener and the script player read.
 *
 * SetupLevelObjects (levelsetup.cpp) copies the focus into the pose's
 * target and sets its eye to (0, 1000, 0), the other way round from the
 * names in CameraPose; the level entry overwrites both one frame later.
 */
#pragma once

/* The live pose: `eye` and `target` as UpdateViewTransform (render/camera.cpp)
 * leaves them, and the yaw and pitch it eases, in radians. */
class CameraPose {
public:
    float *eye() { return eye_; }
    const float *eye() const { return eye_; }

    float *target() { return target_; }
    const float *target() const { return target_; }

    float yaw() const { return yaw_; }
    void  setYaw(float y) { yaw_ = y; }
    float pitch() const { return pitch_; }
    void  setPitch(float p) { pitch_ = p; }

private:
    float  eye_[3]{};
    float  target_[3]{};
    float  yaw_{};
    float  pitch_{};
};

class CameraRig {
public:
    CameraPose *pose() { return &pose_; }
    const CameraPose *pose() const { return &pose_; }

    /* 0 = follow the player (the camera's focus is the player); nonzero =
     * the camera's focus is the separate point focus() (FramePose_Player /
     * UpdateViewTransform); 2 also spins the yaw -- the menus and the tally.
     * Checkpoints restore it. */
    unsigned char  mode() const                      { return mode_; }
    void           setMode(unsigned char m)          { mode_ = m; }
    /* The distance the camera eases towards (UpdateViewTransform subtracts
     * the current eye distance and closes a dt-scaled share of the gap):
     * 7.0 by default, 40.0 in CameraOverview, animated by the sway. */
    float          distance() const                  { return distance_; }
    void           setDistance(float d)              { distance_ = d; }
    /* Where GameTick parks cameraTurnsWithPlayer while gliding forces
     * it on: the player's choice + 10, so 0 means "nothing parked".  Load
     * zeroes it. */
    unsigned char  parkedCameraOption() const        { return parkedCameraOption_; }
    void           setParkedCameraOption(unsigned char v) { parkedCameraOption_ = v; }
    /* The point the camera looks at and follows when mode() is nonzero
     * (FramePose_Player copies it into the camera focus, z negated, and
     * UpdateViewTransform eases the pose target to it); the level builder,
     * the player's falls and the checkpoints set it. */
    void           setFocus(int i, float v)          { focus_[i] = v; }
    float          focus(int i) const                { return focus_[i]; }
    /* The eye of the scripted camera: RenderGameFrame's scripted-camera
     * branch (taken instead of UpdateViewTransform while the script player's
     * spline is active) views from it at focus().  The script player's spline
     * starts from it ("splinexyz"), checkpoints restore it from the spline
     * point, and the level builder seeds it {0, 1000, 0}. */
    void           setScriptedEye(int i, float v)    { scriptedEye_[i] = v; }
    float          scriptedEye(int i) const          { return scriptedEye_[i]; }
    /* zoomDistance is the zoom distance the Zoom In/Out actions step (clamped
     * 2..20); GameTick eases distance towards it and copies it back
     * when the overview ends.  overviewActive is the overview flag the OverView
     * action raises (with distance 40); GameTick and the level
     * builder clear it. */
    float          zoomDistance() const              { return zoomDistance_; }
    void           setZoomDistance(float d)          { zoomDistance_ = d; }
    int            overviewActive() const            { return overviewActive_; }
    void           setOverviewActive(int v)          { overviewActive_ = v; }

private:
    CameraPose     pose_;
    unsigned char  mode_{};
    float          distance_{};
    float          focus_[3]{};
    float          scriptedEye_[3]{};
    float          zoomDistance_{};
    int            overviewActive_{};
    unsigned char  parkedCameraOption_{};
};
