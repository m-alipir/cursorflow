#pragma once

#include <windows.h>

namespace raw_input {

struct State {
    // Accumulated raw relative motion since the last ResetFrame(), in
    // device units (not screen pixels). Used for velocity estimation once
    // the spring/rotation physics need it; NOT the authoritative position
    // -- that stays GetCursorPos-based to avoid drift across DPI/monitor
    // boundaries.
    LONG accumDx = 0;
    LONG accumDy = 0;
    UINT eventCount = 0;
};

// Registers for background (RIDEV_INPUTSINK) mouse raw input on hwnd, so
// events keep arriving even though the overlay window never has focus.
// Call once after the window is created.
bool Register(HWND hwnd);

// Call from the window's WM_INPUT handler. Accumulates the event's
// relative deltas into state and increments its event counter.
void HandleWmInput(LPARAM lParam, State& state);

// Resets accumulated deltas/count to zero. Call once per rendered frame,
// after reading the accumulated values.
void ResetFrame(State& state);

}  // namespace raw_input
