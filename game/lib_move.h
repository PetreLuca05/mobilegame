/* LIB-1 step 6: Maps / Back / New Game as one camera move between the library (menu) camera and the map
 * camera, no cut. Pure maths: the caller draws. With t = elapsed / duration and e = t^3 (t (6t - 15) + 10):
 *   distance(e) = exp(lerp(ln d0, ln d1, e)) * (1 + hump * sin(pi e))
 *   pitch, yaw, fov: lerp by e (yaw along the shorter arc)
 *   target = lerp(T0, T1, w(e)), w(e) = integral 0..e of distance / integral 0..1 (65 samples, built at start):
 *            the camera rises from the table first and crosses the island while it is high
 *   near = 0.05 x distance, far = 6 x distance + the island's size
 *   shell k = smoothstep((e - f0) / (f1 - f0))
 * Towards the map e runs 0 -> 1; Back runs the same path with e 1 -> 0. Camera poses are world space,
 * yaw 0 = the eye on the +z side looking at -z (the map's "from the south"). */
#ifndef MG_LIB_MOVE_H
#define MG_LIB_MOVE_H

#include <stdbool.h>

#include "core/mg_types.h"

#define LIB_MOVE_SAMPLES 65

typedef struct LibCamPose {
    HMM_Vec3 target;
    float dist, pitch_deg, yaw_deg, fov_deg;
} LibCamPose;

typedef struct LibMove {
    bool active, to_map;
    float t, duration, hump, shell_f0, shell_f1;
    LibCamPose a, b; /* e = 0 (the library), e = 1 (the map) */
    float w[LIB_MOVE_SAMPLES];
} LibMove;

void lib_move_begin(LibMove *mv, LibCamPose library, LibCamPose map, float duration, float hump, float f0, float f1, bool to_map);
/* Advances t; true on the frame it reaches the end (active goes false). */
bool lib_move_step(LibMove *mv, float dt);
/* e for the current t (towards the map, or reversed for Back) */
float lib_move_e(const LibMove *mv);
LibCamPose lib_move_pose(const LibMove *mv, float e);
float lib_move_shell(const LibMove *mv, float e);
/* view, projection and eye of a pose; `island` = the island's size for the far plane */
void lib_cam_matrices(const LibCamPose *c, float aspect, float island, HMM_Mat4 *view, HMM_Mat4 *proj, HMM_Vec3 *eye);

#endif
