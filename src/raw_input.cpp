#include "raw_input.h"

namespace raw_input {
namespace {
constexpr USHORT kHidUsagePageGeneric = 0x01;
constexpr USHORT kHidUsageGenericMouse = 0x02;
}  // namespace

bool Register(HWND hwnd) {
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = kHidUsagePageGeneric;
    rid.usUsage = kHidUsageGenericMouse;
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = hwnd;
    return RegisterRawInputDevices(&rid, 1, sizeof(rid)) == TRUE;
}

void HandleWmInput(LPARAM lParam, State& state) {
    UINT size = 0;
    GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr,
                     &size, sizeof(RAWINPUTHEADER));
    if (size == 0 || size > 256) {
        return;
    }

    BYTE buffer[256];
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT,
                         buffer, &size, sizeof(RAWINPUTHEADER)) != size) {
        return;
    }

    auto* raw = reinterpret_cast<RAWINPUT*>(buffer);
    if (raw->header.dwType != RIM_TYPEMOUSE) {
        return;
    }

    if (!(raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
        state.accumDx += raw->data.mouse.lLastX;
        state.accumDy += raw->data.mouse.lLastY;
    }
    ++state.eventCount;
}

void ResetFrame(State& state) {
    state.accumDx = 0;
    state.accumDy = 0;
    state.eventCount = 0;
}

}  // namespace raw_input
