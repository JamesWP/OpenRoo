/* camera.h -- the orbit camera's update (camera.cpp).  The camera's state is
 * the CameraRig (camerarig.h); this file only eases its pose.
 *
 * Camera_UpdateViewTransform eases the pose's `target` towards the player every
 * frame and places `eye` at target + RotX(-pitch).RotY(yaw) applied to
 * (0, 0, -distance).  yaw and pitch are in radians; pitch is pi/3 after the
 * level entry and clamped at 1.569051.
 */
#pragma once

 

struct Mat4;
class RenderDevice;
class World;
class CameraRig;
class Config;
class Scene;

/* The 9-float block FramePose_Player fills and RenderGameFrame passes BY
 * VALUE to UpdateViewTransform: [5] is the yaw the camera turns
 * towards, [6..8] the point `target` follows.  [0..4] are not read here. */
struct CameraFocus { float f[9]; };

/* cdecl(d3d, world, camera, config, focus BY VALUE, double dt): eases the
 * camera's pose (camera->pose()) and sets the VIEW transform.  One caller,
 * RenderGameFrame. */
void Camera_UpdateViewTransform(RenderDevice *d3d, World *world, CameraRig *camera,
                                const Config *config, Scene *scene, CameraFocus focus, double dt);


 
/* cdecl(out, eye, at, up by value, roll) -> out: a left-handed
 * LookAt (x = up x fwd, y = fwd x x, z = fwd, each normalised), then
 * * RotZ(-roll) when roll != 0. */
  Mat4 * 
Camera_BuildLookAt(Mat4 *out, float ex, float ey, float ez,
                   float ax, float ay, float az,
                   float ux, float uy, float uz, float roll);

