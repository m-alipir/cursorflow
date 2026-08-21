#pragma once

#include <X11/Xlib.h>

#include <string>

namespace cursor_scheme {

// Mirrors the Windows port's cursor_scheme::Style -- kept in sync so
// config.ini's layer1_style value means the same thing on both platforms.
// Shape only -- color mode (invert vs. solid) is the independent `invert`
// parameter below, not baked into the enum.
enum class Style {
    kThinCross,   // 1px-thick cross -- the original pre-thickening look
    kThickCross,  // 3px-thick cross (default shape)
    kDot,         // small filled dot
    kCustom,      // user-supplied Xcursor-format cursor file
};

// Builds the Layer 1 cursor image for `style` (via libXcursor, ARGB with
// real alpha) and grabs the pointer with it as the active cursor.
//
// `invert`: X11 has no true screen-invert compositing primitive
// (Xcursor/XRender cursors are plain alpha-blended sprites), so this is a
// visual stand-in rather than a literal AND=1/XOR=1 invert like the
// Windows port's: true draws a white fill with a black outline (always
// readable against most backgrounds), false draws a plain solid black
// fill. Ignored for kCustom (the loaded file's own colors are used
// as-is).
//
// For kCustom, `customCursorPath` must point to an Xcursor-format cursor
// file (e.g. one found under /usr/share/icons/<theme>/cursors/); on load
// failure this falls back to kThickCross+invert.
//
// X11 has no equivalent of Windows' SetSystemCursor (a persistent,
// shared, whole-system resource-table override) -- XFixes' cursor-image
// API is query-only (XFixesGetCursorImage) for reading, and its
// modification calls (XFixesChangeCursor/-ByName) only redirect specific
// already-created Cursor objects, not "whatever is currently displayed".
// The correct, well-established X11 mechanism for "force one cursor image
// to show everywhere, without eating other apps' input" is instead
// XGrabPointer() with owner_events=True: it displays the given Cursor for
// as long as the grab is held, while still delivering events normally to
// whatever window is actually under the pointer.
//
// CAVEAT: a pointer grab is exclusive -- only one client can hold it at a
// time. Toolkits commonly grab the pointer themselves for the duration of
// a drag, a dropdown menu, or a window move/resize, which forcibly
// releases ours; ReassertGrab() must be called periodically (e.g. once
// per frame) to reclaim it once the other grab ends. This means Layer 1's
// fixed shape may briefly revert to normal during those interactions --
// a real, expected gap in this mechanism, not a bug to chase away.
bool Initialize(Display* display, Window grabWindow,
                 Style style = Style::kThickCross, bool invert = true,
                 const std::string& customCursorPath = "");

// Rebuilds the Layer 1 cursor for a new style/invert/path (e.g. after a
// live config.ini reload) and re-grabs with it immediately if a grab is
// currently held. Falls back to kThickCross+invert on load failure, same
// as Initialize().
void SetStyle(Display* display, Window grabWindow, Style style, bool invert,
              const std::string& customCursorPath);

// Attempts to (re-)acquire the pointer grab with our fixed cursor. Safe
// and cheap to call every frame: succeeds immediately if we already hold
// it, and harmlessly fails (leaving the real cursor visible until the
// next attempt) if some other client currently holds a grab.
void ReassertGrab(Display* display, Window grabWindow);

// Releases the grab, handing the cursor back to normal X11 resolution.
// Idempotent and safe to call even if we don't currently hold the grab.
// Unlike Windows, there is no persistent state to corrupt if the process
// is killed without calling this -- the grab is tied to our X connection
// and the server releases it automatically when that connection closes
// (clean exit, crash, or SIGKILL alike), so no watchdog process is
// needed here the way Windows' SetSystemCursor override required one.
void Release(Display* display);

}  // namespace cursor_scheme
