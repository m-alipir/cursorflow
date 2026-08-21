#include "anticheat_watcher.h"

#include <X11/Xatom.h>
#include <sys/types.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>

namespace anticheat_watcher {
namespace {

// Small built-in default exclude-list of well-known anti-cheat-sensitive
// process names (matched against /proc/<pid>/comm, which is truncated to
// 15 characters by the kernel -- keep entries short). A real config file
// (see config.h) lets users extend this without a rebuild.
constexpr std::array<const char*, 8> kExcludedProcessNames = {
    "csgo",        "cs2",          "valorant",  "valorant-win64-",
    "beamng.drive", "eac_launcher", "beservice", "acs",
};

Window GetActiveWindow(Display* display) {
    Window root = DefaultRootWindow(display);
    Atom activeAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);

    Atom actualType;
    int actualFormat;
    unsigned long numItems, bytesAfter;
    unsigned char* data = nullptr;

    int status = XGetWindowProperty(display, root, activeAtom, 0, 1, False,
                                     XA_WINDOW, &actualType, &actualFormat,
                                     &numItems, &bytesAfter, &data);
    Window result = None;
    if (status == Success && data && numItems > 0) {
        result = *reinterpret_cast<Window*>(data);
    }
    if (data) {
        XFree(data);
    }
    return result;
}

pid_t GetWindowPid(Display* display, Window win) {
    Atom pidAtom = XInternAtom(display, "_NET_WM_PID", False);
    Atom actualType;
    int actualFormat;
    unsigned long numItems, bytesAfter;
    unsigned char* data = nullptr;

    int status = XGetWindowProperty(display, win, pidAtom, 0, 1, False,
                                     XA_CARDINAL, &actualType, &actualFormat,
                                     &numItems, &bytesAfter, &data);
    pid_t pid = 0;
    if (status == Success && data && numItems > 0) {
        pid = static_cast<pid_t>(*reinterpret_cast<unsigned long*>(data));
    }
    if (data) {
        XFree(data);
    }
    return pid;
}

std::string GetProcessName(pid_t pid) {
    if (pid <= 0) {
        return "";
    }
    std::ifstream f("/proc/" + std::to_string(pid) + "/comm");
    std::string name;
    std::getline(f, name);
    return name;
}

bool IsExcludedName(const std::string& name,
                     const std::vector<std::string>& extra) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    for (const char* excluded : kExcludedProcessNames) {
        if (lower == excluded) {
            return true;
        }
    }
    for (const std::string& excluded : extra) {
        if (lower == excluded) {
            return true;
        }
    }
    return false;
}

bool IsFullscreen(Display* display, Window win) {
    Atom stateAtom = XInternAtom(display, "_NET_WM_STATE", False);
    Atom fullscreenAtom = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);

    Atom actualType;
    int actualFormat;
    unsigned long numItems, bytesAfter;
    unsigned char* data = nullptr;

    int status = XGetWindowProperty(display, win, stateAtom, 0, 32, False,
                                     XA_ATOM, &actualType, &actualFormat,
                                     &numItems, &bytesAfter, &data);
    bool found = false;
    if (status == Success && data) {
        auto* atoms = reinterpret_cast<Atom*>(data);
        for (unsigned long i = 0; i < numItems; ++i) {
            if (atoms[i] == fullscreenAtom) {
                found = true;
                break;
            }
        }
    }
    if (data) {
        XFree(data);
    }
    return found;
}

}  // namespace

bool ShouldSuspend(Display* display,
                    const std::vector<std::string>& extraExcludedNames) {
    Window active = GetActiveWindow(display);
    if (active == None) {
        return false;
    }

    pid_t pid = GetWindowPid(display, active);
    std::string name = GetProcessName(pid);
    if (!name.empty() && IsExcludedName(name, extraExcludedNames)) {
        return true;
    }

    return IsFullscreen(display, active);
}

}  // namespace anticheat_watcher
