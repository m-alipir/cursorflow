#include "raw_input.h"

#include <X11/extensions/XInput2.h>

namespace raw_input {

int Register(Display* display) {
    int opcode, event, error;
    if (!XQueryExtension(display, "XInputExtension", &opcode, &event, &error)) {
        return -1;
    }

    int major = 2, minor = 0;
    if (XIQueryVersion(display, &major, &minor) != Success) {
        return -1;
    }

    unsigned char mask[XIMaskLen(XI_RawMotion)] = {0};
    XISetMask(mask, XI_RawMotion);

    XIEventMask evmask;
    evmask.deviceid = XIAllMasterDevices;
    evmask.mask_len = sizeof(mask);
    evmask.mask = mask;

    Window root = DefaultRootWindow(display);
    XISelectEvents(display, root, &evmask, 1);
    XFlush(display);

    return opcode;
}

bool IsRawMotionEvent(const XEvent& event, int xinput2Opcode) {
    return event.type == GenericEvent &&
           event.xcookie.extension == xinput2Opcode &&
           event.xcookie.evtype == XI_RawMotion;
}

}  // namespace raw_input
