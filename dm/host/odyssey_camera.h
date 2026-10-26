// Exact-build touch camera for Odyssey 1.4.1: swipes turn the follow camera
// directly instead of through the right stick's dead zone and smoothing.
#pragma once
#include <stdint.h>

void dm_odyssey_camera_install(void);
// 1 while a follow camera has updated within DM_ODYSSEY_CAMERA_LIVE_NS.
int dm_odyssey_camera_live(void);
// Queue a turn in degrees, signed like the right stick (right and up positive).
// Returns 0, queueing nothing, when no follow camera is live: the caller then
// drives the stick instead.
int dm_odyssey_camera_turn(double right, double up);

#define DM_ODYSSEY_CAMERA_LIVE_NS UINT64_C(100000000)
// Each camera update applies this share of the queued turn (the rest carries
// to the next frame), which evens out touch samples landing unevenly between frames.
#define DM_ODYSSEY_CAMERA_TAKE 0.65
#define DM_ODYSSEY_CAMERA_FLUSH 0.01   // degrees: a smaller remainder is applied whole

typedef struct {
    uint64_t yaw_updates, pitch_updates, turns_queued, turns_refused;
    double yaw_applied, pitch_applied;
    int installed;
} dm_odyssey_camera_stats;
void dm_odyssey_camera_snapshot(dm_odyssey_camera_stats *out);
