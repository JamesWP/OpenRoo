/* The camera's focus on the player, worked out every frame before the camera
 * moves (framepose.cpp). */

#pragma once

class World;
class CameraRig;
class Config;
struct CameraFocus;

/* The player's pose into the CameraFocus block (camera.h): its step
 * fraction, yaw and position, the yaw the camera turns towards and the point
 * it follows.  dt is the frame's tick, ms. */
void FramePose_Player(const World *world, const CameraRig *camera, const Config *config,
                      double dt, CameraFocus *out);
