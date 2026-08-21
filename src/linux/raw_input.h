#pragma once

#include <X11/Xlib.h>

namespace raw_input {

// Selects XI_RawMotion events on the root window for all master devices.
// Raw XInput2 events are delivered regardless of which window has focus
// (or whether any of our own windows exist at all) -- the X11 analogue of
// Windows' RIDEV_INPUTSINK, and what makes the main loop event-driven
// instead of a GetCursorPos-style busy-poll. Call once after opening the
// display connection. Returns the XInput2 extension's opcode (needed to
// recognize these events in XNextEvent's generic-event stream via
// IsRawMotionEvent), or -1 if the extension isn't available.
int Register(Display* display);

// True if `event` (from XNextEvent) is a raw motion event from the
// extension registered via Register(). The authoritative pointer
// position is still read via XQueryPointer each frame (avoids drift the
// same way the Windows port reconciles raw deltas against GetCursorPos);
// this is only used to wake the main loop efficiently on movement.
bool IsRawMotionEvent(const XEvent& event, int xinput2Opcode);

}  // namespace raw_input
