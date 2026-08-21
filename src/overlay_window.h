#pragma once

#include <windows.h>

#include "raw_input.h"

namespace overlay_window {

// Custom message the tray icon uses to report mouse activity (see
// NOTIFYICONDATA::uCallbackMessage), and the popup menu command IDs.
constexpr UINT kTrayIconMessage = WM_APP + 1;
constexpr UINT kMenuIdToggle = 1;
constexpr UINT kMenuIdExit = 2;
constexpr UINT kMenuIdSettings = 3;

// Shared per-window state the WindowProc reads/writes and main.cpp polls.
// A pointer to one of these is stashed in GWLP_USERDATA (see Create()'s
// doc comment) since Win32 has no other clean way to hand context into a
// static WindowProc callback.
struct WindowContext {
    raw_input::State rawInput;
    bool manuallyDisabled = false;  // toggled from the tray icon menu
    bool exitRequested = false;     // set when "Exit" is chosen from the tray menu
};

// Bounds of the full virtual desktop (all monitors combined), in physical
// pixels. Requires the process to be per-monitor DPI aware (see
// app.manifest) for this to be meaningful on mixed-DPI setups.
RECT GetVirtualDesktopBounds();

// Creates a borderless, click-through, always-on-top window covering
// `bounds`. Uses WS_EX_LAYERED (never painted via UpdateLayeredWindow --
// content comes from the DirectComposition-attached DXGI swap chain
// instead) together with WS_EX_TRANSPARENT: WS_EX_TRANSPARENT alone is not
// sufficient for input to pass through to windows owned by OTHER processes
// (e.g. the desktop) -- WS_EX_LAYERED is required alongside it for that.
//
// Caller should call SetWindowLongPtrW(hwnd, GWLP_USERDATA,
// reinterpret_cast<LONG_PTR>(&context)) right after this returns, so the
// window's WM_INPUT/tray-icon/menu handlers can read and update it.
HWND Create(HINSTANCE hInstance, const RECT& bounds);

}  // namespace overlay_window
