#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace anticheat_watcher {

// True if the overlay should suspend itself right now: either the
// foreground process's executable name matches the built-in exclude-list
// (or `extraExcludedNames`, lower-case with .exe -- see config.h), or the
// foreground window looks like exclusive fullscreen (covers its entire
// monitor with no window chrome). Both are situations where a system-wide,
// always-on-top overlay risks tripping a game's anti-cheat -- or, for
// genuine exclusive fullscreen, simply can't render above the game anyway,
// so there's no point staying active. `overlayHwnd` is excluded from
// consideration (our own window is never itself the reason to suspend).
bool ShouldSuspend(HWND overlayHwnd,
                    const std::vector<std::string>& extraExcludedNames = {});

}  // namespace anticheat_watcher
