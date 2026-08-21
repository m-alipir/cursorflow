#pragma once

#include <string>

namespace cursor_scheme {

// Shape only -- color mode (invert vs. solid) is the independent `invert`
// parameter below, not baked into the enum, so any shape can be combined
// with either color mode.
enum class Style {
    kThinCross,   // 1px-thick cross -- the original pre-thickening look
    kThickCross,  // 3px-thick cross (current default shape)
    kDot,         // a small filled dot
    kCustom,      // load a user-supplied .cur/.ani/.ico via customCursorPath
};

// Overrides every system cursor role (arrow, hand, ibeam, resize, etc.) with
// a single fixed shape, system-wide, chosen by `style`. Uses SetSystemCursor(),
// which never touches the registry -- the user's real cursor scheme stays
// intact on disk no matter what happens to this process afterward.
//
// `invert`: when true, the shape is drawn with AND=1,XOR=1, the classic
// Win32 monochrome-cursor "invert" combination (screen color XOR
// 0xFFFFFF) -- always visible against any background. When false, it's
// plain solid black (AND=0,XOR=0) -- simpler, but can vanish against a
// dark background. Ignored for kCustom (the loaded file's own colors are
// used as-is).
//
// `customCursorPath` is only used when style == kCustom; if the file
// can't be loaded, this falls back to kThickCross+invert rather than
// failing outright.
bool ApplyOverride(Style style = Style::kThickCross, bool invert = true,
                    const std::wstring& customCursorPath = L"");

// Reloads the real cursor scheme from the registry, discarding any
// ApplyOverride() changes. Safe to call even if no override is active, and
// safe to call from a different process than the one that applied it (this
// is what makes the watchdog-based crash restore in crash_restore.h work).
void Restore();

}  // namespace cursor_scheme
