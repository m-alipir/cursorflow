#pragma once

namespace crash_restore {

// Installs SIGINT/SIGTERM handlers that request a clean shutdown -- the
// main loop checks ShutdownRequested() and, if set, exits its loop
// normally, releasing the pointer grab via cursor_scheme::Release().
//
// Unlike the Windows port, NO watchdog process is needed here: X11's
// pointer grab (see cursor_scheme.h) is tied to our connection to the X
// server, so ANY process termination -- clean exit, SIGTERM, or even
// SIGKILL, which cannot be caught by any handler -- releases it
// automatically the moment that connection closes. This is a genuine
// architectural difference from Windows' SetSystemCursor, which persists
// as global registry/session state until something explicitly restores
// it. Installing these handlers just lets SIGINT/SIGTERM shut down
// cleanly (flushing the X connection, etc.) rather than abruptly; it is
// not what makes crash-safety hold here.
void Install();

// True once SIGINT or SIGTERM has been received.
bool ShutdownRequested();

}  // namespace crash_restore
