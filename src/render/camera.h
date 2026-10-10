/* camera.h -- the orbit camera's update (camera.cpp).  The camera's state is
 * the CameraRig Game owns (camerarig.h); this file only eases its pose.
 *
 * Camera_UpdateViewTransform eases the pose's `target` towards the player every
 * frame and places `eye` at target + RotX(-pitch).RotY(yaw) applied to
 * (0, 0, -distance).  yaw and pitch are in radians; pitch is pi/3 after the
 * level entry and clamped at 1.569051.
 */
#pragma once

 

struct Mat4;
class RenderDevice;
class Game;

/* The 9-float block FramePose_Player fills and RenderGameFrame passes BY
 * VALUE to UpdateViewTransform: [5] is the yaw the camera turns
 * towards, [6..8] the point `target` follows.  [0..4] are not read here. */
struct CameraFocus { float f[9]; };

/* cdecl(d3d, game, focus BY VALUE, double dt): eases the game's camera pose
 * (g->camera()->pose()) and sets the VIEW transform.  One caller, RenderGameFrame. */
void Camera_UpdateViewTransform(RenderDevice *d3d, Game *g, CameraFocus focus,
                                double dt);


/* The level entry zeroes it; FramePose_Player fills it every frame. */
extern CameraFocus g_cameraFocus;

 
/* cdecl(out, eye, at, up by value, roll) -> out: a left-handed
 * LookAt (x = up x fwd, y = fwd x x, z = fwd, each normalised), then
 * * RotZ(-roll) when roll != 0. */
  Mat4 * 
Camera_BuildLookAt(Mat4 *out, float ex, float ey, float ez,
                   float ax, float ay, float az,
                   float ux, float uy, float uz, float roll);

