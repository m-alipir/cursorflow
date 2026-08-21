#pragma once

#include <string>

namespace cursor_scheme {

enum class Style {
    kInvertCross,  // current default: thick cross, screen-color invert (AND=1,XOR=1)
    kSolidCross,   // the original pre-invert look: thick solid black cross
    kDot,          // a small solid-black dot, invert-style for visibility
    kCustom,       // load a user-supplied .cur/.ani/.ico via customCursorPath
};

// Overrides every system cursor role (arrow, hand, ibeam, resize, etc.) with
// a single fixed shape, system-wide, chosen by `style`. Uses SetSystemCursor(),
// which never touches the registry -- the user's real cursor scheme stays
// intact on disk no matter what happens to this process afterward.
// `customCursorPath` is only used when style == kCustom; if the file can't
// be loaded, this falls back to kInvertCross rather than failing outright.
bool ApplyOverride(Style style = Style::kInvertCross,
                    const std::wstring& customCursorPath = L"");

// Reloads the real cursor scheme from the registry, discarding any
// ApplyOverride() changes. Safe to call even if no override is active, and
// safe to call from a different process than the one that applied it (this
// is what makes the watchdog-based crash restore in crash_restore.h work).
void Restore();

}  // namespace cursor_scheme
