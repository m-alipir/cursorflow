#include "config.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>

namespace config {
namespace {

std::wstring GetConfigPath() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring path(exePath);
    size_t slash = path.find_last_of(L"\\/");
    std::wstring dir = slash == std::wstring::npos ? L"." : path.substr(0, slash);
    return dir + L"\\config.ini";
}

constexpr wchar_t kDefaultConfigContents[] =
    L"# Smooth Cursor Overlay config\n"
    L"# blur_intensity: multiplier on the built-in blur strength (0 = off, 1 = default)\n"
    L"blur_intensity=1.0\n"
    L"# trail_length: number of fading trail points (0 = off, higher = longer tail)\n"
    L"trail_length=24\n"
    L"# ghost_scale: overall size multiplier for the ghost/blur/trail (1.0 = real cursor size)\n"
    L"ghost_scale=1.0\n"
    L"# rotation_intensity: multiplier on the ghost's max lean angle (0 = no rotation)\n"
    L"rotation_intensity=1.0\n"
    L"# spring_speed: multiplier on how fast the ghost catches up (1.0 = default, higher = snappier)\n"
    L"spring_speed=1.0\n"
    L"# layer1_style: thin_cross | thick_cross | dot | custom\n"
    L"layer1_style=thick_cross\n"
    L"# layer1_invert: 1 = screen-color invert (default), 0 = plain solid black\n"
    L"layer1_invert=1\n"
    L"# layer1_custom_path: .cur/.ani/.ico file to use when layer1_style=custom\n"
    L"layer1_custom_path=\n"
    L"# exclude_list: comma-separated extra process names to auto-suspend for\n"
    L"exclude_list=\n";

void WriteDefaultConfig(const std::wstring& path) {
    std::wofstream out(path);
    if (out) {
        out << kDefaultConfigContents;
    }
}

// layer1_custom_path is a real filesystem path, which on Windows can
// contain non-ASCII characters (a non-ASCII username, for instance) -- so
// unlike the ASCII-only fields, this one gets a proper UTF-8 round trip
// rather than a narrowing cast.
std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0,
                                    nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, result.data(), size,
                         nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, result.data(), size);
    return result;
}

std::wstring Trim(const std::wstring& s) {
    size_t begin = s.find_first_not_of(L" \t\r\n");
    if (begin == std::wstring::npos) {
        return L"";
    }
    size_t end = s.find_last_not_of(L" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

// Process names are always plain ASCII, so a narrowing cast here (rather
// than a full UTF-16 -> UTF-8 conversion) is safe and keeps config.h's
// Settings::extraExcludedProcesses a plain std::string shareable with the
// Linux port.
std::vector<std::string> SplitCsv(const std::wstring& s) {
    std::vector<std::string> result;
    std::wstringstream ss(s);
    std::wstring item;
    while (std::getline(ss, item, L',')) {
        std::wstring trimmed = Trim(item);
        if (!trimmed.empty()) {
            std::transform(trimmed.begin(), trimmed.end(), trimmed.begin(),
                            [](wchar_t c) { return std::towlower(c); });
            std::string narrow;
            narrow.reserve(trimmed.size());
            for (wchar_t c : trimmed) {
                narrow.push_back(static_cast<char>(c));
            }
            result.push_back(std::move(narrow));
        }
    }
    return result;
}

}  // namespace

Settings Load() {
    Settings settings;
    std::wstring path = GetConfigPath();

    std::wifstream in(path);
    if (!in) {
        WriteDefaultConfig(path);
        return settings;  // defaults
    }

    std::wstring line;
    while (std::getline(in, line)) {
        std::wstring trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == L'#') {
            continue;
        }
        size_t eq = trimmed.find(L'=');
        if (eq == std::wstring::npos) {
            continue;
        }
        std::wstring key = Trim(trimmed.substr(0, eq));
        std::wstring value = Trim(trimmed.substr(eq + 1));

        if (key == L"blur_intensity") {
            try {
                settings.blurIntensity = std::max(0.0f, std::stof(value));
            } catch (...) {
            }
        } else if (key == L"trail_length") {
            try {
                settings.trailLength = std::clamp(std::stoi(value), 0, 200);
            } catch (...) {
            }
        } else if (key == L"ghost_scale") {
            try {
                settings.ghostScale = std::clamp(std::stof(value), 0.1f, 4.0f);
            } catch (...) {
            }
        } else if (key == L"rotation_intensity") {
            try {
                settings.rotationIntensity = std::clamp(std::stof(value), 0.0f, 3.0f);
            } catch (...) {
            }
        } else if (key == L"spring_speed") {
            try {
                settings.springSpeed = std::clamp(std::stof(value), 0.1f, 5.0f);
            } catch (...) {
            }
        } else if (key == L"layer1_style") {
            settings.layer1Style = WideToUtf8(value);
        } else if (key == L"layer1_invert") {
            settings.layer1Invert = (value == L"1" || value == L"true");
        } else if (key == L"layer1_custom_path") {
            settings.layer1CustomCursorPath = WideToUtf8(value);
        } else if (key == L"exclude_list") {
            settings.extraExcludedProcesses = SplitCsv(value);
        }
    }

    return settings;
}

void Save(const Settings& settings) {
    std::wstring path = GetConfigPath();
    std::wofstream out(path);
    if (!out) {
        return;
    }

    out << L"# Smooth Cursor Overlay config\n";
    out << L"blur_intensity=" << settings.blurIntensity << L"\n";
    out << L"trail_length=" << settings.trailLength << L"\n";
    out << L"ghost_scale=" << settings.ghostScale << L"\n";
    out << L"rotation_intensity=" << settings.rotationIntensity << L"\n";
    out << L"spring_speed=" << settings.springSpeed << L"\n";
    out << L"layer1_style=" << Utf8ToWide(settings.layer1Style) << L"\n";
    out << L"layer1_invert=" << (settings.layer1Invert ? 1 : 0) << L"\n";
    out << L"layer1_custom_path=" << Utf8ToWide(settings.layer1CustomCursorPath) << L"\n";
    out << L"exclude_list=";
    for (size_t i = 0; i < settings.extraExcludedProcesses.size(); ++i) {
        if (i > 0) {
            out << L",";
        }
        const std::string& s = settings.extraExcludedProcesses[i];
        out << std::wstring(s.begin(), s.end());
    }
    out << L"\n";
}

}  // namespace config
