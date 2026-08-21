#include "config.h"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <locale>
#include <sstream>

namespace config {
namespace {

// std::stof/std::stoi parse through the process's global C locale, not a
// fixed one -- GTK's gtk_init() calls setlocale(LC_ALL, "") on startup, so
// under a locale that uses ',' as the decimal separator (Turkish among
// many others), "1.75" silently parses as just "1", and a value the user
// set looks like it "didn't save" after a restart. Parsing through an
// istringstream explicitly imbued with the classic "C" locale sidesteps
// that. WriteDefaultConfig/Save() imbue their output streams the same way
// so a saved file always uses '.' too.
float ParseFloatClassic(const std::string& s, float fallback) {
    std::istringstream iss(s);
    iss.imbue(std::locale::classic());
    float v;
    if (iss >> v) return v;
    return fallback;
}

int ParseIntClassic(const std::string& s, int fallback) {
    std::istringstream iss(s);
    iss.imbue(std::locale::classic());
    int v;
    if (iss >> v) return v;
    return fallback;
}

std::string GetConfigPath() {
    char exePath[4096];
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    std::string dir = ".";
    if (len > 0) {
        exePath[len] = '\0';
        std::string path(exePath);
        size_t slash = path.find_last_of('/');
        if (slash != std::string::npos) {
            dir = path.substr(0, slash);
        }
    }
    return dir + "/config.ini";
}

constexpr char kDefaultConfigContents[] =
    "# CursorFlow config\n"
    "# blur_intensity: multiplier on the built-in blur strength (0 = off, 1 = default)\n"
    "blur_intensity=1.0\n"
    "# trail_length: number of fading trail points (0 = off, higher = longer tail)\n"
    "trail_length=24\n"
    "# ghost_scale: overall size multiplier for the ghost/blur/trail (1.0 = real cursor size)\n"
    "ghost_scale=1.0\n"
    "# rotation_intensity: multiplier on the ghost's max lean angle (0 = no rotation)\n"
    "rotation_intensity=1.0\n"
    "# spring_speed: multiplier on how fast the ghost catches up (1.0 = default, higher = snappier)\n"
    "spring_speed=1.0\n"
    "# layer1_style: thin_cross | thick_cross | dot | custom\n"
    "layer1_style=thick_cross\n"
    "# layer1_invert: 1 = high-contrast fill+outline (default), 0 = plain solid black\n"
    "layer1_invert=1\n"
    "# layer1_custom_path: Xcursor-format cursor file to use when layer1_style=custom\n"
    "layer1_custom_path=\n"
    "# layer1_reload_token: bumped by the settings GUI's Reload button; not user-editable\n"
    "layer1_reload_token=0\n"
    "# exclude_list: comma-separated extra process names to auto-suspend for\n"
    "exclude_list=\n";

void WriteDefaultConfig(const std::string& path) {
    std::ofstream out(path);
    if (out) {
        out.imbue(std::locale::classic());
        out << kDefaultConfigContents;
    }
}

std::string Trim(const std::string& s) {
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::vector<std::string> SplitCsv(const std::string& s) {
    std::vector<std::string> result;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        std::string trimmed = Trim(item);
        if (!trimmed.empty()) {
            std::transform(trimmed.begin(), trimmed.end(), trimmed.begin(),
                            [](unsigned char c) { return std::tolower(c); });
            result.push_back(trimmed);
        }
    }
    return result;
}

}  // namespace

Settings Load() {
    Settings settings;
    std::string path = GetConfigPath();

    std::ifstream in(path);
    if (!in) {
        WriteDefaultConfig(path);
        return settings;  // defaults
    }

    std::string line;
    while (std::getline(in, line)) {
        std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = Trim(trimmed.substr(0, eq));
        std::string value = Trim(trimmed.substr(eq + 1));

        if (key == "blur_intensity") {
            settings.blurIntensity =
                std::max(0.0f, ParseFloatClassic(value, settings.blurIntensity));
        } else if (key == "trail_length") {
            settings.trailLength =
                std::clamp(ParseIntClassic(value, settings.trailLength), 0, 200);
        } else if (key == "ghost_scale") {
            settings.ghostScale = std::clamp(
                ParseFloatClassic(value, settings.ghostScale), 0.1f, 4.0f);
        } else if (key == "rotation_intensity") {
            settings.rotationIntensity = std::clamp(
                ParseFloatClassic(value, settings.rotationIntensity), 0.0f, 3.0f);
        } else if (key == "spring_speed") {
            settings.springSpeed = std::clamp(
                ParseFloatClassic(value, settings.springSpeed), 0.1f, 5.0f);
        } else if (key == "layer1_style") {
            settings.layer1Style = value;
        } else if (key == "layer1_invert") {
            settings.layer1Invert = (value == "1" || value == "true");
        } else if (key == "layer1_custom_path") {
            settings.layer1CustomCursorPath = value;
        } else if (key == "layer1_reload_token") {
            settings.layer1ReloadToken = ParseIntClassic(value, settings.layer1ReloadToken);
        } else if (key == "exclude_list") {
            settings.extraExcludedProcesses = SplitCsv(value);
        }
    }

    return settings;
}

void Save(const Settings& settings) {
    std::string path = GetConfigPath();
    std::ofstream out(path);
    if (!out) {
        return;
    }
    out.imbue(std::locale::classic());

    out << "# CursorFlow config\n";
    out << "blur_intensity=" << settings.blurIntensity << "\n";
    out << "trail_length=" << settings.trailLength << "\n";
    out << "ghost_scale=" << settings.ghostScale << "\n";
    out << "rotation_intensity=" << settings.rotationIntensity << "\n";
    out << "spring_speed=" << settings.springSpeed << "\n";
    out << "layer1_style=" << settings.layer1Style << "\n";
    out << "layer1_invert=" << (settings.layer1Invert ? 1 : 0) << "\n";
    out << "layer1_custom_path=" << settings.layer1CustomCursorPath << "\n";
    out << "layer1_reload_token=" << settings.layer1ReloadToken << "\n";
    out << "exclude_list=";
    for (size_t i = 0; i < settings.extraExcludedProcesses.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << settings.extraExcludedProcesses[i];
    }
    out << "\n";
}

}  // namespace config
