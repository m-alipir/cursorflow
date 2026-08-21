#define OEMRESOURCE
#include "cursor_scheme.h"

#include <windows.h>

#include <array>

namespace cursor_scheme {
namespace {

// No OCR_HELP exists in the Win32 header set -- the "?" help cursor is not
// individually settable via SetSystemCursor, so it is intentionally omitted.
constexpr std::array<DWORD, 13> kCursorRoles = {
    OCR_NORMAL,   OCR_IBEAM,       OCR_WAIT,   OCR_CROSS, OCR_UP,
    OCR_SIZENWSE, OCR_SIZENESW,    OCR_SIZEWE, OCR_SIZENS, OCR_SIZEALL,
    OCR_NO,       OCR_HAND,        OCR_APPSTARTING,
};

constexpr int kSize = 32;
constexpr int kRowBytes = kSize / 8;  // 1bpp, already WORD-aligned at 32px

using MaskArray = std::array<BYTE, kRowBytes * kSize>;

struct CursorMasks {
    MaskArray andMask;
    MaskArray xorMask;
};

CursorMasks NewBlankMasks() {
    CursorMasks m;
    m.andMask.fill(0xFF);  // fully transparent everywhere by default
    m.xorMask.fill(0x00);
    return m;
}

// `invert`: AND=1,XOR=1, the classic Win32 monochrome-cursor "invert"
// combination (screen color XOR 0xFFFFFF) -- always visible against any
// background, no fixed color choice needed. When false: plain solid
// black (AND=0,XOR=0) -- simpler, but can vanish against a dark
// background.
void SetPixel(CursorMasks& m, int x, int y, bool invert) {
    int byteIndex = y * kRowBytes + (x / 8);
    BYTE bit = static_cast<BYTE>(0x80 >> (x % 8));
    if (invert) {
        m.xorMask[byteIndex] |= bit;
    } else {
        m.andMask[byteIndex] &= static_cast<BYTE>(~bit);
    }
}

// Builds a fixed crosshair cursor, identical regardless of which role it
// replaces -- Layer 1 must never change shape based on context.
// `halfThickness` 0 = 1px-thick arms (kThinCross), 1 = 3px-thick
// (kThickCross).
HCURSOR BuildCrossCursor(bool invert, int halfThickness) {
    CursorMasks m = NewBlankMasks();

    constexpr int kCenter = kSize / 2;
    constexpr int kArmLength = 6;
    for (int i = -kArmLength; i <= kArmLength; ++i) {
        for (int t = -halfThickness; t <= halfThickness; ++t) {
            SetPixel(m, kCenter + i, kCenter + t, invert);
            SetPixel(m, kCenter + t, kCenter + i, invert);
        }
    }

    return CreateCursor(GetModuleHandleW(nullptr), kCenter, kCenter, kSize,
                         kSize, m.andMask.data(), m.xorMask.data());
}

// A small filled dot.
HCURSOR BuildDotCursor(bool invert) {
    CursorMasks m = NewBlankMasks();

    constexpr int kCenter = kSize / 2;
    constexpr int kRadius = 4;
    for (int y = -kRadius; y <= kRadius; ++y) {
        for (int x = -kRadius; x <= kRadius; ++x) {
            if (x * x + y * y > kRadius * kRadius) continue;
            SetPixel(m, kCenter + x, kCenter + y, invert);
        }
    }

    return CreateCursor(GetModuleHandleW(nullptr), kCenter, kCenter, kSize,
                         kSize, m.andMask.data(), m.xorMask.data());
}

HCURSOR BuildStyledCursor(Style style, bool invert,
                           const std::wstring& customCursorPath) {
    switch (style) {
        case Style::kThinCross:
            return BuildCrossCursor(invert, /*halfThickness=*/0);
        case Style::kDot:
            return BuildDotCursor(invert);
        case Style::kCustom:
            if (!customCursorPath.empty()) {
                HCURSOR loaded = LoadCursorFromFileW(customCursorPath.c_str());
                if (loaded) {
                    // LoadCursorFromFileW's handle has its own lifetime
                    // rules; SetSystemCursor takes ownership of (and
                    // destroys) whatever we give it, so hand it a copy.
                    HCURSOR copy = CopyCursor(loaded);
                    DestroyCursor(loaded);
                    if (copy) {
                        return copy;
                    }
                }
            }
            // Custom file missing/invalid -- fall back rather than fail.
            return BuildCrossCursor(/*invert=*/true, /*halfThickness=*/1);
        case Style::kThickCross:
        default:
            return BuildCrossCursor(invert, /*halfThickness=*/1);
    }
}

}  // namespace

bool ApplyOverride(Style style, bool invert,
                    const std::wstring& customCursorPath) {
    bool allOk = true;
    for (DWORD role : kCursorRoles) {
        HCURSOR shape = BuildStyledCursor(style, invert, customCursorPath);
        if (!shape) {
            allOk = false;
            continue;
        }
        // SetSystemCursor takes ownership of the handle it's given, even on
        // failure, so we never leak or double-destroy it here.
        if (!SetSystemCursor(shape, role)) {
            allOk = false;
        }
    }
    return allOk;
}

void Restore() {
    // Reloads OCR_* cursors from the registry-defined scheme. Because
    // ApplyOverride() never wrote to the registry, this always recovers the
    // user's real scheme -- regardless of which process calls it or why.
    SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
}

}  // namespace cursor_scheme
