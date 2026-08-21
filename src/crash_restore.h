#pragma once

namespace crash_restore {

// Installs an in-process unhandled-exception filter and an atexit hook that
// restore the real cursor scheme immediately on a crash or normal exit, and
// spawns a watchdog helper process (a second instance of this executable,
// launched with a hidden command-line flag) that waits for this process to
// die -- for ANY reason, including a hard kill from Task Manager, which no
// in-process handler can intercept -- and restores the cursor scheme too.
// Call once, early in wWinMain, before cursor_scheme::ApplyOverride().
void Install();

// Checks whether the current process was launched in watchdog mode (see
// Install()). If so, blocks until the target process exits, restores the
// cursor scheme, and returns true. If not, returns false immediately and
// the caller should proceed with normal startup.
bool RunIfWatchdogMode();

}  // namespace crash_restore
