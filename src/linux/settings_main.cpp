#include <gtk/gtk.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "config.h"

namespace {

config::Settings g_settings;

struct Widgets {
    GtkWidget* blurScale;
    GtkWidget* trailScale;
    GtkWidget* ghostScale;
    GtkWidget* rotationScale;
    GtkWidget* speedScale;
    GtkWidget* styleCombo;
    GtkWidget* invertCheck;
    GtkWidget* startupCheck;
    GtkWidget* customPathChooser;
    GtkWidget* reloadButton;
    GtkWidget* excludeEntry;
};

// Index <-> config string mapping for the style combo box; kept in sync
// with the Windows settings GUI's equivalent list.
constexpr const char* kStyleIds[] = {"thin_cross", "thick_cross", "dot",
                                      "custom"};
constexpr int kStyleCount = 4;

int StyleToIndex(const std::string& style) {
    for (int i = 0; i < kStyleCount; ++i) {
        if (style == kStyleIds[i]) return i;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Run at startup: an XDG autostart .desktop entry is the Linux equivalent of
// the Windows port's registry Run key -- most desktop environments (GNOME,
// KDE, Xfce, ...) look under ~/.config/autostart at login without needing
// per-DE integration. Like the Windows port, this is queried/written
// directly against the OS mechanism rather than round-tripped through
// config.ini, since the autostart file itself IS the persistent state.
// ---------------------------------------------------------------------------
std::string AutostartDesktopPath() {
    const char* home = getenv("HOME");
    if (!home || !*home) return "";
    return std::string(home) + "/.config/autostart/cursorflow.desktop";
}

// The overlay binary ("CursorFlow") is expected to sit next to this
// settings binary, same assumption the Windows port makes in both
// directions between its two exes.
std::string OverlayExecutablePath() {
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return "";
    buf[len] = '\0';
    std::string path(buf);
    size_t slash = path.find_last_of('/');
    std::string dir = slash == std::string::npos ? "." : path.substr(0, slash);
    return dir + "/CursorFlow";
}

bool QueryRunAtStartup() {
    std::string path = AutostartDesktopPath();
    if (path.empty()) return false;
    std::ifstream f(path);
    return f.good();
}

void SetRunAtStartup(bool enabled) {
    std::string path = AutostartDesktopPath();
    if (path.empty()) return;

    if (enabled) {
        const char* home = getenv("HOME");
        mkdir((std::string(home) + "/.config").c_str(), 0755);
        mkdir((std::string(home) + "/.config/autostart").c_str(), 0755);

        std::ofstream f(path);
        if (f) {
            f << "[Desktop Entry]\n"
              << "Type=Application\n"
              << "Name=CursorFlow\n"
              << "Exec=\"" << OverlayExecutablePath() << "\"\n"
              << "X-GNOME-Autostart-enabled=true\n";
        }
    } else {
        remove(path.c_str());
    }
}

// Modern flat "snow white" theme: plain white background, thin flat
// sliders (no 3D bevels), soft rounded entry field. GTK3's CSS support
// makes this straightforward without any custom drawing code.
constexpr char kCss[] = R"CSS(
window { background-color: #ffffff; }
label { color: #1e1e1e; }
label.hint { color: #8a8a8a; font-size: 11px; }
label.section { font-weight: 600; font-size: 13px; }
scale { min-height: 24px; }
scale trough { background-color: #e8e8e8; border-radius: 4px; min-height: 6px; }
scale highlight { background-color: #2b6ef2; border-radius: 4px; }
scale slider {
    background-color: #ffffff;
    border: 1px solid #c9c9c9;
    border-radius: 50%;
    min-width: 16px;
    min-height: 16px;
    box-shadow: 0 1px 2px rgba(0,0,0,0.15);
}
entry {
    background-color: #f7f7f7;
    border: 1px solid #dcdcdc;
    border-radius: 6px;
    padding: 6px 8px;
}
)CSS";

std::string JoinExcludeList(const config::Settings& s) {
    std::string result;
    for (size_t i = 0; i < s.extraExcludedProcesses.size(); ++i) {
        if (i > 0) result += ", ";
        result += s.extraExcludedProcesses[i];
    }
    return result;
}

std::vector<std::string> SplitExcludeList(const std::string& text) {
    std::vector<std::string> result;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        size_t begin = item.find_first_not_of(" \t");
        size_t end = item.find_last_not_of(" \t");
        if (begin != std::string::npos) {
            result.push_back(item.substr(begin, end - begin + 1));
        }
    }
    return result;
}

void UpdateStyleWidgetsSensitivity(Widgets* w) {
    int styleIndex = gtk_combo_box_get_active(GTK_COMBO_BOX(w->styleCombo));
    bool isCustom = (styleIndex == 3);
    gtk_widget_set_sensitive(w->customPathChooser, isCustom);
    // Invert only applies to the shapes we draw ourselves -- a custom
    // cursor file's own colors are used as-is (see cursor_scheme.h).
    gtk_widget_set_sensitive(w->invertCheck, !isCustom);
}

void SaveFromWidgets(Widgets* w) {
    g_settings.blurIntensity =
        static_cast<float>(gtk_range_get_value(GTK_RANGE(w->blurScale)));
    g_settings.trailLength =
        static_cast<int>(gtk_range_get_value(GTK_RANGE(w->trailScale)));
    g_settings.ghostScale =
        static_cast<float>(gtk_range_get_value(GTK_RANGE(w->ghostScale)));
    g_settings.rotationIntensity =
        static_cast<float>(gtk_range_get_value(GTK_RANGE(w->rotationScale)));
    g_settings.springSpeed =
        static_cast<float>(gtk_range_get_value(GTK_RANGE(w->speedScale)));

    int styleIndex = gtk_combo_box_get_active(GTK_COMBO_BOX(w->styleCombo));
    if (styleIndex < 0) styleIndex = 0;
    g_settings.layer1Style = kStyleIds[styleIndex];

    g_settings.layer1Invert =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->invertCheck));

    char* customPath =
        gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(w->customPathChooser));
    g_settings.layer1CustomCursorPath = customPath ? customPath : "";
    g_free(customPath);

    const char* text = gtk_entry_get_text(GTK_ENTRY(w->excludeEntry));
    g_settings.extraExcludedProcesses = SplitExcludeList(text);
    config::Save(g_settings);

    UpdateStyleWidgetsSensitivity(w);
}

void OnScaleChanged(GtkRange*, gpointer userData) {
    SaveFromWidgets(static_cast<Widgets*>(userData));
}

void OnEntryChanged(GtkEditable*, gpointer userData) {
    SaveFromWidgets(static_cast<Widgets*>(userData));
}

void OnStyleChanged(GtkComboBox*, gpointer userData) {
    SaveFromWidgets(static_cast<Widgets*>(userData));
}

void OnCustomPathChanged(GtkFileChooserButton*, gpointer userData) {
    SaveFromWidgets(static_cast<Widgets*>(userData));
}

void OnInvertToggled(GtkToggleButton*, gpointer userData) {
    SaveFromWidgets(static_cast<Widgets*>(userData));
}

void OnStartupToggled(GtkToggleButton* btn, gpointer) {
    SetRunAtStartup(gtk_toggle_button_get_active(btn));
}

// Forces the overlay to fully rebuild Layer 1 from scratch (all cursor
// roles re-applied) even though style/invert/path haven't changed --
// recovery for when a custom cursor file occasionally fails to reapply
// cleanly. See config.h's layer1ReloadToken.
void OnReloadClicked(GtkButton*, gpointer userData) {
    g_settings.layer1ReloadToken++;
    SaveFromWidgets(static_cast<Widgets*>(userData));
}

gchar* FormatMultiplier(GtkScale*, gdouble value, gpointer) {
    return g_strdup_printf("%.2fx", value);
}

gchar* FormatCount(GtkScale*, gdouble value, gpointer) {
    return g_strdup_printf("%.0f", value);
}

GtkWidget* AddSection(GtkWidget* container, const char* title, GtkWidget* scale) {
    GtkWidget* label = gtk_label_new(title);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    GtkStyleContext* labelStyle = gtk_widget_get_style_context(label);
    gtk_style_context_add_class(labelStyle, "section");
    gtk_box_pack_start(GTK_BOX(container), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(container), scale, FALSE, FALSE, 4);
    return scale;
}

}  // namespace

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);

    g_settings = config::Load();

    GtkCssProvider* cssProvider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(cssProvider, kCss, -1, nullptr);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(cssProvider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "CursorFlow - Settings");
    gtk_window_set_default_size(GTK_WINDOW(window), 440, 740);
    gtk_container_set_border_width(GTK_CONTAINER(window), 24);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(window), box);

    auto* widgets = new Widgets{};

    widgets->blurScale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                    0.0, 2.0, 0.01);
    gtk_range_set_value(GTK_RANGE(widgets->blurScale), g_settings.blurIntensity);
    gtk_scale_set_value_pos(GTK_SCALE(widgets->blurScale), GTK_POS_RIGHT);
    g_signal_connect(widgets->blurScale, "format-value",
                      G_CALLBACK(FormatMultiplier), nullptr);
    AddSection(box, "Blur Intensity", widgets->blurScale);

    widgets->trailScale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                     0.0, 100.0, 1.0);
    gtk_range_set_value(GTK_RANGE(widgets->trailScale), g_settings.trailLength);
    gtk_scale_set_value_pos(GTK_SCALE(widgets->trailScale), GTK_POS_RIGHT);
    g_signal_connect(widgets->trailScale, "format-value",
                      G_CALLBACK(FormatCount), nullptr);
    AddSection(box, "Trail Length", widgets->trailScale);

    widgets->ghostScale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                     0.1, 4.0, 0.01);
    gtk_range_set_value(GTK_RANGE(widgets->ghostScale), g_settings.ghostScale);
    gtk_scale_set_value_pos(GTK_SCALE(widgets->ghostScale), GTK_POS_RIGHT);
    g_signal_connect(widgets->ghostScale, "format-value",
                      G_CALLBACK(FormatMultiplier), nullptr);
    AddSection(box, "Ghost Size", widgets->ghostScale);

    widgets->rotationScale = gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL, 0.0, 3.0, 0.01);
    gtk_range_set_value(GTK_RANGE(widgets->rotationScale),
                         g_settings.rotationIntensity);
    gtk_scale_set_value_pos(GTK_SCALE(widgets->rotationScale), GTK_POS_RIGHT);
    g_signal_connect(widgets->rotationScale, "format-value",
                      G_CALLBACK(FormatMultiplier), nullptr);
    AddSection(box, "Rotation Power", widgets->rotationScale);

    widgets->speedScale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                     0.1, 5.0, 0.01);
    gtk_range_set_value(GTK_RANGE(widgets->speedScale), g_settings.springSpeed);
    gtk_scale_set_value_pos(GTK_SCALE(widgets->speedScale), GTK_POS_RIGHT);
    g_signal_connect(widgets->speedScale, "format-value",
                      G_CALLBACK(FormatMultiplier), nullptr);
    AddSection(box, "Follow Speed (Snappiness)", widgets->speedScale);

    GtkWidget* styleLabel = gtk_label_new("Front Cursor (Layer 1) Shape");
    gtk_widget_set_halign(styleLabel, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(styleLabel), "section");
    gtk_box_pack_start(GTK_BOX(box), styleLabel, FALSE, FALSE, 4);

    widgets->styleCombo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->styleCombo),
                                    "Thin Cross");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->styleCombo),
                                    "Thick Cross (Default)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->styleCombo),
                                    "Dot");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->styleCombo),
                                    "Custom...");
    gtk_combo_box_set_active(GTK_COMBO_BOX(widgets->styleCombo),
                              StyleToIndex(g_settings.layer1Style));
    gtk_box_pack_start(GTK_BOX(box), widgets->styleCombo, FALSE, FALSE, 4);

    widgets->invertCheck = gtk_check_button_new_with_label("Invert Colors");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets->invertCheck),
                                  g_settings.layer1Invert);
    gtk_box_pack_start(GTK_BOX(box), widgets->invertCheck, FALSE, FALSE, 4);

    widgets->startupCheck = gtk_check_button_new_with_label("Run at Startup");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets->startupCheck),
                                  QueryRunAtStartup());
    gtk_box_pack_start(GTK_BOX(box), widgets->startupCheck, FALSE, FALSE, 8);

    GtkWidget* customLabel = gtk_label_new("Custom Cursor File");
    gtk_widget_set_halign(customLabel, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(customLabel), "section");
    gtk_box_pack_start(GTK_BOX(box), customLabel, FALSE, FALSE, 4);

    GtkWidget* customHelp = gtk_label_new(
        "Xcursor-format file -- ideally 32x32px (up to 256x256)");
    gtk_widget_set_halign(customHelp, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(customHelp), "hint");
    gtk_box_pack_start(GTK_BOX(box), customHelp, FALSE, FALSE, 2);

    GtkWidget* customRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    widgets->customPathChooser = gtk_file_chooser_button_new(
        "Select a custom cursor file", GTK_FILE_CHOOSER_ACTION_OPEN);
    if (!g_settings.layer1CustomCursorPath.empty()) {
        gtk_file_chooser_set_filename(
            GTK_FILE_CHOOSER(widgets->customPathChooser),
            g_settings.layer1CustomCursorPath.c_str());
    }
    gtk_widget_set_hexpand(widgets->customPathChooser, TRUE);
    gtk_box_pack_start(GTK_BOX(customRow), widgets->customPathChooser, TRUE, TRUE, 0);

    // Recovery button: forces a full reapply of Layer 1 without needing
    // to restart the overlay -- see OnReloadClicked's comment.
    widgets->reloadButton = gtk_button_new_with_label("Reload");
    gtk_box_pack_start(GTK_BOX(customRow), widgets->reloadButton, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), customRow, FALSE, FALSE, 4);

    GtkWidget* excludeLabel = gtk_label_new("Excluded Processes");
    gtk_widget_set_halign(excludeLabel, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(excludeLabel), "section");
    gtk_box_pack_start(GTK_BOX(box), excludeLabel, FALSE, FALSE, 12);

    GtkWidget* excludeHelp = gtk_label_new(
        "The overlay automatically disables itself while these programs are "
        "in the foreground -- e.g. fullscreen games or anti-cheat-protected "
        "apps. Separate with commas.");
    gtk_widget_set_halign(excludeHelp, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(excludeHelp), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(excludeHelp), "hint");
    gtk_box_pack_start(GTK_BOX(box), excludeHelp, FALSE, FALSE, 2);

    widgets->excludeEntry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(widgets->excludeEntry), JoinExcludeList(g_settings).c_str());
    gtk_entry_set_placeholder_text(GTK_ENTRY(widgets->excludeEntry), "e.g. game, csgo");
    gtk_box_pack_start(GTK_BOX(box), widgets->excludeEntry, FALSE, FALSE, 4);

    GtkWidget* hint = gtk_label_new(
        "Changes apply automatically within about a second.");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "hint");
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 16);

    g_signal_connect(widgets->blurScale, "value-changed",
                      G_CALLBACK(OnScaleChanged), widgets);
    g_signal_connect(widgets->trailScale, "value-changed",
                      G_CALLBACK(OnScaleChanged), widgets);
    g_signal_connect(widgets->ghostScale, "value-changed",
                      G_CALLBACK(OnScaleChanged), widgets);
    g_signal_connect(widgets->rotationScale, "value-changed",
                      G_CALLBACK(OnScaleChanged), widgets);
    g_signal_connect(widgets->speedScale, "value-changed",
                      G_CALLBACK(OnScaleChanged), widgets);
    g_signal_connect(widgets->styleCombo, "changed",
                      G_CALLBACK(OnStyleChanged), widgets);
    g_signal_connect(widgets->invertCheck, "toggled",
                      G_CALLBACK(OnInvertToggled), widgets);
    g_signal_connect(widgets->startupCheck, "toggled",
                      G_CALLBACK(OnStartupToggled), widgets);
    g_signal_connect(widgets->customPathChooser, "file-set",
                      G_CALLBACK(OnCustomPathChanged), widgets);
    g_signal_connect(widgets->reloadButton, "clicked",
                      G_CALLBACK(OnReloadClicked), widgets);
    g_signal_connect(widgets->excludeEntry, "changed",
                      G_CALLBACK(OnEntryChanged), widgets);

    UpdateStyleWidgetsSensitivity(widgets);

    gtk_widget_show_all(window);
    gtk_main();

    delete widgets;
    return 0;
}
