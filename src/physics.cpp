#include "physics.h"

#include <algorithm>
#include <cmath>

namespace physics {
namespace {

// Higher = snappier/less lag, lower = laggier. Tuned later against the
// spec's ~60-100ms lag target once the spring (M5) replaces this lerp.
constexpr float kLerpRatePerSecond = 12.0f;

// Base stiffness at speedMultiplier=1.0; UpdateSpring recomputes damping
// as 2*sqrt(k) each call so the motion stays critically damped (fastest
// approach with no overshoot/oscillation) at any speedMultiplier, not
// just this default. Chosen so the natural frequency (sqrt(kSpringK))
// puts ~98% settling time in the spec's ~60-100ms lag band.
constexpr float kSpringK = 3200.0f;

// Fixed sub-step size for the spring integrator, so behavior stays stable
// (and lag timing stays consistent) even at low frame rates or after a
// stall produces one large dt.
constexpr float kMaxSubstepSeconds = 1.0f / 240.0f;

// Tilt: a small lean OPPOSITE the direction of horizontal travel (drag/
// inertia, like a dragged flag or a pendulum lagging its pivot) -- not a
// rotation that points toward the direction of motion. kMaxTiltRadians
// caps it (at intensityMultiplier=1.0); kTiltVelocityForMaxTilt is the
// horizontal speed (px/s) at which that cap is reached. kTiltSmoothingRate
// low-pass filters the result frame to frame so a sudden direction
// reversal eases through zero instead of snapping between the two
// extremes (previously visible as the ghost "flipping").
constexpr float kMaxTiltRadians = 0.70f;  // ~40 degrees
constexpr float kTiltVelocityForMaxTilt = 1400.0f;
constexpr float kTiltSmoothingRate = 10.0f;

}  // namespace

void UpdateLerp(LerpState& state, Vec2 target, float dtSeconds) {
    float t = 1.0f - std::exp(-kLerpRatePerSecond * dtSeconds);
    state.position.x += (target.x - state.position.x) * t;
    state.position.y += (target.y - state.position.y) * t;
}

void UpdateSpring(SpringState& state, Vec2 target, float dtSeconds,
                   float speedMultiplier) {
    float k = kSpringK * std::max(0.01f, speedMultiplier);
    float damping = 2.0f * std::sqrt(k);  // stays critically damped at any k

    float remaining = dtSeconds;
    while (remaining > 0.0f) {
        float dt = std::min(remaining, kMaxSubstepSeconds);

        Vec2 diff{target.x - state.position.x, target.y - state.position.y};
        state.velocity.x += diff.x * k * dt - state.velocity.x * damping * dt;
        state.velocity.y += diff.y * k * dt - state.velocity.y * damping * dt;

        state.position.x += state.velocity.x * dt;
        state.position.y += state.velocity.y * dt;

        remaining -= dt;
    }
}

void UpdateTilt(SpringState& state, float dtSeconds, float intensityMultiplier) {
    float maxTilt = kMaxTiltRadians * std::max(0.0f, intensityMultiplier);
    float tiltPerVelocity = maxTilt / kTiltVelocityForMaxTilt;
    float targetTilt = std::clamp(-state.velocity.x * tiltPerVelocity, -maxTilt,
                                   maxTilt);
    float t = 1.0f - std::exp(-kTiltSmoothingRate * dtSeconds);
    state.tiltAngle += (targetTilt - state.tiltAngle) * t;
}

}  // namespace physics
