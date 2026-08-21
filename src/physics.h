#pragma once

namespace physics {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct LerpState {
    Vec2 position;
};

// Moves state.position a framerate-independent fraction of the way toward
// target. Legacy MVP placeholder (M2); superseded by UpdateSpring below.
void UpdateLerp(LerpState& state, Vec2 target, float dtSeconds);

struct SpringState {
    Vec2 position;
    Vec2 velocity;
    float tiltAngle = 0.0f;  // smoothed lean angle (radians) -- see UpdateTilt
};

// Critically-damped spring toward `target`:
// velocity += (target - pos) * k - velocity * damping; pos += velocity * dt.
// Produces smooth, non-oscillating motion (no overshoot) with lag tuned to
// roughly a 60-100ms band at speedMultiplier=1.0. Sub-steps internally for
// stability at low frame rates or after a stall (large dt). `speedMultiplier`
// (config.h's spring_speed) scales the spring's stiffness -- damping is
// recomputed from it each call, so the motion stays critically damped (no
// overshoot) at any multiplier value, not just the default.
void UpdateSpring(SpringState& state, Vec2 target, float dtSeconds,
                   float speedMultiplier = 1.0f);

// Updates state.tiltAngle toward a target lean angle derived from the
// spring's current horizontal velocity: moving right leans slightly LEFT
// (negative angle), moving left leans slightly RIGHT -- drag/inertia,
// like a dragged flag or a pendulum lagging behind its pivot, NOT "point
// in the direction of travel". Magnitude scales with horizontal speed up
// to a cap set by `intensityMultiplier` (config.h's rotation_intensity;
// 1.0 = default cap, 0.0 = no rotation at all). The result is itself
// low-pass filtered frame to frame, so a sudden reversal in direction
// eases smoothly through zero instead of snapping between the two
// extremes (which reads as the ghost "flipping"). Call once per frame
// after UpdateSpring; reads/writes state.tiltAngle.
void UpdateTilt(SpringState& state, float dtSeconds,
                 float intensityMultiplier = 1.0f);

}  // namespace physics
