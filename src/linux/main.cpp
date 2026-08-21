#include <X11/Xlib.h>

#include <chrono>
#include <thread>

#include "anticheat_watcher.h"
#include "config.h"
#include "crash_restore.h"
#include "cursor_scheme.h"
#include "cursor_shape_sync.h"
#include "overlay_window.h"
#include "physics.h"
#include "raw_input.h"
#include "renderer_cairo.h"

namespace {
constexpr auto kTargetFrameDuration = std::chrono::microseconds(16667);  // ~60fps

// Xlib's default error handler prints to stderr and, for some error
// classes, can terminate the process. Querying window properties for
// anti-cheat/foreground detection is inherently racy -- the active window
// can be destroyed between XGetWindowProperty calls -- so a BadWindow (or
// similar) here is an expected, recoverable condition, not a fatal one.
int HandleXError(Display*, XErrorEvent*) { return 0; }

cursor_scheme::Style ParseLayer1Style(const std::string& s) {
    if (s == "thin_cross") return cursor_scheme::Style::kThinCross;
    if (s == "dot") return cursor_scheme::Style::kDot;
    if (s == "custom") return cursor_scheme::Style::kCustom;
    return cursor_scheme::Style::kThickCross;
}

}  // namespace

int main() {
    XSetErrorHandler(HandleXError);

    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        return 1;
    }
    Window root = DefaultRootWindow(display);

    overlay_window::Bounds bounds = overlay_window::GetVirtualScreenBounds(display);
    Window win = overlay_window::Create(display, bounds);
    if (!win) {
        XCloseDisplay(display);
        return 1;
    }

    renderer_cairo::Renderer renderer;
    if (!renderer.Initialize(display, win, bounds.width, bounds.height)) {
        XCloseDisplay(display);
        return 1;
    }

    // Capture the user's real/default cursor appearance BEFORE grabbing
    // the pointer below -- this is the ghost's shape (instead of a
    // generic placeholder circle), so Layer 2 reads as "your actual
    // cursor, delayed and blurred". Must happen before the grab:
    // afterward, XFixesGetCursorImage() only ever reports our own fixed
    // cross for as long as we hold it.
    cursor_shape_sync::CapturedCursor userCursor;
    bool haveUserCursor =
        cursor_shape_sync::CaptureCurrentSystemCursor(display, userCursor);

    config::Settings settings = config::Load();

    if (!cursor_scheme::Initialize(display, root,
                                    ParseLayer1Style(settings.layer1Style),
                                    settings.layer1Invert,
                                    settings.layer1CustomCursorPath)) {
        XCloseDisplay(display);
        return 1;
    }
    std::string appliedLayer1Style = settings.layer1Style;
    bool appliedLayer1Invert = settings.layer1Invert;
    std::string appliedLayer1Path = settings.layer1CustomCursorPath;
    int appliedLayer1ReloadToken = settings.layer1ReloadToken;

    renderer.Configure(settings.blurIntensity, settings.trailLength,
                        settings.ghostScale);

    int xinput2Opcode = raw_input::Register(display);

    crash_restore::Install();

    physics::SpringState springState;
    auto lastFrameTime = std::chrono::steady_clock::now();
    auto lastConfigReload = std::chrono::steady_clock::now();
    bool suspended = false;

    bool running = true;
    while (running) {
        while (XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
            if (raw_input::IsRawMotionEvent(event, xinput2Opcode)) {
                XFreeEventData(display, &event.xcookie);
            }
        }

        if (crash_restore::ShutdownRequested()) {
            break;
        }

        // Reload config.ini roughly once a second, so the settings window
        // (or hand-editing the file) takes effect live without needing to
        // restart the overlay -- same reasoning as the Windows port's
        // main.cpp.
        auto nowForReload = std::chrono::steady_clock::now();
        if (nowForReload - lastConfigReload >= std::chrono::seconds(1)) {
            lastConfigReload = nowForReload;
            settings = config::Load();
            renderer.Configure(settings.blurIntensity, settings.trailLength,
                                settings.ghostScale);

            if (!suspended && (settings.layer1Style != appliedLayer1Style ||
                                settings.layer1Invert != appliedLayer1Invert ||
                                settings.layer1CustomCursorPath != appliedLayer1Path ||
                                settings.layer1ReloadToken != appliedLayer1ReloadToken)) {
                cursor_scheme::SetStyle(display, root,
                                         ParseLayer1Style(settings.layer1Style),
                                         settings.layer1Invert,
                                         settings.layer1CustomCursorPath);
                appliedLayer1Style = settings.layer1Style;
                appliedLayer1Invert = settings.layer1Invert;
                appliedLayer1Path = settings.layer1CustomCursorPath;
                appliedLayer1ReloadToken = settings.layer1ReloadToken;
            }
        }

        bool shouldSuspend =
            anticheat_watcher::ShouldSuspend(display, settings.extraExcludedProcesses);
        if (shouldSuspend != suspended) {
            suspended = shouldSuspend;
            if (suspended) {
                cursor_scheme::Release(display);
                XUnmapWindow(display, win);
                XFlush(display);
            } else {
                XMapWindow(display, win);
                cursor_scheme::ReassertGrab(display, root);
                XFlush(display);
                lastFrameTime = std::chrono::steady_clock::now();
            }
        }

        if (suspended) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        // Cheap and safe to call every frame: succeeds instantly if we
        // already hold the grab, and harmlessly no-ops if some other
        // client currently holds one (see cursor_scheme.h's caveat).
        cursor_scheme::ReassertGrab(display, root);

        auto frameStart = std::chrono::steady_clock::now();
        float dtSeconds =
            std::chrono::duration<float>(frameStart - lastFrameTime).count();
        lastFrameTime = frameStart;

        Window queryRoot, queryChild;
        int rootX, rootY, winX, winY;
        unsigned int mask;
        XQueryPointer(display, root, &queryRoot, &queryChild, &rootX, &rootY,
                      &winX, &winY, &mask);

        // X11's root coordinate space is always (0,0)-anchored at the
        // virtual screen's top-left (see overlay_window.h), so root-
        // relative and window-relative coordinates are the same here --
        // no translation needed, unlike the Windows port.
        physics::Vec2 frameStartPos = springState.position;
        physics::Vec2 target{static_cast<float>(rootX), static_cast<float>(rootY)};
        physics::UpdateSpring(springState, target, dtSeconds, settings.springSpeed);
        physics::UpdateTilt(springState, dtSeconds, settings.rotationIntensity);

        renderer.RenderGhostAt(
            {springState.position.x, springState.position.y},
            {frameStartPos.x, frameStartPos.y}, springState.tiltAngle,
            haveUserCursor ? userCursor.surface : nullptr,
            {userCursor.hotspotX, userCursor.hotspotY});

        auto frameEnd = std::chrono::steady_clock::now();
        auto elapsed = frameEnd - frameStart;
        if (elapsed < kTargetFrameDuration) {
            std::this_thread::sleep_for(kTargetFrameDuration - elapsed);
        }
    }

    cursor_scheme::Release(display);
    if (haveUserCursor && userCursor.surface) {
        cairo_surface_destroy(userCursor.surface);
    }
    XCloseDisplay(display);
    return 0;
}
