#pragma once

#include <string>
#include <vector>

namespace config {

struct Settings {
    // Multiplies the renderer's built-in conservative blur alpha. 1.0 =
    // default, 0.0 = off, >1.0 = stronger. Kept as a multiplier (not an
    // absolute value) so the shipped default stays deliberately
    // conservative even if a user's config file is incomplete/stale.
    float blurIntensity = 1.0f;

    // Number of trail points kept in the fading ring buffer. Clamped to a
    // sane range at load time.
    int trailLength = 24;

    // Overall size multiplier for the ghost cursor, its blur copies, and
    // its trail copies. 1.0 = the real cursor's actual size.
    float ghostScale = 1.0f;

    // Multiplier on the ghost's max lean/tilt angle (physics.cpp's
    // kMaxTiltRadians). 1.0 = default, 0.0 = no rotation, >1.0 = more
    // pronounced lean.
    float rotationIntensity = 1.0f;

    // Multiplier on the spring's stiffness (physics.cpp's kSpringK), i.e.
    // how fast the ghost catches up to the real cursor. 1.0 = default,
    // >1.0 = snappier/less lag, <1.0 = laggier. Damping is recomputed from
    // this to stay critically damped (no overshoot) at any value.
    float springSpeed = 1.0f;

    // Layer 1 (the real/front cursor override) shape: "invert_cross"
    // (default -- always contrasts with whatever's behind it), "solid_cross"
    // (the original plain black cross), "dot", or "custom" (loads
    // layer1CustomCursorPath, a .cur/.ani/.ico file).
    std::string layer1Style = "invert_cross";
    std::string layer1CustomCursorPath;

    // Extra process executable names (case-insensitive; include the .exe
    // suffix on Windows, no suffix on Linux) to suspend the overlay for,
    // on top of the built-in default list in anticheat_watcher.cpp. Plain
    // std::string (not platform-wide-string) so this header can be shared
    // as-is between the Windows and Linux ports -- process names are
    // always plain ASCII, so no encoding-conversion loss.
    std::vector<std::string> extraExcludedProcesses;
};

// Loads settings from "config.ini" next to the executable. A missing file
// is created with the defaults (so there's something for the user to
// edit); a missing or malformed individual key/line just falls back to
// that field's default rather than failing the whole load.
Settings Load();

// Writes `settings` to "config.ini" next to the executable, in the same
// key=value format Load() reads -- overwrites the whole file (comments
// are not preserved). Used by the settings GUI; the overlay process
// picks up the change within about a second (see main.cpp's periodic
// reload) without needing a restart.
void Save(const Settings& settings);

}  // namespace config
