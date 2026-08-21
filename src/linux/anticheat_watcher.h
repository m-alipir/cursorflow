#pragma once

#include <X11/Xlib.h>

#include <string>
#include <vector>

namespace anticheat_watcher {

// True if the overlay should suspend itself right now: either the active
// window's process name matches the built-in exclude-list (or
// `extraExcludedNames`, lower-case, no extension -- see config.h), or the
// active window has explicitly set _NET_WM_STATE_FULLSCREEN. The latter
// is the standard EWMH mechanism apps/games use to declare fullscreen
// intent -- and notably more reliable than the Windows port's
// covers-the-monitor-with-no-chrome heuristic, which turned out to false-
// positive on the desktop shell's own window (see anticheat_watcher.cpp
// on the Windows side for that story); X11 apps opt into this state
// explicitly, so there's no geometry-guessing involved here.
bool ShouldSuspend(Display* display,
                    const std::vector<std::string>& extraExcludedNames = {});

}  // namespace anticheat_watcher
