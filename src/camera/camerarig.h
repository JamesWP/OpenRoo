/* CameraRig: the camera, owned by Game.  It holds the camera's intent, which
 * simulation, the menus, the script player and checkpoints set, and its live
 * pose, which render's UpdateViewTransform eases every frame and which the
 * listener and the script player read.
 *
 * SetupLevelObjects (levelsetup.cpp) copies the camera eye into the pose's
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

    /* 0 = follow the player; nonzero = view from the separate eye,
     * cameraEye() (FramePose_Player / UpdateViewTransform), and 2 also spins
     * the yaw -- the menus and the tally.  Checkpoints restore it. */
    unsigned char  cameraMode() const                { return cameraMode_; }
    void           setCameraMode(unsigned char m)    { cameraMode_ = m; }
    /* The distance the camera eases towards (UpdateViewTransform subtracts
     * the current eye distance and closes a dt-scaled share of the gap):
     * 7.0 by default, 40.0 in CameraOverview, animated by the sway. */
    float          cameraDistance() const            { return cameraDistance_; }
    void           setCameraDistance(float d)        { cameraDistance_ = d; }
    /* Where GameTick parks cameraTurnsWithPlayer while gliding forces
     * it on: the player's choice + 10, so 0 means "nothing parked".  Load
     * zeroes it. */
    unsigned char  parkedCameraOption() const        { return parkedCameraOption_; }
    void           setParkedCameraOption(unsigned char v) { parkedCameraOption_ = v; }
    /* The separate eye the camera views from when cameraMode() is nonzero
     * (FramePose_Player makes it the camera's focus, z negated);
     * checkpoints restore it from the script player. */
    void           setCameraEye(int i, float v)      { cameraEye_[i] = v; }
    float          cameraEye(int i) const            { return cameraEye_[i]; }
    /* A float vector RenderGameFrame's scripted-camera branch (taken
     * instead of UpdateViewTransform while the script player's spline is active) reads beside
     * the eye; checkpoints restore it from the script player's spline point,
     * and the level builder seeds it {0, 1000, 0}.  Not decoded. */
    void           setField13cc94(int i, float v)    { field_13cc94_[i] = v; }
    float          field13cc94(int i) const          { return field_13cc94_[i]; }
    /* zoomDistance is the zoom distance the Zoom In/Out actions step (clamped
     * 2..20); GameTick eases cameraDistance towards it and copies it back
     * when the overview ends.  overviewActive is the overview flag the OverView
     * action raises (with cameraDistance 40); GameTick and the level
     * builder clear it.  field_13cc90 is set by both zoom actions and cleared
     * by the same two; PRESERVED: nothing reads it. */
    void           setField13cc90(int v)             { field_13cc90_ = v; }
    float          zoomDistance() const              { return zoomDistance_; }
    void           setZoomDistance(float d)          { zoomDistance_ = d; }
    int            overviewActive() const            { return overviewActive_; }
    void           setOverviewActive(int v)          { overviewActive_ = v; }

private:
    CameraPose     pose_;
    unsigned char  cameraMode_{};
    float          cameraDistance_{};
    float          cameraEye_[3]{};
    float          field_13cc94_[3]{};
    int            field_13cc90_{};
    float          zoomDistance_{};
    int            overviewActive_{};
    unsigned char  parkedCameraOption_{};
};
