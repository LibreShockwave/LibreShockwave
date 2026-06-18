#include "EditorApp.hpp"
#include "EditorStyle.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/cast/FilmLoopInfo.hpp"
#include "libreshockwave/cast/MemberType.hpp"
#include "libreshockwave/cast/ShapeInfo.hpp"
#include "libreshockwave/cast/Shockwave3DInfo.hpp"
#include "libreshockwave/chunks/CastChunk.hpp"
#include "libreshockwave/chunks/CastListChunk.hpp"
#include "libreshockwave/chunks/CastMemberChunk.hpp"
#include "libreshockwave/chunks/ConfigChunk.hpp"
#include "libreshockwave/chunks/PaletteChunk.hpp"
#include "libreshockwave/chunks/ScoreChunk.hpp"
#include "libreshockwave/chunks/ScriptChunk.hpp"
#include "libreshockwave/chunks/TextChunk.hpp"
#include "libreshockwave/format/ChunkType.hpp"
#include "libreshockwave/lingo/decompiler/LingoDecompiler.hpp"
#include "libreshockwave/player/Player.hpp"

#include <libpanel.h>

#if LIBRESHOCKWAVE_HAVE_ZLIB
#include <zlib.h>
#endif

namespace {

namespace fs = std::filesystem;

struct EditorUi;

struct StageTransform {
    double x = 0.0;
    double y = 0.0;
    double scale = 1.0;
};

struct CastLocation {
    int castLib = 1;
    int member = 0;
};

struct CastPreviewData {
    std::vector<std::uint32_t> pixels;
    int width = 0;
    int height = 0;
    std::string emptyMessage = "No preview";
};

struct ParamRow {
    GtkWidget* rowBox = nullptr;
    GtkWidget* keyEntry = nullptr;
    GtkWidget* valueEntry = nullptr;
    GtkWidget* removeButton = nullptr;
};

struct ParamsDialogData {
    EditorUi* ui = nullptr;
    GtkWidget* rowsBox = nullptr;
    GtkWidget* errorLabel = nullptr;
    std::vector<ParamRow> rows;
};

struct ExportRequest {
    EditorUi* ui = nullptr;
    bool exportAll = false;
    int memberIndex = -1;
};

enum class AssetFilter {
    All,
    Bitmap,
    Text,
    Script,
    Sound,
    Palette,
    Shape,
    FilmLoop,
    Movie,
    ThreeD,
    Button,
    Transition,
    Xtra,
    Font,
    Other,
};

enum class CastBrowserMode {
    List,
    Preview,
};

struct EditorUi {
    std::string initialProjectPath;
    std::string workspacePath;
    fs::path settingsPath;
    std::vector<std::string> recentProjects;
    std::vector<std::pair<std::string, std::string>> externalParams;

    GtkApplication* app = nullptr;
    GtkWidget* window = nullptr;
    GtkWidget* rootStack = nullptr;
    GtkWidget* recentList = nullptr;
    GtkWidget* statusLabel = nullptr;
    GtkWidget* chromeTitleLabel = nullptr;
    GtkWidget* chromeSubtitleLabel = nullptr;
    GtkWidget* titleLabel = nullptr;
    GtkWidget* subtitleLabel = nullptr;
    GtkWidget* playButton = nullptr;
    GtkWidget* stopButton = nullptr;
    GtkWidget* paramsButton = nullptr;
    GtkWidget* revealStartCheck = nullptr;
    GtkWidget* revealEndCheck = nullptr;
    GtkWidget* revealBottomCheck = nullptr;
    std::vector<GtkWidget*> panelToggleChecks;
    GtkWidget* castViewDropDown = nullptr;
    std::vector<GtkWidget*> castLists;
    GtkWidget* frameLabel = nullptr;
    GtkWidget* stageArea = nullptr;
    GtkWidget* bitmapPreviewArea = nullptr;
    GtkWidget* palettePreviewArea = nullptr;
    GtkWidget* castList = nullptr;
    GtkWidget* chunkList = nullptr;
    GtkWidget* libraryList = nullptr;
    GtkWidget* scoreList = nullptr;
    GtkWidget* inspectorList = nullptr;
    GtkWidget* bitmapDetailList = nullptr;
    GtkWidget* textDetailList = nullptr;
    GtkWidget* scriptDetailList = nullptr;
    GtkWidget* scriptSourceView = nullptr;
    GtkWidget* scriptBytecodeView = nullptr;
    GtkWidget* soundDetailList = nullptr;
    GtkWidget* paletteDetailList = nullptr;
    GtkWidget* shapeDetailList = nullptr;
    GtkWidget* filmLoopDetailList = nullptr;
    GtkWidget* movieDetailList = nullptr;
    GtkWidget* threeDDetailList = nullptr;
    GtkWidget* genericDetailList = nullptr;

    PanelWidget* stagePanel = nullptr;
    PanelWidget* toolsPanel = nullptr;
    PanelWidget* scorePanel = nullptr;
    PanelWidget* libraryPanel = nullptr;
    PanelWidget* inspectorPanel = nullptr;
    PanelWidget* bitmapPanel = nullptr;
    PanelWidget* textPanel = nullptr;
    PanelWidget* scriptPanel = nullptr;
    PanelWidget* soundPanel = nullptr;
    PanelWidget* palettePanel = nullptr;
    PanelWidget* shapePanel = nullptr;
    PanelWidget* filmLoopPanel = nullptr;
    PanelWidget* moviePanel = nullptr;
    PanelWidget* threeDPanel = nullptr;
    PanelWidget* genericPanel = nullptr;
    PanelWidget* chunksPanel = nullptr;
    std::unordered_map<int, PanelWidget*> castBrowserPanels;

    PanelDock* dock = nullptr;
    PanelGrid* centerGrid = nullptr;

    std::shared_ptr<libreshockwave::DirectorFile> file;
    std::unique_ptr<libreshockwave::player::Player> player;
    std::string currentProjectPath;
    std::vector<std::shared_ptr<libreshockwave::chunks::CastMemberChunk>> castMembers;
    std::unordered_map<int, CastLocation> castLocationsByChunkId;
    AssetFilter filter = AssetFilter::All;
    CastBrowserMode castBrowserMode = CastBrowserMode::List;
    int selectedMemberIndex = -1;
    int selectedGenericMemberIndex = -1;
    std::unordered_map<int, int> selectedMemberByFilter;
    bool refreshingCastLists = false;
    bool syncingPanelToggleChecks = false;
    bool restoringPanelSelection = false;

    std::vector<std::uint32_t> stagePixels;
    int stageWidth = 0;
    int stageHeight = 0;
    std::vector<std::uint32_t> previewPixels;
    int previewWidth = 0;
    int previewHeight = 0;
    std::vector<std::uint32_t> palettePixels;
    int paletteWidth = 0;
    int paletteHeight = 0;

    guint playbackTimer = 0;
    bool playing = false;
    bool applicationHeld = false;
    bool closeAfterWorkspaceSave = false;
};

std::string trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

std::string pathString(const fs::path& path) {
    return path.lexically_normal().string();
}

fs::path absolutePath(const fs::path& path) {
    if (path.empty() || path.is_absolute()) {
        return path;
    }
    return fs::absolute(path);
}

std::string sourceFileName(std::string_view source) {
    const fs::path path(source);
    const auto name = path.filename().string();
    return name.empty() ? std::string(source) : name;
}

fs::path defaultSettingsPath() {
    if (const char* xdgConfigHome = std::getenv("XDG_CONFIG_HOME");
        xdgConfigHome != nullptr && *xdgConfigHome != '\0') {
        return fs::path(xdgConfigHome) / "libreshockwave" / "editor.conf";
    }

    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return fs::path(home) / ".config" / "libreshockwave" / "editor.conf";
    }

    return fs::path(".libreshockwave-editor.conf");
}

std::vector<std::uint8_t> readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open project: " + pathString(path));
    }

    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end == std::ifstream::pos_type(-1)) {
        throw std::runtime_error("Unable to determine project size: " + pathString(path));
    }
    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> data(static_cast<std::size_t>(end));
    if (!data.empty()) {
        input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!input) {
            throw std::runtime_error("Unable to read complete project: " + pathString(path));
        }
    }
    return data;
}

void writeTextFile(const fs::path& path, std::string_view text) {
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path());
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("Unable to write file: " + pathString(path));
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        throw std::runtime_error("Unable to write complete file: " + pathString(path));
    }
}

void writeBytesFile(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path());
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("Unable to write file: " + pathString(path));
    }
    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!output) {
        throw std::runtime_error("Unable to write complete file: " + pathString(path));
    }
}

std::string safeFileStem(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_') {
            result.push_back(static_cast<char>(ch));
        } else if (ch == ' ' || ch == '.' || ch == ':') {
            result.push_back('_');
        }
    }

    while (!result.empty() && result.front() == '_') {
        result.erase(result.begin());
    }
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }
    if (result.empty()) {
        return "member";
    }
    return result;
}

std::optional<std::string> keyFileString(GKeyFile* keyFile, const char* group, const char* key) {
    GError* error = nullptr;
    char* raw = g_key_file_get_string(keyFile, group, key, &error);
    if (error != nullptr) {
        g_error_free(error);
        return std::nullopt;
    }
    std::string value = raw == nullptr ? "" : raw;
    g_free(raw);
    return value;
}

bool keyFileBool(GKeyFile* keyFile, const char* group, const char* key, bool fallback) {
    GError* error = nullptr;
    const gboolean value = g_key_file_get_boolean(keyFile, group, key, &error);
    if (error != nullptr) {
        g_error_free(error);
        return fallback;
    }
    return value;
}

int keyFileInt(GKeyFile* keyFile, const char* group, const char* key, int fallback) {
    GError* error = nullptr;
    const int value = g_key_file_get_integer(keyFile, group, key, &error);
    if (error != nullptr) {
        g_error_free(error);
        return fallback;
    }
    return value;
}

std::string keyName(std::string_view prefix, int index, std::string_view suffix) {
    return std::string(prefix) + std::to_string(index) + std::string(suffix);
}

bool hasWorkspaceExtension(std::string_view path) {
    std::string extension = fs::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension == ".libresw";
}

std::vector<std::string> loadRecentProjects(const fs::path& path) {
    std::ifstream input(path);
    if (!input) {
        return {};
    }

    std::vector<std::string> projects;
    std::string line;
    while (std::getline(input, line)) {
        const std::string value = trim(line);
        if (value.empty() || value.front() == '#') {
            continue;
        }
        constexpr std::string_view prefix = "recent=";
        if (value.starts_with(prefix)) {
            projects.push_back(std::string(value.substr(prefix.size())));
        }
    }
    return projects;
}

void saveRecentProjects(const std::vector<std::string>& projects, const fs::path& path) {
    if (path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
    }

    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        return;
    }

    output << "# LibreShockwave editor settings\n";
    for (const auto& project : projects) {
        output << "recent=" << project << '\n';
    }
}

void rememberProject(EditorUi& ui, std::string projectPath) {
    projectPath = pathString(absolutePath(projectPath));
    ui.recentProjects.erase(
        std::remove(ui.recentProjects.begin(), ui.recentProjects.end(), projectPath),
        ui.recentProjects.end());
    ui.recentProjects.insert(ui.recentProjects.begin(), std::move(projectPath));
    if (ui.recentProjects.size() > 12) {
        ui.recentProjects.resize(12);
    }
    saveRecentProjects(ui.recentProjects, ui.settingsPath);
}

void setStatus(EditorUi& ui, std::string_view message) {
    if (ui.statusLabel != nullptr) {
        gtk_label_set_text(GTK_LABEL(ui.statusLabel), std::string(message).c_str());
    }
}

void setMargins(GtkWidget* widget, int margin) {
    gtk_widget_set_margin_top(widget, margin);
    gtk_widget_set_margin_bottom(widget, margin);
    gtk_widget_set_margin_start(widget, margin);
    gtk_widget_set_margin_end(widget, margin);
}

GtkWidget* makeLabel(std::string_view text, const char* cssClass = nullptr) {
    GtkWidget* label = gtk_label_new(std::string(text).c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    if (cssClass != nullptr) {
        gtk_widget_add_css_class(label, cssClass);
    }
    return label;
}

GtkWidget* iconButton(const char* iconName, const char* tooltip) {
    GtkWidget* button = gtk_button_new_from_icon_name(iconName);
    gtk_widget_set_size_request(button, 36, 32);
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_widget_add_css_class(button, "flat");
    return button;
}

void clearListBox(GtkWidget* list) {
    while (GtkWidget* child = gtk_widget_get_first_child(list)) {
        gtk_list_box_remove(GTK_LIST_BOX(list), child);
    }
}

GtkWidget* makeKeyValueRow(std::string_view key, std::string_view value) {
    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(box, "property-row");
    setMargins(box, 6);

    GtkWidget* keyLabel = makeLabel(key, "property-key");
    GtkWidget* valueLabel = makeLabel(value, "property-value");
    gtk_label_set_selectable(GTK_LABEL(valueLabel), TRUE);
    gtk_widget_set_hexpand(valueLabel, TRUE);
    gtk_label_set_wrap(GTK_LABEL(valueLabel), TRUE);

    gtk_box_append(GTK_BOX(box), keyLabel);
    gtk_box_append(GTK_BOX(box), valueLabel);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    return row;
}

void setInspectorRows(EditorUi& ui, const std::vector<std::pair<std::string, std::string>>& rows) {
    if (ui.inspectorList == nullptr) {
        return;
    }
    clearListBox(ui.inspectorList);
    for (const auto& [key, value] : rows) {
        gtk_list_box_append(GTK_LIST_BOX(ui.inspectorList), makeKeyValueRow(key, value));
    }
}

void setListRows(GtkWidget* list, const std::vector<std::pair<std::string, std::string>>& rows) {
    if (list == nullptr) {
        return;
    }
    clearListBox(list);
    for (const auto& [key, value] : rows) {
        gtk_list_box_append(GTK_LIST_BOX(list), makeKeyValueRow(key, value));
    }
}

std::string boolText(bool value) {
    return value ? "yes" : "no";
}

std::string castBrowserModeText(CastBrowserMode mode) {
    switch (mode) {
        case CastBrowserMode::List: return "list";
        case CastBrowserMode::Preview: return "preview";
    }
    return "list";
}

CastBrowserMode castBrowserModeFromText(std::string_view value) {
    const std::string trimmed = trim(value);
    if (trimmed == "preview" || trimmed == "default" || trimmed == "thumbnail") {
        return CastBrowserMode::Preview;
    }
    return CastBrowserMode::List;
}

void setExternalParam(std::vector<std::pair<std::string, std::string>>& params,
                      std::string key,
                      std::string value) {
    if (key.empty()) {
        throw std::runtime_error("External parameter names cannot be empty");
    }

    for (auto& entry : params) {
        if (entry.first == key) {
            entry.second = std::move(value);
            return;
        }
    }
    params.emplace_back(std::move(key), std::move(value));
}

std::string dimensionsText(int width, int height) {
    return std::to_string(width) + " x " + std::to_string(height);
}

std::string scriptTypeName(libreshockwave::chunks::ScriptChunkType type) {
    using libreshockwave::chunks::ScriptChunkType;
    switch (type) {
        case ScriptChunkType::Score: return "score";
        case ScriptChunkType::Behavior: return "behavior";
        case ScriptChunkType::MovieScript: return "movie script";
        case ScriptChunkType::Parent: return "parent";
        case ScriptChunkType::Unknown: return "unknown";
    }
    return "unknown";
}

std::string castMemberScriptTypeName(libreshockwave::chunks::CastMemberScriptType type) {
    using libreshockwave::chunks::CastMemberScriptType;
    switch (type) {
        case CastMemberScriptType::Score: return "score";
        case CastMemberScriptType::Behavior: return "behavior";
        case CastMemberScriptType::MovieScript: return "movie script";
        case CastMemberScriptType::Parent: return "parent";
        case CastMemberScriptType::Unknown: return "unknown";
    }
    return "unknown";
}

std::string shapeTypeName(libreshockwave::cast::ShapeType type) {
    using libreshockwave::cast::ShapeType;
    switch (type) {
        case ShapeType::Rect: return "rectangle";
        case ShapeType::OvalRect: return "rounded rectangle";
        case ShapeType::Oval: return "oval";
        case ShapeType::Line: return "line";
        case ShapeType::Unknown: return "unknown";
    }
    return "unknown";
}

const char* iconForMember(const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member) {
    using libreshockwave::cast::MemberType;
    if (member == nullptr) {
        return "text-x-generic-symbolic";
    }

    switch (member->memberType()) {
        case MemberType::Bitmap:
        case MemberType::Picture:
            return "image-x-generic-symbolic";
        case MemberType::FilmLoop:
        case MemberType::Movie:
        case MemberType::DigitalVideo:
            return "video-x-generic-symbolic";
        case MemberType::Text:
        case MemberType::RichText:
            return "text-x-generic-symbolic";
        case MemberType::Sound:
            return "audio-x-generic-symbolic";
        case MemberType::Script:
            return "application-x-executable-symbolic";
        case MemberType::Palette:
            return "color-select-symbolic";
        case MemberType::Shape:
            return "insert-object-symbolic";
        case MemberType::Shockwave3D:
            return "applications-graphics-symbolic";
        default:
            return "text-x-generic-symbolic";
    }
}

bool memberMatchesFilter(const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member, AssetFilter filter) {
    if (member == nullptr) {
        return false;
    }
    using libreshockwave::cast::MemberType;
    switch (filter) {
        case AssetFilter::All: return true;
        case AssetFilter::Bitmap:
            return member->memberType() == MemberType::Bitmap || member->memberType() == MemberType::Picture;
        case AssetFilter::Text:
            return member->memberType() == MemberType::Text || member->memberType() == MemberType::RichText;
        case AssetFilter::Script:
            return member->memberType() == MemberType::Script;
        case AssetFilter::Sound:
            return member->memberType() == MemberType::Sound;
        case AssetFilter::Palette:
            return member->memberType() == MemberType::Palette;
        case AssetFilter::Shape:
            return member->memberType() == MemberType::Shape;
        case AssetFilter::FilmLoop:
            return member->memberType() == MemberType::FilmLoop;
        case AssetFilter::Movie:
            return member->memberType() == MemberType::Movie || member->memberType() == MemberType::DigitalVideo;
        case AssetFilter::ThreeD:
            return member->memberType() == MemberType::Shockwave3D;
        case AssetFilter::Button:
            return member->memberType() == MemberType::Button;
        case AssetFilter::Transition:
            return member->memberType() == MemberType::Transition;
        case AssetFilter::Xtra:
            return member->memberType() == MemberType::Xtra;
        case AssetFilter::Font:
            return member->memberType() == MemberType::Font;
        case AssetFilter::Other:
            return member->memberType() == MemberType::Null || member->memberType() == MemberType::Unknown;
    }
    return true;
}

AssetFilter filterForMember(const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member) {
    if (member == nullptr) {
        return AssetFilter::Other;
    }

    using libreshockwave::cast::MemberType;
    switch (member->memberType()) {
        case MemberType::Bitmap:
        case MemberType::Picture:
            return AssetFilter::Bitmap;
        case MemberType::Text:
        case MemberType::RichText:
            return AssetFilter::Text;
        case MemberType::Script:
            return AssetFilter::Script;
        case MemberType::Sound:
            return AssetFilter::Sound;
        case MemberType::Palette:
            return AssetFilter::Palette;
        case MemberType::Shape:
            return AssetFilter::Shape;
        case MemberType::FilmLoop:
            return AssetFilter::FilmLoop;
        case MemberType::Movie:
        case MemberType::DigitalVideo:
            return AssetFilter::Movie;
        case MemberType::Shockwave3D:
            return AssetFilter::ThreeD;
        case MemberType::Button:
            return AssetFilter::Button;
        case MemberType::Transition:
            return AssetFilter::Transition;
        case MemberType::Xtra:
            return AssetFilter::Xtra;
        case MemberType::Font:
            return AssetFilter::Font;
        case MemberType::Null:
        case MemberType::Unknown:
        default:
            return AssetFilter::Other;
    }
}

const char* iconForFilter(AssetFilter filter) {
    switch (filter) {
        case AssetFilter::All: return "view-list-symbolic";
        case AssetFilter::Bitmap: return "image-x-generic-symbolic";
        case AssetFilter::Text: return "text-x-generic-symbolic";
        case AssetFilter::Script: return "application-x-executable-symbolic";
        case AssetFilter::Sound: return "audio-x-generic-symbolic";
        case AssetFilter::Palette: return "color-select-symbolic";
        case AssetFilter::Shape: return "insert-object-symbolic";
        case AssetFilter::FilmLoop:
        case AssetFilter::Movie: return "video-x-generic-symbolic";
        case AssetFilter::ThreeD: return "applications-graphics-symbolic";
        case AssetFilter::Button: return "input-mouse-symbolic";
        case AssetFilter::Transition: return "view-refresh-symbolic";
        case AssetFilter::Xtra: return "application-x-addon-symbolic";
        case AssetFilter::Font: return "font-x-generic-symbolic";
        case AssetFilter::Other: return "text-x-generic-symbolic";
    }
    return "text-x-generic-symbolic";
}

struct CastPanelSpec {
    AssetFilter filter;
    const char* id;
    const char* title;
};

const std::vector<CastPanelSpec>& castPanelSpecs() {
    static const std::vector<CastPanelSpec> specs{
        {AssetFilter::All, "cast-all", "Cast: All"},
        {AssetFilter::Bitmap, "cast-bitmaps", "Cast: Bitmaps"},
        {AssetFilter::Text, "cast-text", "Cast: Text"},
        {AssetFilter::Script, "cast-scripts", "Cast: Scripts"},
        {AssetFilter::Sound, "cast-sounds", "Cast: Sounds"},
        {AssetFilter::Palette, "cast-palettes", "Cast: Palettes"},
        {AssetFilter::Shape, "cast-shapes", "Cast: Shapes"},
        {AssetFilter::FilmLoop, "cast-film-loops", "Cast: Film Loops"},
        {AssetFilter::Movie, "cast-movies", "Cast: Movies"},
        {AssetFilter::ThreeD, "cast-3d", "Cast: Shockwave 3D"},
        {AssetFilter::Button, "cast-buttons", "Cast: Buttons"},
        {AssetFilter::Transition, "cast-transitions", "Cast: Transitions"},
        {AssetFilter::Xtra, "cast-xtras", "Cast: Xtras"},
        {AssetFilter::Font, "cast-fonts", "Cast: Fonts"},
        {AssetFilter::Other, "cast-other", "Cast: Other"},
    };
    return specs;
}

int countMembersForFilter(const EditorUi& ui, AssetFilter filter) {
    return static_cast<int>(std::count_if(ui.castMembers.begin(), ui.castMembers.end(),
                                          [filter](const auto& member) {
                                              return memberMatchesFilter(member, filter);
                                          }));
}

std::string castFilterStorageName(AssetFilter filter) {
    switch (filter) {
        case AssetFilter::All: return "all";
        case AssetFilter::Bitmap: return "bitmap";
        case AssetFilter::Text: return "text";
        case AssetFilter::Script: return "script";
        case AssetFilter::Sound: return "sound";
        case AssetFilter::Palette: return "palette";
        case AssetFilter::Shape: return "shape";
        case AssetFilter::FilmLoop: return "film_loop";
        case AssetFilter::Movie: return "movie";
        case AssetFilter::ThreeD: return "shockwave_3d";
        case AssetFilter::Button: return "button";
        case AssetFilter::Transition: return "transition";
        case AssetFilter::Xtra: return "xtra";
        case AssetFilter::Font: return "font";
        case AssetFilter::Other: return "other";
    }
    return "all";
}

std::optional<AssetFilter> castFilterFromStorageName(std::string_view value) {
    for (const auto& spec : castPanelSpecs()) {
        if (value == castFilterStorageName(spec.filter)) {
            return spec.filter;
        }
    }
    return std::nullopt;
}

bool validMemberSelectionForFilter(const EditorUi& ui, AssetFilter filter, int memberIndex) {
    return memberIndex >= 0 &&
           memberIndex < static_cast<int>(ui.castMembers.size()) &&
           memberMatchesFilter(ui.castMembers[static_cast<std::size_t>(memberIndex)], filter);
}

int rememberedMemberForFilter(const EditorUi& ui, AssetFilter filter) {
    if (auto found = ui.selectedMemberByFilter.find(static_cast<int>(filter));
        found != ui.selectedMemberByFilter.end() &&
        validMemberSelectionForFilter(ui, filter, found->second)) {
        return found->second;
    }
    return -1;
}

void rememberMemberForFilter(EditorUi& ui, AssetFilter filter, int memberIndex) {
    if (validMemberSelectionForFilter(ui, filter, memberIndex)) {
        ui.selectedMemberByFilter[static_cast<int>(filter)] = memberIndex;
    } else {
        ui.selectedMemberByFilter.erase(static_cast<int>(filter));
    }
}

bool isGenericDetailFilter(AssetFilter filter) {
    return filter == AssetFilter::Button ||
           filter == AssetFilter::Transition ||
           filter == AssetFilter::Xtra ||
           filter == AssetFilter::Font ||
           filter == AssetFilter::Other;
}

int rememberedGenericMember(const EditorUi& ui) {
    if (ui.selectedGenericMemberIndex >= 0 &&
        ui.selectedGenericMemberIndex < static_cast<int>(ui.castMembers.size()) &&
        isGenericDetailFilter(filterForMember(ui.castMembers[static_cast<std::size_t>(ui.selectedGenericMemberIndex)]))) {
        return ui.selectedGenericMemberIndex;
    }

    for (const AssetFilter filter : {AssetFilter::Button,
                                     AssetFilter::Transition,
                                     AssetFilter::Xtra,
                                     AssetFilter::Font,
                                     AssetFilter::Other}) {
        const int memberIndex = rememberedMemberForFilter(ui, filter);
        if (memberIndex >= 0) {
            return memberIndex;
        }
    }
    return -1;
}

void rememberMemberSelection(EditorUi& ui, AssetFilter sourceFilter, int memberIndex) {
    rememberMemberForFilter(ui, sourceFilter, memberIndex);
    if (memberIndex >= 0 && memberIndex < static_cast<int>(ui.castMembers.size())) {
        const AssetFilter memberFilter = filterForMember(ui.castMembers[static_cast<std::size_t>(memberIndex)]);
        rememberMemberForFilter(ui, memberFilter, memberIndex);
        if (isGenericDetailFilter(memberFilter)) {
            ui.selectedGenericMemberIndex = memberIndex;
        }
    }
    ui.selectedMemberIndex = memberIndex;
}

std::string memberDisplayName(const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member) {
    if (member == nullptr) {
        return "(missing member)";
    }
    if (!member->name().empty()) {
        return member->name();
    }
    return std::string("(") + std::string(libreshockwave::cast::name(member->memberType())) + ")";
}

CastLocation locationForMember(const EditorUi& ui,
                               const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member) {
    if (member == nullptr) {
        return {};
    }
    if (const auto found = ui.castLocationsByChunkId.find(member->id().value());
        found != ui.castLocationsByChunkId.end()) {
        return found->second;
    }
    return CastLocation{1, member->id().value()};
}

std::string memberSlotText(const EditorUi& ui,
                           const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member) {
    const auto location = locationForMember(ui, member);
    return "cast " + std::to_string(location.castLib) + ", member " + std::to_string(location.member);
}

void buildCastLocationMap(EditorUi& ui) {
    ui.castLocationsByChunkId.clear();
    if (ui.file == nullptr) {
        return;
    }

    const auto casts = ui.file->casts();
    const auto castList = ui.file->castList();
    const auto config = ui.file->config();
    const int fallbackMinMember = config ? std::max(1, config->minMember()) : 1;

    for (std::size_t castIndex = 0; castIndex < casts.size(); ++castIndex) {
        const int castLib = static_cast<int>(castIndex) + 1;
        int minMember = fallbackMinMember;
        if (castList != nullptr && castIndex < castList->entries().size()) {
            minMember = std::max(1, castList->entries()[castIndex].minMember);
        }

        const auto& cast = casts[castIndex];
        if (cast == nullptr) {
            continue;
        }
        const auto& ids = cast->memberIds();
        for (std::size_t index = 0; index < ids.size(); ++index) {
            if (ids[index] <= 0) {
                continue;
            }
            ui.castLocationsByChunkId.emplace(ids[index], CastLocation{castLib, minMember + static_cast<int>(index)});
        }
    }
}

std::uint8_t blendChannel(std::uint8_t foreground, std::uint8_t background, std::uint8_t alpha) {
    return static_cast<std::uint8_t>(
        (static_cast<int>(foreground) * alpha + static_cast<int>(background) * (255 - alpha) + 127) / 255);
}

void setPixelsFromBitmap(std::vector<std::uint32_t>& target,
                         int& width,
                         int& height,
                         const libreshockwave::bitmap::Bitmap& bitmap,
                         int backgroundRgb = 0x202020) {
    width = bitmap.width();
    height = bitmap.height();
    target.clear();

    if (width <= 0 || height <= 0 || bitmap.pixels().size() != static_cast<std::size_t>(width * height)) {
        width = 0;
        height = 0;
        return;
    }

    const auto backgroundRed = static_cast<std::uint8_t>((backgroundRgb >> 16) & 0xFF);
    const auto backgroundGreen = static_cast<std::uint8_t>((backgroundRgb >> 8) & 0xFF);
    const auto backgroundBlue = static_cast<std::uint8_t>(backgroundRgb & 0xFF);

    target.reserve(bitmap.pixels().size());
    for (const std::uint32_t argb : bitmap.pixels()) {
        const auto alpha = static_cast<std::uint8_t>((argb >> 24) & 0xFF);
        const auto red = static_cast<std::uint8_t>((argb >> 16) & 0xFF);
        const auto green = static_cast<std::uint8_t>((argb >> 8) & 0xFF);
        const auto blue = static_cast<std::uint8_t>(argb & 0xFF);
        target.push_back(
            0xFF000000U |
            (static_cast<std::uint32_t>(blendChannel(red, backgroundRed, alpha)) << 16) |
            (static_cast<std::uint32_t>(blendChannel(green, backgroundGreen, alpha)) << 8) |
            static_cast<std::uint32_t>(blendChannel(blue, backgroundBlue, alpha)));
    }
}

void setPixelsFromColors(std::vector<std::uint32_t>& target,
                         int& width,
                         int& height,
                         const std::vector<std::uint32_t>& colors) {
    if (colors.empty()) {
        target.clear();
        width = 0;
        height = 0;
        return;
    }

    constexpr int swatchSize = 18;
    constexpr int columns = 16;
    const int rows = static_cast<int>((colors.size() + columns - 1) / columns);
    width = columns * swatchSize;
    height = rows * swatchSize;
    target.assign(static_cast<std::size_t>(width * height), 0xFF202020U);

    for (std::size_t colorIndex = 0; colorIndex < colors.size(); ++colorIndex) {
        const int column = static_cast<int>(colorIndex % columns);
        const int row = static_cast<int>(colorIndex / columns);
        const std::uint32_t color = 0xFF000000U | (colors[colorIndex] & 0x00FFFFFFU);
        for (int y = 0; y < swatchSize; ++y) {
            for (int x = 0; x < swatchSize; ++x) {
                const bool border = x == 0 || y == 0 || x == swatchSize - 1 || y == swatchSize - 1;
                target[static_cast<std::size_t>((row * swatchSize + y) * width + column * swatchSize + x)] =
                    border ? 0xFF111111U : color;
            }
        }
    }
}

std::optional<StageTransform> transformForSize(int contentWidth,
                                               int contentHeight,
                                               int widgetWidth,
                                               int widgetHeight) {
    if (contentWidth <= 0 || contentHeight <= 0 || widgetWidth <= 0 || widgetHeight <= 0) {
        return std::nullopt;
    }

    const double scale = std::min(static_cast<double>(widgetWidth) / static_cast<double>(contentWidth),
                                  static_cast<double>(widgetHeight) / static_cast<double>(contentHeight));
    if (scale <= 0.0 || !std::isfinite(scale)) {
        return std::nullopt;
    }

    const double targetWidth = static_cast<double>(contentWidth) * scale;
    const double targetHeight = static_cast<double>(contentHeight) * scale;
    return StageTransform{
        (static_cast<double>(widgetWidth) - targetWidth) / 2.0,
        (static_cast<double>(widgetHeight) - targetHeight) / 2.0,
        scale,
    };
}

void drawPixelBuffer(cairo_t* cr,
                     int width,
                     int height,
                     const std::vector<std::uint32_t>& pixels,
                     int pixelWidth,
                     int pixelHeight,
                     std::string_view emptyMessage) {
    cairo_set_source_rgb(cr, 0.08, 0.08, 0.08);
    cairo_paint(cr);

    if (pixels.empty() || pixelWidth <= 0 || pixelHeight <= 0) {
        cairo_set_source_rgb(cr, 0.58, 0.58, 0.58);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 14.0);
        std::string message(emptyMessage);
        cairo_text_extents_t extents{};
        cairo_text_extents(cr, message.c_str(), &extents);
        cairo_move_to(cr,
                      (static_cast<double>(width) - extents.width) / 2.0 - extents.x_bearing,
                      (static_cast<double>(height) - extents.height) / 2.0 - extents.y_bearing);
        cairo_show_text(cr, message.c_str());
        return;
    }

    const auto transform = transformForSize(pixelWidth, pixelHeight, width, height);
    if (!transform.has_value()) {
        return;
    }

    const double targetWidth = static_cast<double>(pixelWidth) * transform->scale;
    const double targetHeight = static_cast<double>(pixelHeight) * transform->scale;

    cairo_save(cr);
    cairo_rectangle(cr, transform->x, transform->y, targetWidth, targetHeight);
    cairo_clip(cr);
    cairo_translate(cr, transform->x, transform->y);
    cairo_scale(cr, transform->scale, transform->scale);
    cairo_surface_t* surface = cairo_image_surface_create_for_data(
        const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(pixels.data())),
        CAIRO_FORMAT_ARGB32,
        pixelWidth,
        pixelHeight,
        pixelWidth * 4);
    cairo_set_source_surface(cr, surface, 0.0, 0.0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_surface_destroy(surface);
    cairo_restore(cr);
}

void drawStage(GtkDrawingArea*, cairo_t* cr, int width, int height, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    drawPixelBuffer(cr, width, height, ui.stagePixels, ui.stageWidth, ui.stageHeight, "No movie loaded");
}

void drawAssetPreview(GtkDrawingArea*, cairo_t* cr, int width, int height, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    drawPixelBuffer(cr, width, height, ui.previewPixels, ui.previewWidth, ui.previewHeight, "Select a bitmap");
}

void drawPalettePreview(GtkDrawingArea*, cairo_t* cr, int width, int height, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    drawPixelBuffer(cr, width, height, ui.palettePixels, ui.paletteWidth, ui.paletteHeight, "Select a palette");
}

void drawCastPreview(GtkDrawingArea*, cairo_t* cr, int width, int height, gpointer userData) {
    const auto& data = *static_cast<CastPreviewData*>(userData);
    drawPixelBuffer(cr, width, height, data.pixels, data.width, data.height, data.emptyMessage);
}

void destroyCastPreviewData(gpointer userData) {
    delete static_cast<CastPreviewData*>(userData);
}

void updateWindowLabels(EditorUi& ui) {
    const bool loaded = ui.file != nullptr;
    const std::string title = loaded ? sourceFileName(ui.currentProjectPath) : "Director Studio";
    const std::string subtitle = loaded ? ui.currentProjectPath : "Open a Director or Shockwave project";

    if (ui.titleLabel != nullptr) {
        gtk_label_set_text(GTK_LABEL(ui.titleLabel), title.c_str());
    }
    if (ui.subtitleLabel != nullptr) {
        gtk_label_set_text(GTK_LABEL(ui.subtitleLabel), subtitle.c_str());
    }
    if (ui.chromeTitleLabel != nullptr) {
        gtk_label_set_text(GTK_LABEL(ui.chromeTitleLabel), loaded ? title.c_str() : "LibreShockwave Director Studio");
    }
    if (ui.chromeSubtitleLabel != nullptr) {
        gtk_label_set_text(GTK_LABEL(ui.chromeSubtitleLabel), subtitle.c_str());
    }
    if (ui.window != nullptr) {
        const std::string windowTitle = loaded ? title + " - LibreShockwave Director Studio"
                                               : "LibreShockwave Director Studio";
        gtk_window_set_title(GTK_WINDOW(ui.window), windowTitle.c_str());
    }
}

void updatePlaybackControls(EditorUi& ui) {
    const bool loaded = ui.player != nullptr;
    if (ui.playButton != nullptr) {
        gtk_widget_set_sensitive(ui.playButton, loaded);
        gtk_button_set_icon_name(GTK_BUTTON(ui.playButton),
                                 ui.playing ? "media-playback-pause-symbolic"
                                            : "media-playback-start-symbolic");
        gtk_widget_set_tooltip_text(ui.playButton, ui.playing ? "Pause" : "Play");
    }
    if (ui.stopButton != nullptr) {
        gtk_widget_set_sensitive(ui.stopButton, loaded);
    }
}

void renderCurrentFrame(EditorUi& ui) {
    if (ui.player == nullptr) {
        ui.stagePixels.clear();
        ui.stageWidth = 0;
        ui.stageHeight = 0;
        if (ui.frameLabel != nullptr) {
            gtk_label_set_text(GTK_LABEL(ui.frameLabel), "No movie");
        }
        if (ui.stageArea != nullptr) {
            gtk_widget_queue_draw(ui.stageArea);
        }
        updatePlaybackControls(ui);
        updateWindowLabels(ui);
        return;
    }

    const auto snapshot = ui.player->frameSnapshot();
    auto frame = snapshot.renderFrame();
    setPixelsFromBitmap(ui.stagePixels, ui.stageWidth, ui.stageHeight, frame, snapshot.backgroundColor);
    if (ui.stageArea != nullptr) {
        gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(ui.stageArea), std::max(320, ui.stageWidth));
        gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(ui.stageArea), std::max(240, ui.stageHeight));
        gtk_widget_queue_draw(ui.stageArea);
    }

    if (ui.frameLabel != nullptr) {
        std::ostringstream out;
        out << "Frame " << ui.player->currentFrame() << " / " << ui.player->frameCount()
            << "    Tempo " << ui.player->tempo() << " fps";
        gtk_label_set_text(GTK_LABEL(ui.frameLabel), out.str().c_str());
    }
    updatePlaybackControls(ui);
    updateWindowLabels(ui);
}

void cancelPlaybackTimer(EditorUi& ui) {
    if (ui.playbackTimer != 0) {
        g_source_remove(ui.playbackTimer);
        ui.playbackTimer = 0;
    }
}

gboolean playbackTick(gpointer userData);

void schedulePlaybackTick(EditorUi& ui) {
    if (!ui.playing || ui.player == nullptr || ui.playbackTimer != 0) {
        return;
    }
    const int tempo = std::clamp(ui.player->tempo(), 1, 240);
    const guint delay = static_cast<guint>(std::max(1, static_cast<int>(std::lround(1000.0 / tempo))));
    ui.playbackTimer = g_timeout_add_full(G_PRIORITY_DEFAULT, delay, playbackTick, &ui, nullptr);
}

gboolean playbackTick(gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    ui.playbackTimer = 0;
    if (!ui.playing || ui.player == nullptr) {
        return G_SOURCE_REMOVE;
    }

    try {
        ui.player->resume();
        const bool keepGoing = ui.player->tick();
        renderCurrentFrame(ui);
        if (!keepGoing) {
            ui.playing = false;
            updatePlaybackControls(ui);
            return G_SOURCE_REMOVE;
        }
    } catch (const std::exception& error) {
        ui.playing = false;
        setStatus(ui, error.what());
        updatePlaybackControls(ui);
        return G_SOURCE_REMOVE;
    }

    schedulePlaybackTick(ui);
    return G_SOURCE_REMOVE;
}

void setPlaying(EditorUi& ui, bool playing) {
    if (ui.player == nullptr) {
        return;
    }

    ui.playing = playing;
    if (playing) {
        ui.player->resume();
        schedulePlaybackTick(ui);
    } else {
        cancelPlaybackTimer(ui);
        ui.player->pause();
    }
    updatePlaybackControls(ui);
}

void playClicked(GtkButton*, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    setPlaying(ui, !ui.playing);
}

void stopClicked(GtkButton*, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    if (ui.player == nullptr) {
        return;
    }

    try {
        cancelPlaybackTimer(ui);
        ui.playing = false;
        ui.player->stop();
        renderCurrentFrame(ui);
        setStatus(ui, "Playback stopped");
    } catch (const std::exception& error) {
        setStatus(ui, error.what());
    }
}

PanelFrame* createFrameWithHeader() {
    auto* frame = PANEL_FRAME(panel_frame_new());
    auto* headerWidget = panel_frame_tab_bar_new();
    panel_frame_tab_bar_set_autohide(PANEL_FRAME_TAB_BAR(headerWidget), FALSE);
    panel_frame_tab_bar_set_expand_tabs(PANEL_FRAME_TAB_BAR(headerWidget), FALSE);
    auto* header = PANEL_FRAME_HEADER(headerWidget);
    panel_frame_set_header(frame, header);
    return frame;
}

gboolean adoptFrameWidget(PanelFrame*, PanelWidget* widget, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    const char* title = widget == nullptr ? nullptr : panel_widget_get_title(widget);
    setStatus(ui, std::string("Docked ") + (title == nullptr ? "panel" : title));
    return GDK_EVENT_PROPAGATE;
}

void allowFrameDocking(PanelFrame* frame, EditorUi& ui) {
    g_signal_connect(frame, "adopt-widget", G_CALLBACK(adoptFrameWidget), &ui);
}

PanelFrame* createGridFrame(PanelGrid*, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    auto* frame = createFrameWithHeader();
    allowFrameDocking(frame, ui);
    return frame;
}

PanelFrame* createDockFrame(PanelDock*, PanelPosition*, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    auto* frame = createFrameWithHeader();
    allowFrameDocking(frame, ui);
    return frame;
}

gboolean adoptDockWidget(PanelDock*, PanelWidget* widget, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    const char* title = widget == nullptr ? nullptr : panel_widget_get_title(widget);
    setStatus(ui, std::string("Docked ") + (title == nullptr ? "panel" : title));
    return GDK_EVENT_PROPAGATE;
}

PanelWidget* makePanelWidget(const char* id,
                             const char* title,
                             const char* iconName,
                             GtkWidget* child,
                             const char* kind = PANEL_WIDGET_KIND_DOCUMENT) {
    auto* panel = PANEL_WIDGET(panel_widget_new());
    panel_widget_set_id(panel, id);
    panel_widget_set_title(panel, title);
    panel_widget_set_tooltip(panel, title);
    panel_widget_set_icon_name(panel, iconName);
    panel_widget_set_kind(panel, kind);
    panel_widget_set_reorderable(panel, TRUE);
    panel_widget_set_can_maximize(panel, TRUE);
    panel_widget_set_child(panel, child);
    return panel;
}

void setFrameHeader(GtkWidget* frameWidget) {
    if (!PANEL_IS_FRAME(frameWidget)) {
        return;
    }
    auto* headerWidget = panel_frame_tab_bar_new();
    panel_frame_tab_bar_set_autohide(PANEL_FRAME_TAB_BAR(headerWidget), FALSE);
    panel_frame_tab_bar_set_expand_tabs(PANEL_FRAME_TAB_BAR(headerWidget), FALSE);
    auto* header = PANEL_FRAME_HEADER(headerWidget);
    panel_frame_set_header(PANEL_FRAME(frameWidget), header);
}

GtkWidget* makeScrolled(GtkWidget* child) {
    GtkWidget* scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), child);
    gtk_widget_set_hexpand(scrolled, TRUE);
    gtk_widget_set_vexpand(scrolled, TRUE);
    return scrolled;
}

GtkWidget* makeDetailList(GtkWidget*& list) {
    list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    return makeScrolled(list);
}

GtkWidget* makeReadonlyCodeView(GtkWidget*& view) {
    view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_NONE);
    gtk_widget_add_css_class(view, "code-preview");
    return makeScrolled(view);
}

void setTextViewText(GtkWidget* view, const std::string& text) {
    if (view == nullptr) {
        return;
    }
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_set_text(buffer, text.c_str(), -1);
}

GtkWidget* makeBitmapDetailContent(EditorUi& ui) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    ui.bitmapPreviewArea = gtk_drawing_area_new();
    gtk_widget_add_css_class(ui.bitmapPreviewArea, "asset-preview");
    gtk_widget_set_size_request(ui.bitmapPreviewArea, 260, 180);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(ui.bitmapPreviewArea), drawAssetPreview, &ui, nullptr);
    gtk_box_append(GTK_BOX(box), ui.bitmapPreviewArea);
    gtk_box_append(GTK_BOX(box), makeDetailList(ui.bitmapDetailList));
    return box;
}

GtkWidget* makeScriptDetailContent(EditorUi& ui) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    GtkWidget* details = makeDetailList(ui.scriptDetailList);
    gtk_widget_set_size_request(details, -1, 150);
    gtk_widget_set_vexpand(details, FALSE);
    gtk_box_append(GTK_BOX(box), details);

    GtkWidget* notebook = gtk_notebook_new();
    gtk_widget_set_hexpand(notebook, TRUE);
    gtk_widget_set_vexpand(notebook, TRUE);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                             makeReadonlyCodeView(ui.scriptSourceView),
                             gtk_label_new("Lingo"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                             makeReadonlyCodeView(ui.scriptBytecodeView),
                             gtk_label_new("Bytecode"));
    gtk_box_append(GTK_BOX(box), notebook);

    return box;
}

GtkWidget* makePaletteDetailContent(EditorUi& ui) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    ui.palettePreviewArea = gtk_drawing_area_new();
    gtk_widget_add_css_class(ui.palettePreviewArea, "asset-preview");
    gtk_widget_set_size_request(ui.palettePreviewArea, 260, 160);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(ui.palettePreviewArea), drawPalettePreview, &ui, nullptr);
    gtk_box_append(GTK_BOX(box), ui.palettePreviewArea);
    gtk_box_append(GTK_BOX(box), makeDetailList(ui.paletteDetailList));
    return box;
}

GtkWidget* makeToolsPanel() {
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    setMargins(grid, 8);

    const std::vector<std::pair<const char*, const char*>> tools{
        {"selection-mode-symbolic", "Select"},
        {"transform-move-symbolic", "Move"},
        {"draw-rectangle-symbolic", "Rectangle"},
        {"draw-ellipse-symbolic", "Ellipse"},
        {"insert-text-symbolic", "Text"},
        {"color-select-symbolic", "Color"},
        {"zoom-in-symbolic", "Zoom in"},
        {"zoom-out-symbolic", "Zoom out"},
        {"object-rotate-right-symbolic", "Rotate"},
        {"document-properties-symbolic", "Properties"},
    };

    int row = 0;
    int column = 0;
    for (const auto& [icon, tooltip] : tools) {
        GtkWidget* button = iconButton(icon, tooltip);
        gtk_widget_set_sensitive(button, FALSE);
        gtk_grid_attach(GTK_GRID(grid), button, column, row, 1, 1);
        ++column;
        if (column == 3) {
            column = 0;
            ++row;
        }
    }

    return grid;
}

GtkWidget* makeInfoTile(std::string_view title, std::string_view subtitle, const char* iconName) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    setMargins(box, 6);
    gtk_widget_add_css_class(box, "asset-row");

    GtkWidget* icon = gtk_image_new_from_icon_name(iconName);
    gtk_widget_set_size_request(icon, 24, 24);
    GtkWidget* labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* titleLabel = makeLabel(title, "asset-title");
    GtkWidget* subtitleLabel = makeLabel(subtitle, "asset-subtitle");
    gtk_box_append(GTK_BOX(labels), titleLabel);
    gtk_box_append(GTK_BOX(labels), subtitleLabel);
    gtk_widget_set_hexpand(labels, TRUE);

    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), labels);
    return box;
}

GtkWidget* makeCastPreviewWidget(EditorUi& ui,
                                 const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member) {
    using libreshockwave::cast::MemberType;

    const auto makeArea = [](CastPreviewData* data) {
        GtkWidget* area = gtk_drawing_area_new();
        gtk_widget_add_css_class(area, "cast-preview");
        gtk_widget_set_size_request(area, 58, 44);
        gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), 58);
        gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), 44);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), drawCastPreview, data, destroyCastPreviewData);
        return area;
    };

    if (ui.file != nullptr &&
        (member->memberType() == MemberType::Bitmap || member->memberType() == MemberType::Picture)) {
        auto* data = new CastPreviewData;
        data->emptyMessage = "Bitmap";
        try {
            if (auto bitmap = ui.file->decodeBitmap(member)) {
                setPixelsFromBitmap(data->pixels, data->width, data->height, *bitmap);
            }
        } catch (const std::exception&) {
            data->emptyMessage = "Error";
        }
        return makeArea(data);
    }

    if (ui.file != nullptr && member->memberType() == MemberType::Palette) {
        auto* data = new CastPreviewData;
        data->emptyMessage = "Palette";
        try {
            const auto location = locationForMember(ui, member);
            if (auto palette = ui.file->resolvePaletteByMemberNumber(location.member)) {
                setPixelsFromColors(data->pixels, data->width, data->height, palette->colors());
            }
        } catch (const std::exception&) {
            data->emptyMessage = "Error";
        }
        return makeArea(data);
    }

    GtkWidget* icon = gtk_image_new_from_icon_name(iconForMember(member));
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 32);
    gtk_widget_set_size_request(icon, 58, 44);
    gtk_widget_add_css_class(icon, "cast-preview-icon");
    return icon;
}

GtkWidget* makeCastPreviewTile(EditorUi& ui,
                               const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member,
                               std::string_view title,
                               std::string_view subtitle) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    setMargins(box, 6);
    gtk_widget_add_css_class(box, "asset-row");
    gtk_widget_add_css_class(box, "cast-preview-row");

    GtkWidget* preview = makeCastPreviewWidget(ui, member);
    GtkWidget* labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* titleLabel = makeLabel(title, "asset-title");
    GtkWidget* subtitleLabel = makeLabel(subtitle, "asset-subtitle");
    gtk_box_append(GTK_BOX(labels), titleLabel);
    gtk_box_append(GTK_BOX(labels), subtitleLabel);
    gtk_widget_set_hexpand(labels, TRUE);

    gtk_box_append(GTK_BOX(box), preview);
    gtk_box_append(GTK_BOX(box), labels);
    return box;
}

void setPreviewEmpty(EditorUi& ui);
void setPalettePreviewEmpty(EditorUi& ui);
void resetMemberDetailPanels(EditorUi& ui);
AssetFilter castFilterForList(GtkWidget* list);

std::vector<std::pair<std::string, std::string>> baseMemberRows(
    const EditorUi& ui,
    const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member) {
    const auto location = locationForMember(ui, member);
    return {
        {"Name", memberDisplayName(member)},
        {"Type", std::string(libreshockwave::cast::name(member->memberType()))},
        {"Slot", "cast " + std::to_string(location.castLib) + ", member " + std::to_string(location.member)},
        {"Chunk id", std::to_string(member->id().value())},
        {"Info bytes", std::to_string(member->info().size())},
        {"Data bytes", std::to_string(member->specificData().size())},
        {"Script id", std::to_string(member->scriptId())},
        {"Registration point", std::to_string(member->regPointX()) + ", " + std::to_string(member->regPointY())},
    };
}

void appendLinkedChunkRows(EditorUi& ui,
                           const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member,
                           std::vector<std::pair<std::string, std::string>>& rows) {
    if (ui.file == nullptr || member == nullptr) {
        return;
    }

    try {
        const auto linked = ui.file->getLinkedChunkInfoForMember(member);
        rows.emplace_back("Linked chunks", std::to_string(linked.size()));
        for (std::size_t index = 0; index < linked.size() && index < 12; ++index) {
            const auto& info = linked[index];
            rows.emplace_back("Linked #" + std::to_string(info.id.value()),
                              std::string(libreshockwave::format::fourCCString(info.type())) +
                                  "    " + std::to_string(info.length) + " bytes");
        }
    } catch (const std::exception& error) {
        rows.emplace_back("Linked chunks", error.what());
    }
}

void raisePanel(PanelWidget* panel) {
    if (panel != nullptr) {
        panel_widget_raise(panel);
    }
}

std::string formatRawBytesPreview(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) {
        return "Raw bytecode: empty\n";
    }

    constexpr std::size_t bytesPerLine = 16;
    constexpr std::size_t maxPreviewBytes = 8192;
    const std::size_t byteCount = std::min(bytes.size(), maxPreviewBytes);

    std::ostringstream out;
    out << "Raw bytecode (" << bytes.size() << " bytes";
    if (byteCount < bytes.size()) {
        out << ", first " << byteCount << " shown";
    }
    out << ")\n";

    for (std::size_t offset = 0; offset < byteCount; offset += bytesPerLine) {
        out << std::setfill('0') << std::setw(6) << std::hex << offset << "  ";
        const std::size_t lineEnd = std::min(offset + bytesPerLine, byteCount);
        for (std::size_t index = offset; index < lineEnd; ++index) {
            out << std::setw(2) << static_cast<int>(bytes[index]) << ' ';
        }
        out << std::dec << std::setfill(' ') << '\n';
    }

    return out.str();
}

std::vector<std::uint8_t> bitmapToPam(const libreshockwave::bitmap::Bitmap& bitmap) {
    std::ostringstream header;
    header << "P7\n"
           << "WIDTH " << bitmap.width() << "\n"
           << "HEIGHT " << bitmap.height() << "\n"
           << "DEPTH 4\n"
           << "MAXVAL 255\n"
           << "TUPLTYPE RGB_ALPHA\n"
           << "ENDHDR\n";

    const auto headerText = header.str();
    std::vector<std::uint8_t> bytes(headerText.begin(), headerText.end());
    bytes.reserve(bytes.size() + bitmap.pixels().size() * 4);
    for (const std::uint32_t argb : bitmap.pixels()) {
        bytes.push_back(static_cast<std::uint8_t>((argb >> 16) & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>((argb >> 8) & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>(argb & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>((argb >> 24) & 0xFF));
    }
    return bytes;
}

void appendBigEndianU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

#if LIBRESHOCKWAVE_HAVE_ZLIB
void appendPngChunk(std::vector<std::uint8_t>& png,
                    const std::array<char, 4>& type,
                    const std::vector<std::uint8_t>& data) {
    appendBigEndianU32(png, static_cast<std::uint32_t>(data.size()));
    const std::size_t typeOffset = png.size();
    for (const char ch : type) {
        png.push_back(static_cast<std::uint8_t>(ch));
    }
    png.insert(png.end(), data.begin(), data.end());

    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, png.data() + typeOffset, static_cast<uInt>(4 + data.size()));
    appendBigEndianU32(png, static_cast<std::uint32_t>(crc));
}

std::optional<std::vector<std::uint8_t>> bitmapToPng(const libreshockwave::bitmap::Bitmap& bitmap) {
    if (bitmap.width() <= 0 || bitmap.height() <= 0 ||
        bitmap.pixels().size() != static_cast<std::size_t>(bitmap.width() * bitmap.height())) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(bitmap.height()) * (1 + static_cast<std::size_t>(bitmap.width()) * 4));
    for (int y = 0; y < bitmap.height(); ++y) {
        raw.push_back(0);
        for (int x = 0; x < bitmap.width(); ++x) {
            const std::uint32_t argb = bitmap.pixels()[static_cast<std::size_t>(y * bitmap.width() + x)];
            raw.push_back(static_cast<std::uint8_t>((argb >> 16) & 0xFF));
            raw.push_back(static_cast<std::uint8_t>((argb >> 8) & 0xFF));
            raw.push_back(static_cast<std::uint8_t>(argb & 0xFF));
            raw.push_back(static_cast<std::uint8_t>((argb >> 24) & 0xFF));
        }
    }

    uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::uint8_t> compressed(compressedSize);
    const int status = compress2(compressed.data(), &compressedSize, raw.data(), static_cast<uLong>(raw.size()), Z_BEST_COMPRESSION);
    if (status != Z_OK) {
        return std::nullopt;
    }
    compressed.resize(compressedSize);

    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

    std::vector<std::uint8_t> ihdr;
    appendBigEndianU32(ihdr, static_cast<std::uint32_t>(bitmap.width()));
    appendBigEndianU32(ihdr, static_cast<std::uint32_t>(bitmap.height()));
    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);

    appendPngChunk(png, {'I', 'H', 'D', 'R'}, ihdr);
    appendPngChunk(png, {'I', 'D', 'A', 'T'}, compressed);
    appendPngChunk(png, {'I', 'E', 'N', 'D'}, {});
    return png;
}
#endif

void writeBitmapExport(const fs::path& base, const libreshockwave::bitmap::Bitmap& bitmap) {
#if LIBRESHOCKWAVE_HAVE_ZLIB
    if (auto png = bitmapToPng(bitmap)) {
        writeBytesFile(base.string() + ".png", *png);
        return;
    }
#endif
    writeBytesFile(base.string() + ".pam", bitmapToPam(bitmap));
}

std::string paletteText(const std::vector<std::uint32_t>& colors) {
    std::ostringstream out;
    for (std::size_t index = 0; index < colors.size(); ++index) {
        out << std::setw(3) << index << "  #"
            << std::hex << std::uppercase << std::setfill('0')
            << std::setw(6) << (colors[index] & 0x00FFFFFFU)
            << std::dec << std::nouppercase << std::setfill(' ') << '\n';
    }
    return out.str();
}

std::string exportBaseName(const EditorUi& ui,
                           const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member,
                           int memberIndex) {
    const auto location = locationForMember(ui, member);
    std::ostringstream stem;
    stem << std::setw(4) << std::setfill('0') << (memberIndex + 1)
         << "_cast" << location.castLib
         << "_member" << location.member
         << "_" << safeFileStem(memberDisplayName(member))
         << "_" << safeFileStem(libreshockwave::cast::name(member->memberType()));
    return stem.str();
}

std::string decompileScriptPreview(libreshockwave::DirectorFile& file,
                                   const std::shared_ptr<libreshockwave::chunks::ScriptChunk>& script) {
    auto names = file.getScriptNamesForScript(script);
    libreshockwave::lingo::decompiler::LingoDecompiler decompiler;
    return decompiler.decompile(*script, names.get());
}

std::string formatScriptBytecodePreview(libreshockwave::DirectorFile& file,
                                        const std::shared_ptr<libreshockwave::chunks::ScriptChunk>& script) {
    auto names = file.getScriptNamesForScript(script);
    libreshockwave::lingo::decompiler::LingoDecompiler decompiler;

    std::ostringstream out;
    if (script->handlers().empty()) {
        out << "No parsed handlers.\n\n";
    } else {
        for (const auto& handler : script->handlers()) {
            out << decompiler.formatHandlerBytecodeOnly(handler, names.get()) << '\n';
        }
    }
    out << formatRawBytesPreview(script->rawBytecode());
    return out.str();
}

void exportCastMember(EditorUi& ui,
                      const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member,
                      int memberIndex,
                      const fs::path& directory) {
    using libreshockwave::cast::MemberType;

    const auto base = directory / exportBaseName(ui, member, memberIndex);
    const auto location = locationForMember(ui, member);

    std::ostringstream metadata;
    metadata << "Name: " << memberDisplayName(member) << '\n'
             << "Type: " << libreshockwave::cast::name(member->memberType()) << '\n'
             << "Cast library: " << location.castLib << '\n'
             << "Member: " << location.member << '\n'
             << "Chunk id: " << member->id().value() << '\n'
             << "Info bytes: " << member->info().size() << '\n'
             << "Data bytes: " << member->specificData().size() << '\n'
             << "Script id: " << member->scriptId() << '\n'
             << "Registration point: " << member->regPointX() << ", " << member->regPointY() << '\n';
    writeTextFile(base.string() + ".metadata.txt", metadata.str());

    if (!member->info().empty()) {
        writeBytesFile(base.string() + ".info.bin", member->info());
    }
    if (!member->specificData().empty()) {
        writeBytesFile(base.string() + ".data.bin", member->specificData());
    }

    if (ui.file == nullptr) {
        return;
    }

    switch (member->memberType()) {
        case MemberType::Bitmap:
        case MemberType::Picture:
            if (auto bitmap = ui.file->decodeBitmap(member)) {
                writeBitmapExport(base, *bitmap);
            }
            break;
        case MemberType::Text:
        case MemberType::RichText:
            if (auto text = ui.file->getTextForMember(member)) {
                writeTextFile(base.string() + ".txt", text->text());
            }
            break;
        case MemberType::Script:
            if (auto script = ui.file->getScriptForCastMember(member)) {
                writeTextFile(base.string() + ".lingo", decompileScriptPreview(*ui.file, script));
                writeTextFile(base.string() + ".bytecode.txt", formatScriptBytecodePreview(*ui.file, script));
                if (!script->rawBytecode().empty()) {
                    writeBytesFile(base.string() + ".bytecode.bin", script->rawBytecode());
                }
            }
            break;
        case MemberType::Palette:
            if (auto palette = ui.file->resolvePaletteByMemberNumber(location.member)) {
                writeTextFile(base.string() + ".palette.txt", paletteText(palette->colors()));
                const auto swatch = libreshockwave::bitmap::Bitmap::createPaletteSwatch(*palette, 18);
                writeBitmapExport(base.string() + ".palette", swatch);
            }
            break;
        default:
            break;
    }
}

int selectedCastMemberIndex(EditorUi& ui) {
    if (ui.selectedMemberIndex < 0 ||
        ui.selectedMemberIndex >= static_cast<int>(ui.castMembers.size())) {
        return -1;
    }
    return ui.selectedMemberIndex;
}

int exportCastMembers(EditorUi& ui, bool exportAll, int memberIndex, const fs::path& directory) {
    if (ui.file == nullptr) {
        throw std::runtime_error("No project loaded.");
    }

    fs::create_directories(directory);

    int exported = 0;
    if (exportAll) {
        for (int index = 0; index < static_cast<int>(ui.castMembers.size()); ++index) {
            const auto& member = ui.castMembers[static_cast<std::size_t>(index)];
            if (member != nullptr) {
                exportCastMember(ui, member, index, directory);
                ++exported;
            }
        }
        return exported;
    }

    if (memberIndex < 0 || memberIndex >= static_cast<int>(ui.castMembers.size())) {
        throw std::runtime_error("No cast member selected.");
    }
    exportCastMember(ui, ui.castMembers[static_cast<std::size_t>(memberIndex)], memberIndex, directory);
    return 1;
}

void updateBitmapPanel(EditorUi& ui,
                       const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member,
                       std::vector<std::pair<std::string, std::string>>& inspectorRows) {
    auto rows = baseMemberRows(ui, member);
    try {
        if (auto bitmap = ui.file->decodeBitmap(member)) {
            rows.emplace_back("Bitmap", dimensionsText(bitmap->width(), bitmap->height()) +
                                            ", " + std::to_string(bitmap->bitDepth()) + "-bit");
            rows.emplace_back("Transparent pixels", boolText(bitmap->hasTransparentPixels()));
            rows.emplace_back("Translucent pixels", boolText(bitmap->hasTranslucentPixels()));
            setPixelsFromBitmap(ui.previewPixels, ui.previewWidth, ui.previewHeight, *bitmap);
            inspectorRows.insert(inspectorRows.end(), rows.end() - 3, rows.end());
        } else {
            rows.emplace_back("Bitmap", "decode unavailable");
            inspectorRows.emplace_back("Bitmap", "decode unavailable");
            setPreviewEmpty(ui);
        }
    } catch (const std::exception& error) {
        rows.emplace_back("Bitmap", error.what());
        inspectorRows.emplace_back("Bitmap", error.what());
        setPreviewEmpty(ui);
    }

    appendLinkedChunkRows(ui, member, rows);
    setListRows(ui.bitmapDetailList, rows);
    if (ui.bitmapPreviewArea != nullptr) {
        gtk_widget_queue_draw(ui.bitmapPreviewArea);
    }
    raisePanel(ui.bitmapPanel);
}

void updateTextPanel(EditorUi& ui,
                     const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member,
                     std::vector<std::pair<std::string, std::string>>& inspectorRows) {
    auto rows = baseMemberRows(ui, member);
    try {
        if (auto text = ui.file->getTextForMember(member)) {
            rows.emplace_back("Characters", std::to_string(text->text().size()));
            rows.emplace_back("Text runs", std::to_string(text->runs().size()));
            rows.emplace_back("Content", text->text());
            inspectorRows.emplace_back("Text", text->text().substr(0, std::min<std::size_t>(text->text().size(), 240)));
            inspectorRows.emplace_back("Text runs", std::to_string(text->runs().size()));
        } else {
            rows.emplace_back("Text", "no linked STXT/XMED text found");
            inspectorRows.emplace_back("Text", "no linked STXT/XMED text found");
        }
    } catch (const std::exception& error) {
        rows.emplace_back("Text", error.what());
        inspectorRows.emplace_back("Text", error.what());
    }

    appendLinkedChunkRows(ui, member, rows);
    setListRows(ui.textDetailList, rows);
    raisePanel(ui.textPanel);
}

void updateScriptPanel(EditorUi& ui,
                       const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member,
                       std::vector<std::pair<std::string, std::string>>& inspectorRows) {
    auto rows = baseMemberRows(ui, member);
    if (const auto scriptType = member->getScriptType()) {
        rows.emplace_back("Script type", castMemberScriptTypeName(*scriptType));
        inspectorRows.emplace_back("Script type", castMemberScriptTypeName(*scriptType));
    }

    try {
        if (auto script = ui.file->getScriptForCastMember(member)) {
            rows.emplace_back("Resolved script", script->displayName());
            rows.emplace_back("Chunk id", std::to_string(script->id().value()));
            rows.emplace_back("Resolved type", scriptTypeName(script->resolvedScriptType()));
            rows.emplace_back("Handlers", std::to_string(script->handlers().size()));
            rows.emplace_back("Properties", std::to_string(script->properties().size()));
            rows.emplace_back("Globals", std::to_string(script->globals().size()));
            rows.emplace_back("Raw bytecode", std::to_string(script->rawBytecode().size()) + " bytes");
            for (const auto& handler : script->handlers()) {
                rows.emplace_back("Handler", script->getHandlerName(handler) +
                                             "    " + std::to_string(handler.instructions.size()) + " ops");
            }

            try {
                setTextViewText(ui.scriptSourceView, decompileScriptPreview(*ui.file, script));
                rows.emplace_back("Lingo preview", "decompiled");
                inspectorRows.emplace_back("Lingo preview", "decompiled");
            } catch (const std::exception& error) {
                setTextViewText(ui.scriptSourceView, std::string("Unable to decompile script:\n") + error.what());
                rows.emplace_back("Lingo preview", error.what());
                inspectorRows.emplace_back("Lingo preview", error.what());
            }

            try {
                setTextViewText(ui.scriptBytecodeView, formatScriptBytecodePreview(*ui.file, script));
                rows.emplace_back("Bytecode preview", "disassembly and raw bytes");
                inspectorRows.emplace_back("Bytecode preview", "available");
            } catch (const std::exception& error) {
                setTextViewText(ui.scriptBytecodeView, std::string("Unable to format bytecode:\n") + error.what());
                rows.emplace_back("Bytecode preview", error.what());
                inspectorRows.emplace_back("Bytecode preview", error.what());
            }

            inspectorRows.emplace_back("Resolved script", script->displayName());
            inspectorRows.emplace_back("Handlers", std::to_string(script->handlers().size()));
            inspectorRows.emplace_back("Properties", std::to_string(script->properties().size()));
            inspectorRows.emplace_back("Globals", std::to_string(script->globals().size()));
        } else {
            rows.emplace_back("Resolved script", "not found");
            inspectorRows.emplace_back("Resolved script", "not found");
            setTextViewText(ui.scriptSourceView, "No Lingo script chunk resolved for this cast member.");
            setTextViewText(ui.scriptBytecodeView, "No bytecode chunk resolved for this cast member.");
        }
    } catch (const std::exception& error) {
        rows.emplace_back("Script", error.what());
        inspectorRows.emplace_back("Script", error.what());
        setTextViewText(ui.scriptSourceView, std::string("Unable to load script:\n") + error.what());
        setTextViewText(ui.scriptBytecodeView, std::string("Unable to load bytecode:\n") + error.what());
    }

    appendLinkedChunkRows(ui, member, rows);
    setListRows(ui.scriptDetailList, rows);
    raisePanel(ui.scriptPanel);
}

void updateSoundPanel(EditorUi& ui,
                      const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member) {
    auto rows = baseMemberRows(ui, member);
    rows.emplace_back("Decoder", "sound metadata only in this read-only panel");
    appendLinkedChunkRows(ui, member, rows);
    setListRows(ui.soundDetailList, rows);
    raisePanel(ui.soundPanel);
}

void updatePalettePanel(EditorUi& ui,
                        const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member,
                        std::vector<std::pair<std::string, std::string>>& inspectorRows) {
    auto rows = baseMemberRows(ui, member);
    const auto location = locationForMember(ui, member);
    try {
        if (auto palette = ui.file->resolvePaletteByMemberNumber(location.member)) {
            rows.emplace_back("Palette", palette->name());
            rows.emplace_back("Colors", std::to_string(palette->size()));
            setPixelsFromColors(ui.palettePixels, ui.paletteWidth, ui.paletteHeight, palette->colors());
            inspectorRows.emplace_back("Palette", palette->name());
            inspectorRows.emplace_back("Colors", std::to_string(palette->size()));
        } else {
            rows.emplace_back("Palette", "not resolved");
            inspectorRows.emplace_back("Palette", "not resolved");
            setPalettePreviewEmpty(ui);
        }
    } catch (const std::exception& error) {
        rows.emplace_back("Palette", error.what());
        inspectorRows.emplace_back("Palette", error.what());
        setPalettePreviewEmpty(ui);
    }
    appendLinkedChunkRows(ui, member, rows);
    setListRows(ui.paletteDetailList, rows);
    if (ui.palettePreviewArea != nullptr) {
        gtk_widget_queue_draw(ui.palettePreviewArea);
    }
    raisePanel(ui.palettePanel);
}

void updateShapePanel(EditorUi& ui,
                      const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member,
                      std::vector<std::pair<std::string, std::string>>& inspectorRows) {
    auto rows = baseMemberRows(ui, member);
    try {
        const auto shape = libreshockwave::cast::ShapeInfo::parse(member->specificData());
        rows.emplace_back("Shape", shapeTypeName(shape.shapeType));
        rows.emplace_back("Size", dimensionsText(shape.width, shape.height));
        rows.emplace_back("Filled", boolText(shape.isFilled()));
        rows.emplace_back("Line thickness", std::to_string(shape.lineThickness));
        rows.emplace_back("Foreground color", std::to_string(shape.color));
        rows.emplace_back("Background color", std::to_string(shape.backColor));
        inspectorRows.emplace_back("Shape", shapeTypeName(shape.shapeType));
        inspectorRows.emplace_back("Size", dimensionsText(shape.width, shape.height));
    } catch (const std::exception& error) {
        rows.emplace_back("Shape", error.what());
        inspectorRows.emplace_back("Shape", error.what());
    }
    setListRows(ui.shapeDetailList, rows);
    raisePanel(ui.shapePanel);
}

void updateFilmLoopPanel(EditorUi& ui,
                         const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member,
                         std::vector<std::pair<std::string, std::string>>& inspectorRows) {
    auto rows = baseMemberRows(ui, member);
    try {
        const auto filmLoop = libreshockwave::cast::FilmLoopInfo::parse(member->specificData());
        rows.emplace_back("Size", dimensionsText(filmLoop.width(), filmLoop.height()));
        rows.emplace_back("Centered", boolText(filmLoop.center));
        rows.emplace_back("Cropped", boolText(filmLoop.crop));
        rows.emplace_back("Sound", boolText(filmLoop.sound));
        rows.emplace_back("Loops", boolText(filmLoop.loops));
        inspectorRows.emplace_back("Film loop size", dimensionsText(filmLoop.width(), filmLoop.height()));
    } catch (const std::exception& error) {
        rows.emplace_back("Film loop", error.what());
        inspectorRows.emplace_back("Film loop", error.what());
    }
    appendLinkedChunkRows(ui, member, rows);
    setListRows(ui.filmLoopDetailList, rows);
    raisePanel(ui.filmLoopPanel);
}

void updateMoviePanel(EditorUi& ui,
                      const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member) {
    auto rows = baseMemberRows(ui, member);
    rows.emplace_back("Renderer", "movie/video metadata only in this read-only panel");
    appendLinkedChunkRows(ui, member, rows);
    setListRows(ui.movieDetailList, rows);
    raisePanel(ui.moviePanel);
}

void updateThreeDPanel(EditorUi& ui,
                       const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member,
                       std::vector<std::pair<std::string, std::string>>& inspectorRows) {
    auto rows = baseMemberRows(ui, member);
    try {
        const auto info = libreshockwave::cast::Shockwave3DInfo::parse(member->specificData());
        rows.emplace_back("World", info.worldName);
        rows.emplace_back("Default shader", info.defaultShaderName);
        rows.emplace_back("Texture", info.textureName);
        rows.emplace_back("Camera", info.cameraName);
        rows.emplace_back("Draw distance", std::to_string(info.drawDistance));
        rows.emplace_back("Ambient RGB", std::to_string(info.ambientR) + ", " +
                                         std::to_string(info.ambientG) + ", " +
                                         std::to_string(info.ambientB));
        inspectorRows.emplace_back("World", info.worldName);
        inspectorRows.emplace_back("Camera", info.cameraName);
    } catch (const std::exception& error) {
        rows.emplace_back("Shockwave 3D", error.what());
        inspectorRows.emplace_back("Shockwave 3D", error.what());
    }
    appendLinkedChunkRows(ui, member, rows);
    setListRows(ui.threeDDetailList, rows);
    raisePanel(ui.threeDPanel);
}

void updateGenericPanel(EditorUi& ui,
                        const std::shared_ptr<libreshockwave::chunks::CastMemberChunk>& member) {
    auto rows = baseMemberRows(ui, member);
    appendLinkedChunkRows(ui, member, rows);
    setListRows(ui.genericDetailList, rows);
    raisePanel(ui.genericPanel);
}

void populateInspectorForMovie(EditorUi& ui) {
    resetMemberDetailPanels(ui);
    if (ui.file == nullptr) {
        setInspectorRows(ui, {{"Selection", "No project loaded"}});
        return;
    }

    const auto score = ui.file->scoreChunk();
    std::vector<std::pair<std::string, std::string>> rows{
        {"Project", sourceFileName(ui.currentProjectPath)},
        {"Path", ui.currentProjectPath},
        {"Movie type", libreshockwave::format::toString(ui.file->movieType())},
        {"Director version", std::to_string(ui.file->version())},
        {"Afterburner", boolText(ui.file->isAfterburner())},
        {"Stage", dimensionsText(ui.file->stageWidth(), ui.file->stageHeight())},
        {"Tempo", std::to_string(ui.file->tempo()) + " fps"},
        {"Cast members", std::to_string(ui.file->castMembers().size())},
        {"Cast libraries", std::to_string(ui.file->casts().size())},
        {"Scripts", std::to_string(ui.file->scripts().size())},
        {"Palettes", std::to_string(ui.file->palettes().size())},
        {"Chunks", std::to_string(ui.file->chunkInfo().size())},
        {"Frames", score ? std::to_string(score->getFrameCount()) : "0"},
        {"Channels", std::to_string(ui.file->channelCount())},
        {"External casts", std::to_string(ui.file->getExternalCastPaths().size())},
    };
    setInspectorRows(ui, rows);
}

void setPreviewEmpty(EditorUi& ui) {
    ui.previewPixels.clear();
    ui.previewWidth = 0;
    ui.previewHeight = 0;
    if (ui.bitmapPreviewArea != nullptr) {
        gtk_widget_queue_draw(ui.bitmapPreviewArea);
    }
}

void setPalettePreviewEmpty(EditorUi& ui) {
    ui.palettePixels.clear();
    ui.paletteWidth = 0;
    ui.paletteHeight = 0;
    if (ui.palettePreviewArea != nullptr) {
        gtk_widget_queue_draw(ui.palettePreviewArea);
    }
}

void resetMemberDetailPanels(EditorUi& ui) {
    setPreviewEmpty(ui);
    setPalettePreviewEmpty(ui);
    setListRows(ui.bitmapDetailList, {{"Selection", "No bitmap member selected"}});
    setListRows(ui.textDetailList, {{"Selection", "No text member selected"}});
    setListRows(ui.scriptDetailList, {{"Selection", "No script member selected"}});
    setTextViewText(ui.scriptSourceView, "Select a script cast member to preview decompiled Lingo.");
    setTextViewText(ui.scriptBytecodeView, "Select a script cast member to preview Lingo bytecode.");
    setListRows(ui.soundDetailList, {{"Selection", "No sound member selected"}});
    setListRows(ui.paletteDetailList, {{"Selection", "No palette member selected"}});
    setListRows(ui.shapeDetailList, {{"Selection", "No shape member selected"}});
    setListRows(ui.filmLoopDetailList, {{"Selection", "No film loop member selected"}});
    setListRows(ui.movieDetailList, {{"Selection", "No movie/video member selected"}});
    setListRows(ui.threeDDetailList, {{"Selection", "No Shockwave 3D member selected"}});
    setListRows(ui.genericDetailList, {{"Selection", "No member selected"}});
}

GtkWidget* makeMenuActionButton(const char* label, GCallback callback, EditorUi& ui);

void populateInspectorForMember(EditorUi& ui, int index) {
    if (ui.file == nullptr || index < 0 || index >= static_cast<int>(ui.castMembers.size())) {
        return;
    }

    const auto member = ui.castMembers[static_cast<std::size_t>(index)];
    auto rows = baseMemberRows(ui, member);

    using libreshockwave::cast::MemberType;
    switch (member->memberType()) {
        case MemberType::Bitmap:
        case MemberType::Picture:
            updateBitmapPanel(ui, member, rows);
            break;
        case MemberType::Text:
        case MemberType::RichText:
            updateTextPanel(ui, member, rows);
            break;
        case MemberType::Script:
            updateScriptPanel(ui, member, rows);
            break;
        case MemberType::Sound:
            updateSoundPanel(ui, member);
            break;
        case MemberType::Palette:
            updatePalettePanel(ui, member, rows);
            break;
        case MemberType::Shape:
            updateShapePanel(ui, member, rows);
            break;
        case MemberType::FilmLoop:
            updateFilmLoopPanel(ui, member, rows);
            break;
        case MemberType::Movie:
        case MemberType::DigitalVideo:
            updateMoviePanel(ui, member);
            break;
        case MemberType::Shockwave3D:
            updateThreeDPanel(ui, member, rows);
            break;
        case MemberType::Button:
        case MemberType::Transition:
        case MemberType::Xtra:
        case MemberType::Font:
        case MemberType::Null:
        case MemberType::Unknown:
        default:
            updateGenericPanel(ui, member);
            break;
    }

    setInspectorRows(ui, rows);
}

void restoreRememberedMemberPanels(EditorUi& ui) {
    ui.restoringPanelSelection = true;
    for (const auto& spec : castPanelSpecs()) {
        if (spec.filter == AssetFilter::All) {
            continue;
        }

        const int memberIndex = rememberedMemberForFilter(ui, spec.filter);
        if (memberIndex >= 0) {
            populateInspectorForMember(ui, memberIndex);
        }
    }

    const int genericMemberIndex = rememberedGenericMember(ui);
    if (genericMemberIndex >= 0) {
        populateInspectorForMember(ui, genericMemberIndex);
    }

    if (ui.selectedMemberIndex >= 0 &&
        ui.selectedMemberIndex < static_cast<int>(ui.castMembers.size())) {
        populateInspectorForMember(ui, ui.selectedMemberIndex);
    }
    ui.restoringPanelSelection = false;
}

void populateInspectorForChunk(EditorUi& ui, int chunkId) {
    if (ui.file == nullptr) {
        return;
    }

    const auto info = ui.file->getChunkInfo(libreshockwave::id::ChunkId(chunkId));
    if (info == nullptr) {
        return;
    }

    std::vector<std::pair<std::string, std::string>> rows{
        {"Chunk id", std::to_string(chunkId)},
        {"Type", libreshockwave::format::toString(info->type())},
        {"FourCC", std::string(libreshockwave::format::fourCCString(info->type()))},
        {"Offset", std::to_string(info->offset)},
        {"Length", std::to_string(info->length)},
        {"Uncompressed length", std::to_string(info->uncompressedLength)},
    };
    setInspectorRows(ui, rows);
}

void castRowSelected(GtkListBox* list, GtkListBoxRow* row, gpointer userData) {
    if (row == nullptr) {
        return;
    }
    auto& ui = *static_cast<EditorUi*>(userData);
    if (ui.refreshingCastLists) {
        return;
    }
    const int memberIndex = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "member-index")) - 1;
    if (memberIndex < 0 || memberIndex >= static_cast<int>(ui.castMembers.size())) {
        return;
    }
    const AssetFilter filter = castFilterForList(GTK_WIDGET(list));
    rememberMemberSelection(ui, filter, memberIndex);
    populateInspectorForMember(ui, memberIndex);
}

void exportFolderResponse(GtkNativeDialog* dialog, int response, gpointer userData) {
    auto* request = static_cast<ExportRequest*>(userData);
    if (response == GTK_RESPONSE_ACCEPT) {
        GFile* file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        if (file != nullptr) {
            char* path = g_file_get_path(file);
            if (path != nullptr) {
                try {
                    const int count = exportCastMembers(*request->ui, request->exportAll, request->memberIndex, path);
                    setStatus(*request->ui, "Exported " + std::to_string(count) + " cast member" +
                                            (count == 1 ? "" : "s"));
                } catch (const std::exception& error) {
                    setStatus(*request->ui, error.what());
                }
                g_free(path);
            }
            g_object_unref(file);
        }
    }
    delete request;
    gtk_native_dialog_destroy(dialog);
}

void showExportFolderDialog(EditorUi& ui, bool exportAll, int memberIndex) {
    if (ui.file == nullptr) {
        setStatus(ui, "No project loaded.");
        return;
    }
    if (!exportAll && (memberIndex < 0 || memberIndex >= static_cast<int>(ui.castMembers.size()))) {
        setStatus(ui, "No cast member selected.");
        return;
    }

    GtkFileChooserNative* dialog = gtk_file_chooser_native_new(exportAll ? "Export All Cast Members" : "Export Cast Member",
                                                               GTK_WINDOW(ui.window),
                                                               GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                                               "_Export",
                                                               "_Cancel");
    auto* request = new ExportRequest{&ui, exportAll, memberIndex};
    g_signal_connect(dialog, "response", G_CALLBACK(exportFolderResponse), request);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}

void exportSelectedClicked(GtkButton*, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    showExportFolderDialog(ui, false, selectedCastMemberIndex(ui));
}

void exportAllClicked(GtkButton*, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    showExportFolderDialog(ui, true, -1);
}

void castContextPopoverClosed(GtkPopover* popover, gpointer) {
    gtk_widget_unparent(GTK_WIDGET(popover));
}

void castRowRightClick(GtkGestureClick* gesture, int, double x, double y, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    GtkWidget* row = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    if (row == nullptr) {
        return;
    }

    const int memberIndex = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "member-index")) - 1;
    if (memberIndex < 0 || memberIndex >= static_cast<int>(ui.castMembers.size())) {
        return;
    }

    if (GtkWidget* list = gtk_widget_get_ancestor(row, GTK_TYPE_LIST_BOX)) {
        rememberMemberSelection(ui, castFilterForList(list), memberIndex);
        gtk_list_box_select_row(GTK_LIST_BOX(list), GTK_LIST_BOX_ROW(row));
    } else {
        rememberMemberSelection(ui, filterForMember(ui.castMembers[static_cast<std::size_t>(memberIndex)]), memberIndex);
    }
    populateInspectorForMember(ui, memberIndex);

    GtkWidget* popover = gtk_popover_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    setMargins(box, 8);

    GtkWidget* exportSelected = makeMenuActionButton("Export Selected...", G_CALLBACK(exportSelectedClicked), ui);
    GtkWidget* exportAll = makeMenuActionButton("Export All...", G_CALLBACK(exportAllClicked), ui);
    gtk_box_append(GTK_BOX(box), exportSelected);
    gtk_box_append(GTK_BOX(box), exportAll);

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    gtk_widget_set_parent(popover, row);
    const GdkRectangle rectangle{static_cast<int>(x), static_cast<int>(y), 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rectangle);
    g_signal_connect(popover, "closed", G_CALLBACK(castContextPopoverClosed), nullptr);
    gtk_popover_popup(GTK_POPOVER(popover));
}

void chunkRowSelected(GtkListBox*, GtkListBoxRow* row, gpointer userData) {
    if (row == nullptr) {
        return;
    }
    auto& ui = *static_cast<EditorUi*>(userData);
    const int chunkId = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "chunk-id"));
    populateInspectorForChunk(ui, chunkId);
}

AssetFilter castFilterForList(GtkWidget* list) {
    const int value = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(list), "cast-filter")) - 1;
    if (value < 0 || value > static_cast<int>(AssetFilter::Other)) {
        return AssetFilter::All;
    }
    return static_cast<AssetFilter>(value);
}

bool castListForcesPreview(GtkWidget* list) {
    return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(list), "force-preview")) != 0;
}

void populateCastList(EditorUi& ui, GtkWidget* list) {
    clearListBox(list);
    if (ui.file == nullptr) {
        return;
    }

    const AssetFilter filter = castFilterForList(list);
    const bool usePreview = castListForcesPreview(list) || ui.castBrowserMode == CastBrowserMode::Preview;
    const int rememberedSelection = rememberedMemberForFilter(ui, filter);

    for (int index = 0; index < static_cast<int>(ui.castMembers.size()); ++index) {
        const auto& member = ui.castMembers[static_cast<std::size_t>(index)];
        if (!memberMatchesFilter(member, filter)) {
            continue;
        }

        GtkWidget* row = gtk_list_box_row_new();
        g_object_set_data(G_OBJECT(row), "member-index", GINT_TO_POINTER(index + 1));
        GtkGesture* rightClick = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rightClick), GDK_BUTTON_SECONDARY);
        g_signal_connect(rightClick, "pressed", G_CALLBACK(castRowRightClick), &ui);
        gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(rightClick));

        const std::string title = memberDisplayName(member);
        const std::string subtitle = memberSlotText(ui, member) + "    " +
                                     std::string(libreshockwave::cast::name(member->memberType())) +
                                     "    chunk " + std::to_string(member->id().value());
        GtkWidget* tile = usePreview
                              ? makeCastPreviewTile(ui, member, title, subtitle)
                              : makeInfoTile(title, subtitle, iconForMember(member));
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), tile);
        gtk_list_box_append(GTK_LIST_BOX(list), row);

        if (index == rememberedSelection) {
            gtk_list_box_select_row(GTK_LIST_BOX(list), GTK_LIST_BOX_ROW(row));
        }
    }
}

void populateCastBrowser(EditorUi& ui) {
    if (ui.selectedMemberIndex >= static_cast<int>(ui.castMembers.size())) {
        ui.selectedMemberIndex = -1;
    }
    if (ui.selectedGenericMemberIndex >= static_cast<int>(ui.castMembers.size()) ||
        (ui.selectedGenericMemberIndex >= 0 &&
         !isGenericDetailFilter(filterForMember(ui.castMembers[static_cast<std::size_t>(ui.selectedGenericMemberIndex)])))) {
        ui.selectedGenericMemberIndex = -1;
    }
    for (auto it = ui.selectedMemberByFilter.begin(); it != ui.selectedMemberByFilter.end();) {
        if (it->first < 0 || it->first > static_cast<int>(AssetFilter::Other)) {
            it = ui.selectedMemberByFilter.erase(it);
            continue;
        }
        const auto filter = static_cast<AssetFilter>(it->first);
        if (validMemberSelectionForFilter(ui, filter, it->second)) {
            ++it;
        } else {
            it = ui.selectedMemberByFilter.erase(it);
        }
    }
    ui.refreshingCastLists = true;
    for (GtkWidget* list : ui.castLists) {
        if (list != nullptr) {
            populateCastList(ui, list);
        }
    }
    ui.refreshingCastLists = false;
}

void updateCastViewDropDown(EditorUi& ui) {
    if (ui.castViewDropDown == nullptr) {
        return;
    }
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ui.castViewDropDown),
                               ui.castBrowserMode == CastBrowserMode::Preview ? 1U : 0U);
}

void castViewChanged(GObject*, GParamSpec*, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    if (ui.castViewDropDown == nullptr) {
        return;
    }

    const guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(ui.castViewDropDown));
    ui.castBrowserMode = selected == 1 ? CastBrowserMode::Preview : CastBrowserMode::List;
    populateCastBrowser(ui);
}

GtkWidget* makeCastPanelContent(EditorUi& ui) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    GtkWidget* controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    setMargins(controls, 6);
    GtkWidget* label = makeLabel("View", "asset-subtitle");
    gtk_box_append(GTK_BOX(controls), label);

    const char* modes[] = {"List", "Default Preview", nullptr};
    ui.castViewDropDown = gtk_drop_down_new_from_strings(modes);
    gtk_widget_set_hexpand(ui.castViewDropDown, TRUE);
    updateCastViewDropDown(ui);
    g_signal_connect(ui.castViewDropDown, "notify::selected", G_CALLBACK(castViewChanged), &ui);
    gtk_box_append(GTK_BOX(controls), ui.castViewDropDown);
    gtk_box_append(GTK_BOX(box), controls);

    ui.castList = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ui.castList), GTK_SELECTION_SINGLE);
    g_object_set_data(G_OBJECT(ui.castList), "cast-filter", GINT_TO_POINTER(static_cast<int>(AssetFilter::All) + 1));
    g_object_set_data(G_OBJECT(ui.castList), "force-preview", GINT_TO_POINTER(FALSE));
    g_signal_connect(ui.castList, "row-selected", G_CALLBACK(castRowSelected), &ui);
    ui.castLists.push_back(ui.castList);
    gtk_box_append(GTK_BOX(box), makeScrolled(ui.castList));
    return box;
}

GtkWidget* makeCastTypePanelContent(EditorUi& ui, AssetFilter filter) {
    GtkWidget* list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    g_object_set_data(G_OBJECT(list), "cast-filter", GINT_TO_POINTER(static_cast<int>(filter) + 1));
    g_object_set_data(G_OBJECT(list), "force-preview", GINT_TO_POINTER(TRUE));
    g_signal_connect(list, "row-selected", G_CALLBACK(castRowSelected), &ui);
    ui.castLists.push_back(list);
    return makeScrolled(list);
}

void populateChunkList(EditorUi& ui) {
    if (ui.chunkList == nullptr) {
        return;
    }

    clearListBox(ui.chunkList);
    if (ui.file == nullptr) {
        return;
    }

    for (const auto& [id, info] : ui.file->chunkInfo()) {
        GtkWidget* row = gtk_list_box_row_new();
        g_object_set_data(G_OBJECT(row), "chunk-id", GINT_TO_POINTER(id));
        const std::string title = "#" + std::to_string(id) + " " + std::string(libreshockwave::format::fourCCString(info.type()));
        const std::string subtitle = std::string(libreshockwave::format::description(info.type())) +
                                     std::string("    ") + std::to_string(info.length) + " bytes";
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), makeInfoTile(title, subtitle, "text-x-generic-symbolic"));
        gtk_list_box_append(GTK_LIST_BOX(ui.chunkList), row);
    }
}

void populateScoreList(EditorUi& ui) {
    if (ui.scoreList == nullptr) {
        return;
    }

    clearListBox(ui.scoreList);
    if (ui.file == nullptr || ui.file->scoreChunk() == nullptr) {
        gtk_list_box_append(GTK_LIST_BOX(ui.scoreList), makeKeyValueRow("Score", "No score data"));
        return;
    }

    const auto score = ui.file->scoreChunk();
    gtk_list_box_append(GTK_LIST_BOX(ui.scoreList), makeKeyValueRow("Frames", std::to_string(score->getFrameCount())));
    gtk_list_box_append(GTK_LIST_BOX(ui.scoreList), makeKeyValueRow("Channels", std::to_string(score->getChannelCount())));
    gtk_list_box_append(GTK_LIST_BOX(ui.scoreList), makeKeyValueRow("Sprite record", std::to_string(score->getSpriteRecordSize()) + " bytes"));
    gtk_list_box_append(GTK_LIST_BOX(ui.scoreList), makeKeyValueRow("Frame intervals", std::to_string(score->frameIntervals().size())));

    int shown = 0;
    for (const auto& interval : score->frameIntervals()) {
        if (shown >= 60) {
            gtk_list_box_append(GTK_LIST_BOX(ui.scoreList), makeKeyValueRow("More", "Only first 60 intervals shown"));
            break;
        }
        std::ostringstream key;
        key << "Sprite " << interval.primary.channelIndex;
        std::ostringstream value;
        value << "frames " << interval.primary.startFrame << "-" << interval.primary.endFrame;
        if (interval.secondary.has_value()) {
            value << ", cast " << interval.secondary->castLib << ", member " << interval.secondary->castMember;
        }
        gtk_list_box_append(GTK_LIST_BOX(ui.scoreList), makeKeyValueRow(key.str(), value.str()));
        ++shown;
    }
}

void populateLibrary(EditorUi& ui) {
    if (ui.libraryList == nullptr) {
        return;
    }

    clearListBox(ui.libraryList);
    for (const auto& spec : castPanelSpecs()) {
        GtkWidget* row = gtk_list_box_row_new();
        g_object_set_data(G_OBJECT(row), "filter", GINT_TO_POINTER(static_cast<int>(spec.filter) + 1));
        const std::string count = ui.file ? std::to_string(countMembersForFilter(ui, spec.filter)) : "0";
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row),
                                   makeInfoTile(spec.title, count, iconForFilter(spec.filter)));
        gtk_list_box_append(GTK_LIST_BOX(ui.libraryList), row);
    }
}

void libraryRowSelected(GtkListBox*, GtkListBoxRow* row, gpointer userData) {
    if (row == nullptr) {
        return;
    }
    auto& ui = *static_cast<EditorUi*>(userData);
    const int filterValue = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "filter")) - 1;
    ui.filter = static_cast<AssetFilter>(std::clamp(filterValue, 0, static_cast<int>(AssetFilter::Other)));
    if (auto found = ui.castBrowserPanels.find(static_cast<int>(ui.filter));
        found != ui.castBrowserPanels.end()) {
        raisePanel(found->second);
    }
    const int memberIndex = rememberedMemberForFilter(ui, ui.filter);
    if (memberIndex >= 0) {
        ui.selectedMemberIndex = memberIndex;
        populateInspectorForMember(ui, memberIndex);
    }
}

void castBrowserPanelPresented(PanelWidget* panel, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    if (ui.restoringPanelSelection) {
        return;
    }
    const int filterValue = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(panel), "cast-filter")) - 1;
    if (filterValue < 0 || filterValue > static_cast<int>(AssetFilter::Other)) {
        return;
    }

    const auto filter = static_cast<AssetFilter>(filterValue);
    const int memberIndex = rememberedMemberForFilter(ui, filter);
    if (memberIndex >= 0) {
        ui.filter = filter;
        ui.selectedMemberIndex = memberIndex;
        ui.restoringPanelSelection = true;
        populateInspectorForMember(ui, memberIndex);
        ui.restoringPanelSelection = false;
    }
}

void wireCastBrowserPanel(EditorUi& ui, PanelWidget* panel, AssetFilter filter) {
    g_object_set_data(G_OBJECT(panel), "cast-filter", GINT_TO_POINTER(static_cast<int>(filter) + 1));
    g_signal_connect(panel, "presented", G_CALLBACK(castBrowserPanelPresented), &ui);
}

void memberDetailPanelPresented(PanelWidget* panel, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    if (ui.restoringPanelSelection) {
        return;
    }
    const int filterValue = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(panel), "member-filter")) - 1;
    if (filterValue < 0 || filterValue > static_cast<int>(AssetFilter::Other)) {
        return;
    }

    const auto filter = static_cast<AssetFilter>(filterValue);
    const int memberIndex = panel == ui.genericPanel
                                ? rememberedGenericMember(ui)
                                : rememberedMemberForFilter(ui, filter);
    if (memberIndex >= 0) {
        ui.selectedMemberIndex = memberIndex;
        ui.restoringPanelSelection = true;
        populateInspectorForMember(ui, memberIndex);
        ui.restoringPanelSelection = false;
    }
}

void wireMemberDetailPanel(EditorUi& ui, PanelWidget* panel, AssetFilter filter) {
    g_object_set_data(G_OBJECT(panel), "member-filter", GINT_TO_POINTER(static_cast<int>(filter) + 1));
    g_signal_connect(panel, "presented", G_CALLBACK(memberDetailPanelPresented), &ui);
}

void shutdownProject(EditorUi& ui) {
    cancelPlaybackTimer(ui);
    ui.playing = false;
    if (ui.player != nullptr) {
        ui.player->shutdown();
    }
    ui.player.reset();
    ui.file.reset();
    ui.currentProjectPath.clear();
    ui.castMembers.clear();
    ui.castLocationsByChunkId.clear();
    ui.selectedMemberIndex = -1;
    ui.selectedGenericMemberIndex = -1;
    ui.selectedMemberByFilter.clear();
    ui.stagePixels.clear();
    ui.stageWidth = 0;
    ui.stageHeight = 0;
    setPreviewEmpty(ui);
    setPalettePreviewEmpty(ui);
}

void refreshWorkspace(EditorUi& ui) {
    buildCastLocationMap(ui);
    ui.castMembers = ui.file ? ui.file->castMembers() : std::vector<std::shared_ptr<libreshockwave::chunks::CastMemberChunk>>{};
    populateLibrary(ui);
    populateCastBrowser(ui);
    populateChunkList(ui);
    populateScoreList(ui);
    populateInspectorForMovie(ui);
    renderCurrentFrame(ui);
}

bool openProject(EditorUi& ui, const std::string& projectPath) {
    try {
        shutdownProject(ui);
        const std::string normalizedPath = pathString(absolutePath(projectPath));
        const auto bytes = readFile(normalizedPath);
        auto file = libreshockwave::DirectorFile::load(bytes);
        file->setBasePath(fs::path(normalizedPath).parent_path().string());

        auto player = std::make_unique<libreshockwave::player::Player>(file);
        player->setExternalParams(ui.externalParams);
        player->vm().setTickDeadlineMs(1000);
        player->setErrorListener([&ui](std::string_view message, std::string_view detail) {
            std::string status = "[script] " + std::string(message);
            if (!detail.empty()) {
                status += ": ";
                status += detail;
            }
            setStatus(ui, status);
        });

        ui.file = std::move(file);
        ui.player = std::move(player);
        ui.currentProjectPath = normalizedPath;
        rememberProject(ui, normalizedPath);

        if (ui.rootStack != nullptr) {
            gtk_stack_set_visible_child_name(GTK_STACK(ui.rootStack), "workspace");
        }

        ui.player->play();
        ui.player->pause();
        refreshWorkspace(ui);

        std::ostringstream status;
        status << "Loaded " << normalizedPath
               << "    Stage " << ui.file->stageWidth() << "x" << ui.file->stageHeight()
               << "    Cast members " << ui.file->castMembers().size()
               << "    Read-only";
        setStatus(ui, status.str());
        return true;
    } catch (const std::exception& error) {
        shutdownProject(ui);
        refreshWorkspace(ui);
        setStatus(ui, error.what());
        return false;
    }
}

void recentRowActivated(GtkListBox*, GtkListBoxRow* row, gpointer userData) {
    if (row == nullptr) {
        return;
    }
    auto& ui = *static_cast<EditorUi*>(userData);
    const char* path = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "project-path"));
    if (path != nullptr) {
        openProject(ui, path);
    }
}

void populateRecentList(EditorUi& ui) {
    if (ui.recentList == nullptr) {
        return;
    }

    clearListBox(ui.recentList);
    if (ui.recentProjects.empty()) {
        GtkWidget* row = gtk_list_box_row_new();
        gtk_widget_set_sensitive(row, FALSE);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row),
                                   makeInfoTile("No recent projects", "Open a .dcr, .dir, .dxr, .cct, or .cst file", "document-open-symbolic"));
        gtk_list_box_append(GTK_LIST_BOX(ui.recentList), row);
        return;
    }

    for (const auto& project : ui.recentProjects) {
        GtkWidget* row = gtk_list_box_row_new();
        g_object_set_data_full(G_OBJECT(row), "project-path", g_strdup(project.c_str()), g_free);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row),
                                   makeInfoTile(sourceFileName(project), project, "folder-open-symbolic"));
        gtk_list_box_append(GTK_LIST_BOX(ui.recentList), row);
    }
}

void updateExternalParamsButton(EditorUi& ui);
void setDockAreas(EditorUi& ui, bool start, bool end, bool bottom);

int objectIntProperty(GObject* object, const char* propertyName, int fallback) {
    if (object == nullptr) {
        return fallback;
    }
    int value = fallback;
    g_object_get(object, propertyName, &value, nullptr);
    return value;
}

void setDockSizes(EditorUi& ui, int startWidth, int endWidth, int bottomHeight) {
    if (ui.dock == nullptr) {
        return;
    }
    panel_dock_set_start_width(ui.dock, std::max(120, startWidth));
    panel_dock_set_end_width(ui.dock, std::max(160, endWidth));
    panel_dock_set_bottom_height(ui.dock, std::max(120, bottomHeight));
}

void saveWorkspaceFile(EditorUi& ui, const std::string& workspacePath) {
    fs::path targetPath = absolutePath(workspacePath);
    if (targetPath.extension().empty()) {
        targetPath.replace_extension(".libresw");
    }
    const std::string normalizedPath = pathString(targetPath);
    GKeyFile* keyFile = g_key_file_new();

    g_key_file_set_string(keyFile, "Workspace", "project_path", ui.currentProjectPath.c_str());
    g_key_file_set_string(keyFile, "Workspace", "cast_view", castBrowserModeText(ui.castBrowserMode).c_str());

    g_key_file_set_integer(keyFile, "CastSelection", "active_member_index", ui.selectedMemberIndex);
    g_key_file_set_integer(keyFile, "CastSelection", "generic_member_index", ui.selectedGenericMemberIndex);
    int selectionCount = 0;
    for (const auto& [filterValue, memberIndex] : ui.selectedMemberByFilter) {
        if (filterValue < 0 || filterValue > static_cast<int>(AssetFilter::Other)) {
            continue;
        }
        const auto filter = static_cast<AssetFilter>(filterValue);
        if (!validMemberSelectionForFilter(ui, filter, memberIndex)) {
            continue;
        }
        const auto filterKey = keyName("selection", selectionCount, "_filter");
        const auto memberKey = keyName("selection", selectionCount, "_member_index");
        g_key_file_set_string(keyFile, "CastSelection", filterKey.c_str(), castFilterStorageName(filter).c_str());
        g_key_file_set_integer(keyFile, "CastSelection", memberKey.c_str(), memberIndex);
        ++selectionCount;
    }
    g_key_file_set_integer(keyFile, "CastSelection", "count", selectionCount);

    const bool revealStart = ui.dock == nullptr || panel_dock_get_reveal_start(ui.dock);
    const bool revealEnd = ui.dock == nullptr || panel_dock_get_reveal_end(ui.dock);
    const bool revealBottom = ui.dock == nullptr || panel_dock_get_reveal_bottom(ui.dock);
    g_key_file_set_boolean(keyFile, "Dock", "reveal_start", revealStart);
    g_key_file_set_boolean(keyFile, "Dock", "reveal_end", revealEnd);
    g_key_file_set_boolean(keyFile, "Dock", "reveal_bottom", revealBottom);
    g_key_file_set_integer(keyFile, "Dock", "start_width", objectIntProperty(G_OBJECT(ui.dock), "start-width", 220));
    g_key_file_set_integer(keyFile, "Dock", "end_width", objectIntProperty(G_OBJECT(ui.dock), "end-width", 360));
    g_key_file_set_integer(keyFile, "Dock", "bottom_height", objectIntProperty(G_OBJECT(ui.dock), "bottom-height", 250));

    g_key_file_set_integer(keyFile, "ExternalParameters", "count", static_cast<int>(ui.externalParams.size()));
    for (std::size_t index = 0; index < ui.externalParams.size(); ++index) {
        const auto keyKey = keyName("param", static_cast<int>(index), "_key");
        const auto valueKey = keyName("param", static_cast<int>(index), "_value");
        g_key_file_set_string(keyFile, "ExternalParameters", keyKey.c_str(), ui.externalParams[index].first.c_str());
        g_key_file_set_string(keyFile, "ExternalParameters", valueKey.c_str(), ui.externalParams[index].second.c_str());
    }

    GError* error = nullptr;
    gsize length = 0;
    char* data = g_key_file_to_data(keyFile, &length, &error);
    if (error != nullptr) {
        std::string message = error->message;
        g_error_free(error);
        g_key_file_unref(keyFile);
        throw std::runtime_error(message);
    }

    writeTextFile(normalizedPath, std::string_view(data, length));
    g_free(data);
    g_key_file_unref(keyFile);

    ui.workspacePath = normalizedPath;
    setStatus(ui, "Workspace saved: " + sourceFileName(normalizedPath));
}

void loadWorkspaceFile(EditorUi& ui, const std::string& workspacePath) {
    const std::string normalizedPath = pathString(absolutePath(workspacePath));
    GKeyFile* keyFile = g_key_file_new();
    GError* error = nullptr;
    if (!g_key_file_load_from_file(keyFile, normalizedPath.c_str(), G_KEY_FILE_NONE, &error)) {
        std::string message = error != nullptr ? error->message : "Unable to load workspace";
        if (error != nullptr) {
            g_error_free(error);
        }
        g_key_file_unref(keyFile);
        throw std::runtime_error(message);
    }

    std::string projectPath = keyFileString(keyFile, "Workspace", "project_path").value_or("");
    if (!projectPath.empty() && !fs::path(projectPath).is_absolute()) {
        projectPath = pathString(fs::path(normalizedPath).parent_path() / projectPath);
    }

    ui.castBrowserMode = castBrowserModeFromText(
        keyFileString(keyFile, "Workspace", "cast_view").value_or("list"));

    const int loadedActiveSelection = keyFileInt(keyFile, "CastSelection", "active_member_index", -1);
    const int loadedGenericSelection = keyFileInt(keyFile, "CastSelection", "generic_member_index", -1);
    std::unordered_map<int, int> loadedSelections;
    const int selectionCount = keyFileInt(keyFile, "CastSelection", "count", 0);
    for (int index = 0; index < selectionCount; ++index) {
        const auto filterKey = keyName("selection", index, "_filter");
        const auto memberKey = keyName("selection", index, "_member_index");
        const auto filterName = keyFileString(keyFile, "CastSelection", filterKey.c_str()).value_or("");
        const auto filter = castFilterFromStorageName(filterName);
        if (!filter.has_value()) {
            continue;
        }
        loadedSelections[static_cast<int>(*filter)] = keyFileInt(keyFile, "CastSelection", memberKey.c_str(), -1);
    }

    const bool revealStart = keyFileBool(keyFile, "Dock", "reveal_start", true);
    const bool revealEnd = keyFileBool(keyFile, "Dock", "reveal_end", true);
    const bool revealBottom = keyFileBool(keyFile, "Dock", "reveal_bottom", true);
    const int startWidth = keyFileInt(keyFile, "Dock", "start_width", 220);
    const int endWidth = keyFileInt(keyFile, "Dock", "end_width", 360);
    const int bottomHeight = keyFileInt(keyFile, "Dock", "bottom_height", 250);

    std::vector<std::pair<std::string, std::string>> params;
    const int paramCount = keyFileInt(keyFile, "ExternalParameters", "count", 0);
    for (int index = 0; index < paramCount; ++index) {
        const auto keyKey = keyName("param", index, "_key");
        const auto valueKey = keyName("param", index, "_value");
        auto key = keyFileString(keyFile, "ExternalParameters", keyKey.c_str()).value_or("");
        auto value = keyFileString(keyFile, "ExternalParameters", valueKey.c_str()).value_or("");
        if (!trim(key).empty()) {
            setExternalParam(params, trim(key), value);
        }
    }
    g_key_file_unref(keyFile);

    ui.workspacePath = normalizedPath;
    ui.externalParams = std::move(params);
    updateExternalParamsButton(ui);
    updateCastViewDropDown(ui);
    setDockAreas(ui, revealStart, revealEnd, revealBottom);
    setDockSizes(ui, startWidth, endWidth, bottomHeight);

    bool projectLoaded = false;
    if (!projectPath.empty()) {
        projectLoaded = openProject(ui, projectPath);
    }

    if (projectLoaded) {
        ui.selectedMemberByFilter = std::move(loadedSelections);
        ui.selectedMemberIndex =
            loadedActiveSelection >= 0 && loadedActiveSelection < static_cast<int>(ui.castMembers.size())
                ? loadedActiveSelection
                : -1;
        ui.selectedGenericMemberIndex =
            loadedGenericSelection >= 0 &&
                    loadedGenericSelection < static_cast<int>(ui.castMembers.size()) &&
                    isGenericDetailFilter(filterForMember(ui.castMembers[static_cast<std::size_t>(loadedGenericSelection)]))
                ? loadedGenericSelection
                : -1;

        populateCastBrowser(ui);
        restoreRememberedMemberPanels(ui);
    } else {
        populateCastBrowser(ui);
    }
    if (ui.player != nullptr) {
        ui.player->setExternalParams(ui.externalParams);
    }
    setStatus(ui, "Workspace loaded: " + sourceFileName(normalizedPath));
}

void saveWorkspaceAsResponse(GtkNativeDialog* dialog, int response, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    bool saved = false;
    if (response == GTK_RESPONSE_ACCEPT) {
        GFile* file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        if (file != nullptr) {
            char* path = g_file_get_path(file);
            if (path != nullptr) {
                try {
                    saveWorkspaceFile(ui, path);
                    saved = true;
                } catch (const std::exception& error) {
                    setStatus(ui, error.what());
                }
                g_free(path);
            }
            g_object_unref(file);
        }
    }
    gtk_native_dialog_destroy(dialog);
    const bool shouldClose = ui.closeAfterWorkspaceSave && saved;
    ui.closeAfterWorkspaceSave = false;
    if (shouldClose && ui.window != nullptr) {
        gtk_window_close(GTK_WINDOW(ui.window));
    }
}

void showSaveWorkspaceDialog(EditorUi& ui, bool closeAfterSave = false) {
    ui.closeAfterWorkspaceSave = closeAfterSave;
    GtkFileChooserNative* dialog = gtk_file_chooser_native_new("Save Workspace",
                                                               GTK_WINDOW(ui.window),
                                                               GTK_FILE_CHOOSER_ACTION_SAVE,
                                                               "_Save",
                                                               "_Cancel");
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "LibreShockwave workspace");
    gtk_file_filter_add_pattern(filter, "*.libresw");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "workspace.libresw");
    g_signal_connect(dialog, "response", G_CALLBACK(saveWorkspaceAsResponse), &ui);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}

void saveWorkspaceClicked(GtkButton*, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    if (ui.workspacePath.empty()) {
        showSaveWorkspaceDialog(ui);
        return;
    }
    try {
        saveWorkspaceFile(ui, ui.workspacePath);
    } catch (const std::exception& error) {
        setStatus(ui, error.what());
    }
}

void saveWorkspaceAsClicked(GtkButton*, gpointer userData) {
    showSaveWorkspaceDialog(*static_cast<EditorUi*>(userData));
}

void exitClicked(GtkButton*, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    if (ui.window != nullptr) {
        gtk_window_close(GTK_WINDOW(ui.window));
    }
}

void minimizeClicked(GtkButton*, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    if (ui.window != nullptr) {
        gtk_window_minimize(GTK_WINDOW(ui.window));
    }
}

void maximizeClicked(GtkButton*, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    if (ui.window == nullptr) {
        return;
    }

    if (gtk_window_is_maximized(GTK_WINDOW(ui.window))) {
        gtk_window_unmaximize(GTK_WINDOW(ui.window));
    } else {
        gtk_window_maximize(GTK_WINDOW(ui.window));
    }
}

void saveWorkspaceAndExitClicked(GtkButton*, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    if (ui.workspacePath.empty()) {
        showSaveWorkspaceDialog(ui, true);
        return;
    }

    try {
        saveWorkspaceFile(ui, ui.workspacePath);
        if (ui.window != nullptr) {
            gtk_window_close(GTK_WINDOW(ui.window));
        }
    } catch (const std::exception& error) {
        setStatus(ui, error.what());
    }
}

void openWorkspaceResponse(GtkNativeDialog* dialog, int response, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    if (response == GTK_RESPONSE_ACCEPT) {
        GFile* file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        if (file != nullptr) {
            char* path = g_file_get_path(file);
            if (path != nullptr) {
                try {
                    loadWorkspaceFile(ui, path);
                    populateRecentList(ui);
                } catch (const std::exception& error) {
                    setStatus(ui, error.what());
                }
                g_free(path);
            }
            g_object_unref(file);
        }
    }
    gtk_native_dialog_destroy(dialog);
}

void showOpenWorkspaceDialog(EditorUi& ui) {
    GtkFileChooserNative* dialog = gtk_file_chooser_native_new("Open Workspace",
                                                               GTK_WINDOW(ui.window),
                                                               GTK_FILE_CHOOSER_ACTION_OPEN,
                                                               "_Open",
                                                               "_Cancel");
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "LibreShockwave workspace");
    gtk_file_filter_add_pattern(filter, "*.libresw");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    g_signal_connect(dialog, "response", G_CALLBACK(openWorkspaceResponse), &ui);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}

void openWorkspaceClicked(GtkButton*, gpointer userData) {
    showOpenWorkspaceDialog(*static_cast<EditorUi*>(userData));
}

void openProjectResponse(GtkNativeDialog* dialog, int response, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    if (response == GTK_RESPONSE_ACCEPT) {
        GFile* file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        if (file != nullptr) {
            char* path = g_file_get_path(file);
            if (path != nullptr) {
                openProject(ui, path);
                populateRecentList(ui);
                g_free(path);
            }
            g_object_unref(file);
        }
    }
    gtk_native_dialog_destroy(dialog);
}

void showOpenProjectDialog(EditorUi& ui) {
    GtkFileChooserNative* dialog = gtk_file_chooser_native_new("Open Project",
                                                               GTK_WINDOW(ui.window),
                                                               GTK_FILE_CHOOSER_ACTION_OPEN,
                                                               "_Open",
                                                               "_Cancel");
    GtkFileFilter* directorFilter = gtk_file_filter_new();
    gtk_file_filter_set_name(directorFilter, "Director and Shockwave files");
    gtk_file_filter_add_pattern(directorFilter, "*.dcr");
    gtk_file_filter_add_pattern(directorFilter, "*.dir");
    gtk_file_filter_add_pattern(directorFilter, "*.dxr");
    gtk_file_filter_add_pattern(directorFilter, "*.cct");
    gtk_file_filter_add_pattern(directorFilter, "*.cst");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), directorFilter);

    g_signal_connect(dialog, "response", G_CALLBACK(openProjectResponse), &ui);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}

void openClicked(GtkButton*, gpointer userData) {
    showOpenProjectDialog(*static_cast<EditorUi*>(userData));
}

void updateExternalParamsButton(EditorUi& ui) {
    if (ui.paramsButton == nullptr) {
        return;
    }
    const std::string tooltip = "External parameters (" + std::to_string(ui.externalParams.size()) + ")";
    gtk_widget_set_tooltip_text(ui.paramsButton, tooltip.c_str());
}

void setParamsDialogError(ParamsDialogData& data, std::string_view message) {
    if (data.errorLabel != nullptr) {
        gtk_label_set_text(GTK_LABEL(data.errorLabel), std::string(message).c_str());
    }
}

void removeParamRowClicked(GtkButton* button, gpointer userData) {
    auto& data = *static_cast<ParamsDialogData*>(userData);
    for (auto it = data.rows.begin(); it != data.rows.end(); ++it) {
        if (it->removeButton == GTK_WIDGET(button)) {
            gtk_box_remove(GTK_BOX(data.rowsBox), it->rowBox);
            data.rows.erase(it);
            return;
        }
    }
}

void appendParamRow(ParamsDialogData& data,
                    std::string_view key = {},
                    std::string_view value = {},
                    bool focusKey = false) {
    GtkWidget* rowBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_hexpand(rowBox, TRUE);

    GtkWidget* keyEntry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(keyEntry), "name");
    gtk_widget_set_size_request(keyEntry, 180, -1);
    gtk_editable_set_text(GTK_EDITABLE(keyEntry), std::string(key).c_str());
    gtk_box_append(GTK_BOX(rowBox), keyEntry);

    GtkWidget* valueEntry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(valueEntry), "value");
    gtk_widget_set_hexpand(valueEntry, TRUE);
    gtk_editable_set_text(GTK_EDITABLE(valueEntry), std::string(value).c_str());
    gtk_box_append(GTK_BOX(rowBox), valueEntry);

    GtkWidget* removeButton = iconButton("list-remove-symbolic", "Remove parameter");
    gtk_box_append(GTK_BOX(rowBox), removeButton);

    data.rows.push_back({rowBox, keyEntry, valueEntry, removeButton});
    gtk_box_append(GTK_BOX(data.rowsBox), rowBox);
    g_signal_connect(removeButton, "clicked", G_CALLBACK(removeParamRowClicked), &data);

    if (focusKey) {
        gtk_widget_grab_focus(keyEntry);
    }
}

std::vector<std::pair<std::string, std::string>> collectParamRows(const ParamsDialogData& data) {
    std::vector<std::pair<std::string, std::string>> params;
    int rowNumber = 0;
    for (const auto& row : data.rows) {
        ++rowNumber;
        const char* rawKey = gtk_editable_get_text(GTK_EDITABLE(row.keyEntry));
        const char* rawValue = gtk_editable_get_text(GTK_EDITABLE(row.valueEntry));
        std::string key = trim(rawKey == nullptr ? "" : rawKey);
        std::string value = rawValue == nullptr ? "" : rawValue;
        if (key.empty() && trim(value).empty()) {
            continue;
        }
        if (key.empty()) {
            throw std::runtime_error("Parameter row " + std::to_string(rowNumber) + " needs a name.");
        }
        if (key.find('=') != std::string::npos) {
            throw std::runtime_error("Parameter row " + std::to_string(rowNumber) + " has '=' in the name.");
        }
        setExternalParam(params, std::move(key), std::move(value));
    }
    return params;
}

void addParamClicked(GtkButton*, gpointer userData) {
    appendParamRow(*static_cast<ParamsDialogData*>(userData), {}, {}, true);
}

void paramsDialogResponse(GtkDialog* dialog, int response, gpointer userData) {
    auto* data = static_cast<ParamsDialogData*>(userData);
    if (response == GTK_RESPONSE_APPLY || response == GTK_RESPONSE_ACCEPT) {
        try {
            data->ui->externalParams = collectParamRows(*data);
            if (data->ui->player != nullptr) {
                data->ui->player->setExternalParams(data->ui->externalParams);
            }
            updateExternalParamsButton(*data->ui);
            setStatus(*data->ui, "External parameters updated");
        } catch (const std::exception& error) {
            setParamsDialogError(*data, error.what());
            return;
        }
    }

    delete data;
    gtk_window_destroy(GTK_WINDOW(dialog));
}

void paramsClicked(GtkButton*, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    GtkWidget* dialog = gtk_dialog_new_with_buttons("External Parameters",
                                                    GTK_WINDOW(ui.window),
                                                    GTK_DIALOG_MODAL,
                                                    "_Cancel",
                                                    GTK_RESPONSE_CANCEL,
                                                    "_Apply",
                                                    GTK_RESPONSE_APPLY,
                                                    nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 720, 380);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    setMargins(box, 12);
    gtk_box_append(GTK_BOX(content), box);

    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* keyHeader = makeLabel("Name", "asset-subtitle");
    gtk_widget_set_size_request(keyHeader, 180, -1);
    GtkWidget* valueHeader = makeLabel("Value", "asset-subtitle");
    gtk_widget_set_hexpand(valueHeader, TRUE);
    gtk_box_append(GTK_BOX(header), keyHeader);
    gtk_box_append(GTK_BOX(header), valueHeader);
    gtk_box_append(GTK_BOX(box), header);

    GtkWidget* scroller = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroller, TRUE);
    GtkWidget* rowsBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    setMargins(rowsBox, 2);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), rowsBox);
    gtk_box_append(GTK_BOX(box), scroller);

    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* addButton = gtk_button_new_with_label("Add Parameter");
    GtkWidget* errorLabel = makeLabel("", "error");
    gtk_widget_set_hexpand(errorLabel, TRUE);
    gtk_box_append(GTK_BOX(actions), addButton);
    gtk_box_append(GTK_BOX(actions), errorLabel);
    gtk_box_append(GTK_BOX(box), actions);

    auto* data = new ParamsDialogData{&ui, rowsBox, errorLabel};
    for (const auto& [key, value] : ui.externalParams) {
        appendParamRow(*data, key, value);
    }
    if (data->rows.empty()) {
        appendParamRow(*data);
    }

    g_signal_connect(addButton, "clicked", G_CALLBACK(addParamClicked), data);
    g_signal_connect(dialog, "response", G_CALLBACK(paramsDialogResponse), data);
    gtk_window_present(GTK_WINDOW(dialog));
}

void setDockAreaChecks(EditorUi& ui, bool start, bool end, bool bottom) {
    if (ui.revealStartCheck != nullptr) {
        gtk_check_button_set_active(GTK_CHECK_BUTTON(ui.revealStartCheck), start);
    }
    if (ui.revealEndCheck != nullptr) {
        gtk_check_button_set_active(GTK_CHECK_BUTTON(ui.revealEndCheck), end);
    }
    if (ui.revealBottomCheck != nullptr) {
        gtk_check_button_set_active(GTK_CHECK_BUTTON(ui.revealBottomCheck), bottom);
    }
}

void setDockAreas(EditorUi& ui, bool start, bool end, bool bottom) {
    if (ui.dock != nullptr) {
        panel_dock_set_reveal_start(ui.dock, start);
        panel_dock_set_reveal_end(ui.dock, end);
        panel_dock_set_reveal_bottom(ui.dock, bottom);
    }
    setDockAreaChecks(ui, start, end, bottom);
}

std::vector<std::pair<std::string, PanelWidget*>> corePanelEntries(EditorUi& ui) {
    return {
        {"Stage", ui.stagePanel},
        {"Tools", ui.toolsPanel},
        {"Score", ui.scorePanel},
        {"Library", ui.libraryPanel},
        {"Property Inspector", ui.inspectorPanel},
        {"Chunks", ui.chunksPanel},
    };
}

std::vector<std::pair<std::string, PanelWidget*>> detailPanelEntries(EditorUi& ui) {
    return {
        {"Bitmap Detail", ui.bitmapPanel},
        {"Text Detail", ui.textPanel},
        {"Script Detail", ui.scriptPanel},
        {"Sound Detail", ui.soundPanel},
        {"Palette Detail", ui.palettePanel},
        {"Shape Detail", ui.shapePanel},
        {"Film Loop Detail", ui.filmLoopPanel},
        {"Movie/Video Detail", ui.moviePanel},
        {"Shockwave 3D Detail", ui.threeDPanel},
        {"Generic Member Detail", ui.genericPanel},
    };
}

std::vector<std::pair<std::string, PanelWidget*>> castPanelEntries(EditorUi& ui) {
    std::vector<std::pair<std::string, PanelWidget*>> entries;
    for (const auto& spec : castPanelSpecs()) {
        if (auto found = ui.castBrowserPanels.find(static_cast<int>(spec.filter));
            found != ui.castBrowserPanels.end()) {
            entries.emplace_back(spec.title, found->second);
        }
    }
    return entries;
}

void setPanelVisible(PanelWidget* panel, bool visible) {
    if (panel == nullptr) {
        return;
    }
    gtk_widget_set_visible(GTK_WIDGET(panel), visible);
    if (visible) {
        panel_widget_raise(panel);
    }
}

void setPanelGroupVisible(const std::vector<std::pair<std::string, PanelWidget*>>& panels, bool visible) {
    for (const auto& [unused, panel] : panels) {
        (void)unused;
        setPanelVisible(panel, visible);
    }
}

void setAllPanelWidgetsVisible(EditorUi& ui, bool visible) {
    setPanelGroupVisible(corePanelEntries(ui), visible);
    setPanelGroupVisible(castPanelEntries(ui), visible);
    setPanelGroupVisible(detailPanelEntries(ui), visible);
}

void setPanelToggleChecksActive(EditorUi& ui, bool active) {
    ui.syncingPanelToggleChecks = true;
    for (GtkWidget* check : ui.panelToggleChecks) {
        if (check != nullptr) {
            gtk_check_button_set_active(GTK_CHECK_BUTTON(check), active);
        }
    }
    ui.syncingPanelToggleChecks = false;
}

void showAllPanelsClicked(GtkButton*, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    setAllPanelWidgetsVisible(ui, true);
    setPanelToggleChecksActive(ui, true);
    setDockAreas(ui, true, true, true);
}

void hideAllPanelsClicked(GtkButton*, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    setAllPanelWidgetsVisible(ui, false);
    setPanelToggleChecksActive(ui, false);
    setDockAreas(ui, false, false, false);
}

void panelToggleToggled(GtkCheckButton* check, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    if (ui.syncingPanelToggleChecks) {
        return;
    }
    auto* panel = static_cast<PanelWidget*>(g_object_get_data(G_OBJECT(check), "panel-widget"));
    const bool visible = gtk_check_button_get_active(check);
    setPanelVisible(panel, visible);
    if (visible) {
        setDockAreas(ui, true, true, true);
    }
}

GtkWidget* makePanelToggle(EditorUi& ui, const char* label, PanelWidget* panel) {
    GtkWidget* check = gtk_check_button_new_with_label(label);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(check), panel != nullptr && gtk_widget_get_visible(GTK_WIDGET(panel)));
    gtk_widget_set_sensitive(check, panel != nullptr);
    g_object_set_data(G_OBJECT(check), "panel-widget", panel);
    g_signal_connect(check, "toggled", G_CALLBACK(panelToggleToggled), &ui);
    ui.panelToggleChecks.push_back(check);
    return check;
}

void appendWindowPanelSection(GtkWidget* box,
                              EditorUi& ui,
                              const char* title,
                              const std::vector<std::pair<std::string, PanelWidget*>>& panels) {
    GtkWidget* label = makeLabel(title, "asset-subtitle");
    setMargins(label, 4);
    gtk_box_append(GTK_BOX(box), label);
    for (const auto& [name, panel] : panels) {
        gtk_box_append(GTK_BOX(box), makePanelToggle(ui, name.c_str(), panel));
    }
}

void revealStartToggled(GtkCheckButton* check, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    if (ui.dock != nullptr) {
        panel_dock_set_reveal_start(ui.dock, gtk_check_button_get_active(check));
    }
}

void revealEndToggled(GtkCheckButton* check, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    if (ui.dock != nullptr) {
        panel_dock_set_reveal_end(ui.dock, gtk_check_button_get_active(check));
    }
}

void revealBottomToggled(GtkCheckButton* check, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    if (ui.dock != nullptr) {
        panel_dock_set_reveal_bottom(ui.dock, gtk_check_button_get_active(check));
    }
}

GtkWidget* buildWindowMenuButton(EditorUi& ui) {
    ui.panelToggleChecks.clear();
    GtkWidget* menuButton = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(menuButton), "Window");
    gtk_widget_add_css_class(menuButton, "flat");
    gtk_widget_add_css_class(menuButton, "menu-strip-button");

    GtkWidget* popover = gtk_popover_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    setMargins(box, 8);

    GtkWidget* showAll = gtk_button_new_with_label("Show All Panels");
    gtk_widget_add_css_class(showAll, "flat");
    g_signal_connect(showAll, "clicked", G_CALLBACK(showAllPanelsClicked), &ui);
    GtkWidget* hideAll = gtk_button_new_with_label("Hide All Panels");
    gtk_widget_add_css_class(hideAll, "flat");
    g_signal_connect(hideAll, "clicked", G_CALLBACK(hideAllPanelsClicked), &ui);

    ui.revealStartCheck = gtk_check_button_new_with_label("Tools and Score");
    ui.revealEndCheck = gtk_check_button_new_with_label("Library, Inspector, Details");
    ui.revealBottomCheck = gtk_check_button_new_with_label("Cast and Chunks");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ui.revealStartCheck), TRUE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ui.revealEndCheck), TRUE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ui.revealBottomCheck), TRUE);
    g_signal_connect(ui.revealStartCheck, "toggled", G_CALLBACK(revealStartToggled), &ui);
    g_signal_connect(ui.revealEndCheck, "toggled", G_CALLBACK(revealEndToggled), &ui);
    g_signal_connect(ui.revealBottomCheck, "toggled", G_CALLBACK(revealBottomToggled), &ui);

    gtk_box_append(GTK_BOX(box), showAll);
    gtk_box_append(GTK_BOX(box), hideAll);
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box), ui.revealStartCheck);
    gtk_box_append(GTK_BOX(box), ui.revealEndCheck);
    gtk_box_append(GTK_BOX(box), ui.revealBottomCheck);
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    appendWindowPanelSection(box, ui, "Core Panels", corePanelEntries(ui));
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    appendWindowPanelSection(box, ui, "Cast Member Panels", castPanelEntries(ui));
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    appendWindowPanelSection(box, ui, "Preview/Detail Panels", detailPanelEntries(ui));

    GtkWidget* scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroller), 260);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroller), 620);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), box);
    gtk_popover_set_child(GTK_POPOVER(popover), scroller);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(menuButton), popover);
    return menuButton;
}

GtkWidget* makeMenuActionButton(const char* label, GCallback callback, EditorUi& ui) {
    GtkWidget* button = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(button, "flat");
    gtk_widget_set_halign(button, GTK_ALIGN_FILL);
    g_signal_connect(button, "clicked", callback, &ui);
    return button;
}

GtkWidget* buildFileMenuButton(EditorUi& ui) {
    GtkWidget* menuButton = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(menuButton), "File");
    gtk_widget_add_css_class(menuButton, "flat");
    gtk_widget_add_css_class(menuButton, "menu-strip-button");

    GtkWidget* popover = gtk_popover_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    setMargins(box, 8);

    gtk_box_append(GTK_BOX(box), makeMenuActionButton("Open Project...", G_CALLBACK(openClicked), ui));
    gtk_box_append(GTK_BOX(box), makeMenuActionButton("Open Workspace...", G_CALLBACK(openWorkspaceClicked), ui));
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box), makeMenuActionButton("Save Workspace", G_CALLBACK(saveWorkspaceClicked), ui));
    gtk_box_append(GTK_BOX(box), makeMenuActionButton("Save Workspace As...", G_CALLBACK(saveWorkspaceAsClicked), ui));
    gtk_box_append(GTK_BOX(box), makeMenuActionButton("Save Workspace and Exit", G_CALLBACK(saveWorkspaceAndExitClicked), ui));
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box), makeMenuActionButton("External Parameters...", G_CALLBACK(paramsClicked), ui));
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box), makeMenuActionButton("Exit", G_CALLBACK(exitClicked), ui));

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(menuButton), popover);
    return menuButton;
}

GtkWidget* buildModifyMenuButton(EditorUi& ui) {
    GtkWidget* menuButton = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(menuButton), "Modify");
    gtk_widget_add_css_class(menuButton, "flat");
    gtk_widget_add_css_class(menuButton, "menu-strip-button");

    GtkWidget* popover = gtk_popover_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    setMargins(box, 8);

    gtk_box_append(GTK_BOX(box), makeMenuActionButton("Export Selected Cast Member...", G_CALLBACK(exportSelectedClicked), ui));
    gtk_box_append(GTK_BOX(box), makeMenuActionButton("Export All Cast Members...", G_CALLBACK(exportAllClicked), ui));

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(menuButton), popover);
    return menuButton;
}

GtkWidget* buildWelcomePage(EditorUi& ui) {
    GtkWidget* outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(outer, "welcome-page");
    gtk_widget_set_hexpand(outer, TRUE);
    gtk_widget_set_vexpand(outer, TRUE);

    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(content, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(content, 640, -1);
    setMargins(content, 28);

    GtkWidget* heading = makeLabel("LibreShockwave Director Studio", "welcome-title");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.5f);
    GtkWidget* subheading = makeLabel("Open a Director or Shockwave file to inspect its assets and stage.", "welcome-subtitle");
    gtk_label_set_xalign(GTK_LABEL(subheading), 0.5f);

    GtkWidget* openButton = gtk_button_new_with_label("Open Project...");
    gtk_button_set_icon_name(GTK_BUTTON(openButton), "document-open-symbolic");
    gtk_widget_add_css_class(openButton, "suggested-action");
    gtk_widget_set_halign(openButton, GTK_ALIGN_CENTER);
    g_signal_connect(openButton, "clicked", G_CALLBACK(openClicked), &ui);

    GtkWidget* recentTitle = makeLabel("Recent Projects", "section-title");
    ui.recentList = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ui.recentList), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(ui.recentList, "boxed-list");
    g_signal_connect(ui.recentList, "row-activated", G_CALLBACK(recentRowActivated), &ui);

    gtk_box_append(GTK_BOX(content), heading);
    gtk_box_append(GTK_BOX(content), subheading);
    gtk_box_append(GTK_BOX(content), openButton);
    gtk_box_append(GTK_BOX(content), recentTitle);
    gtk_box_append(GTK_BOX(content), ui.recentList);
    gtk_box_append(GTK_BOX(outer), content);

    populateRecentList(ui);
    return outer;
}

GtkWidget* buildMenuStrip(EditorUi& ui) {
    GtkWidget* strip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(strip, "menu-strip");
    const std::vector<const char*> labels{"File", "Edit", "View", "Insert", "Modify", "Control", "Xtras", "Window", "Help"};
    for (const char* label : labels) {
        if (std::string_view(label) == "File") {
            gtk_box_append(GTK_BOX(strip), buildFileMenuButton(ui));
            continue;
        }
        if (std::string_view(label) == "Modify") {
            gtk_box_append(GTK_BOX(strip), buildModifyMenuButton(ui));
            continue;
        }
        if (std::string_view(label) == "Window") {
            gtk_box_append(GTK_BOX(strip), buildWindowMenuButton(ui));
            continue;
        }
        GtkWidget* button = gtk_button_new_with_label(label);
        gtk_widget_add_css_class(button, "flat");
        gtk_widget_add_css_class(button, "menu-strip-button");
        gtk_widget_set_sensitive(button, std::string_view(label) == "File" || std::string_view(label) == "Control");
        gtk_box_append(GTK_BOX(strip), button);
    }
    return strip;
}

GtkWidget* buildToolbar(EditorUi& ui) {
    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(toolbar, "studio-toolbar");

    GtkWidget* openButton = iconButton("document-open-symbolic", "Open Project");
    g_signal_connect(openButton, "clicked", G_CALLBACK(openClicked), &ui);

    GtkWidget* saveButton = iconButton("document-save-symbolic", "Save disabled in read-only mode");
    gtk_widget_set_sensitive(saveButton, FALSE);
    GtkWidget* importButton = iconButton("document-import-symbolic", "Import disabled in read-only mode");
    gtk_widget_set_sensitive(importButton, FALSE);
    GtkWidget* publishButton = iconButton("document-send-symbolic", "Publish disabled in read-only mode");
    gtk_widget_set_sensitive(publishButton, FALSE);

    ui.playButton = iconButton("media-playback-start-symbolic", "Play");
    ui.stopButton = iconButton("media-playback-stop-symbolic", "Stop");
    ui.paramsButton = iconButton("preferences-system-symbolic", "External parameters");
    g_signal_connect(ui.playButton, "clicked", G_CALLBACK(playClicked), &ui);
    g_signal_connect(ui.stopButton, "clicked", G_CALLBACK(stopClicked), &ui);
    g_signal_connect(ui.paramsButton, "clicked", G_CALLBACK(paramsClicked), &ui);
    updateExternalParamsButton(ui);

    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);

    ui.titleLabel = makeLabel("Director Studio", "toolbar-title");
    ui.subtitleLabel = makeLabel("Read-only asset browser", "toolbar-subtitle");
    GtkWidget* titleBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(titleBox), ui.titleLabel);
    gtk_box_append(GTK_BOX(titleBox), ui.subtitleLabel);

    gtk_box_append(GTK_BOX(toolbar), openButton);
    gtk_box_append(GTK_BOX(toolbar), saveButton);
    gtk_box_append(GTK_BOX(toolbar), importButton);
    gtk_box_append(GTK_BOX(toolbar), publishButton);
    gtk_box_append(GTK_BOX(toolbar), gtk_separator_new(GTK_ORIENTATION_VERTICAL));
    gtk_box_append(GTK_BOX(toolbar), ui.playButton);
    gtk_box_append(GTK_BOX(toolbar), ui.stopButton);
    gtk_box_append(GTK_BOX(toolbar), ui.paramsButton);
    gtk_box_append(GTK_BOX(toolbar), spacer);
    gtk_box_append(GTK_BOX(toolbar), titleBox);
    return toolbar;
}

GtkWidget* buildTitleBar(EditorUi& ui) {
    GtkWidget* titleBar = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(titleBar), FALSE);

    GtkWidget* titleBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    ui.chromeTitleLabel = makeLabel("LibreShockwave Director Studio", "window-title");
    ui.chromeSubtitleLabel = makeLabel("Open a Director or Shockwave project", "window-subtitle");
    gtk_box_append(GTK_BOX(titleBox), ui.chromeTitleLabel);
    gtk_box_append(GTK_BOX(titleBox), ui.chromeSubtitleLabel);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(titleBar), titleBox);

    GtkWidget* controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(controls, "window-controls");

    GtkWidget* minimizeButton = iconButton("window-minimize-symbolic", "Minimize");
    GtkWidget* maximizeButton = iconButton("window-maximize-symbolic", "Maximize");
    GtkWidget* closeButton = iconButton("window-close-symbolic", "Close");
    gtk_widget_add_css_class(closeButton, "close-window-button");

    g_signal_connect(minimizeButton, "clicked", G_CALLBACK(minimizeClicked), &ui);
    g_signal_connect(maximizeButton, "clicked", G_CALLBACK(maximizeClicked), &ui);
    g_signal_connect(closeButton, "clicked", G_CALLBACK(exitClicked), &ui);

    gtk_box_append(GTK_BOX(controls), minimizeButton);
    gtk_box_append(GTK_BOX(controls), maximizeButton);
    gtk_box_append(GTK_BOX(controls), closeButton);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(titleBar), controls);

    return titleBar;
}

void dockDragBegin(PanelDock*, PanelWidget* widget, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    const char* title = widget == nullptr ? nullptr : panel_widget_get_title(widget);
    setStatus(ui, std::string("Docking ") + (title == nullptr ? "panel" : title));
}

void dockDragEnd(PanelDock*, PanelWidget*, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    if (!ui.workspacePath.empty()) {
        try {
            saveWorkspaceFile(ui, ui.workspacePath);
            return;
        } catch (const std::exception& error) {
            setStatus(ui, error.what());
            return;
        }
    }
    setStatus(ui, "Dock layout changed");
}

GtkWidget* buildDock(EditorUi& ui) {
    const char* dockUi = R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<interface>
  <requires lib="gtk" version="4.0"/>
  <requires lib="panel" version="1.0"/>
  <object class="PanelDock" id="dock">
    <property name="vexpand">true</property>
    <property name="hexpand">true</property>
    <property name="reveal-start">true</property>
    <property name="reveal-end">true</property>
    <property name="reveal-bottom">true</property>
    <property name="start-width">220</property>
    <property name="end-width">360</property>
    <property name="bottom-height">250</property>
    <child>
      <object class="PanelGrid" id="center_grid"/>
    </child>
    <child type="start">
      <object class="PanelPaned" id="start_paned">
        <property name="orientation">vertical</property>
      </object>
    </child>
    <child type="end">
      <object class="PanelPaned" id="end_paned">
        <property name="orientation">vertical</property>
      </object>
    </child>
    <child type="bottom">
      <object class="PanelPaned" id="bottom_paned">
        <property name="orientation">horizontal</property>
      </object>
    </child>
  </object>
</interface>
)XML";

    GError* error = nullptr;
    GtkBuilder* builder = gtk_builder_new();
    gtk_builder_add_from_string(builder, dockUi, -1, &error);
    if (error != nullptr) {
        std::string message = error->message;
        g_error_free(error);
        g_object_unref(builder);
        throw std::runtime_error("Unable to create dock UI: " + message);
    }

    ui.dock = PANEL_DOCK(gtk_builder_get_object(builder, "dock"));
    ui.centerGrid = PANEL_GRID(gtk_builder_get_object(builder, "center_grid"));
    auto* startPaned = PANEL_PANED(gtk_builder_get_object(builder, "start_paned"));
    auto* endPaned = PANEL_PANED(gtk_builder_get_object(builder, "end_paned"));
    auto* bottomPaned = PANEL_PANED(gtk_builder_get_object(builder, "bottom_paned"));

    g_object_ref(ui.dock);
    g_signal_connect(ui.dock, "panel-drag-begin", G_CALLBACK(dockDragBegin), &ui);
    g_signal_connect(ui.dock, "panel-drag-end", G_CALLBACK(dockDragEnd), &ui);
    g_signal_connect(ui.dock, "create-frame", G_CALLBACK(createDockFrame), &ui);
    g_signal_connect(ui.dock, "adopt-widget", G_CALLBACK(adoptDockWidget), &ui);
    g_signal_connect(ui.centerGrid, "create-frame", G_CALLBACK(createGridFrame), &ui);

    GtkWidget* toolsFrame = panel_frame_new();
    GtkWidget* scoreFrame = panel_frame_new();
    GtkWidget* endFrame = panel_frame_new();
    GtkWidget* bottomFrame = panel_frame_new();
    setFrameHeader(toolsFrame);
    setFrameHeader(scoreFrame);
    setFrameHeader(endFrame);
    setFrameHeader(bottomFrame);
    allowFrameDocking(PANEL_FRAME(toolsFrame), ui);
    allowFrameDocking(PANEL_FRAME(scoreFrame), ui);
    allowFrameDocking(PANEL_FRAME(endFrame), ui);
    allowFrameDocking(PANEL_FRAME(bottomFrame), ui);

    ui.stageArea = gtk_drawing_area_new();
    gtk_widget_add_css_class(ui.stageArea, "stage-view");
    gtk_widget_set_hexpand(ui.stageArea, TRUE);
    gtk_widget_set_vexpand(ui.stageArea, TRUE);
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(ui.stageArea), 640);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(ui.stageArea), 480);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(ui.stageArea), drawStage, &ui, nullptr);

    GtkWidget* stageBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(stageBox), ui.stageArea);
    ui.frameLabel = makeLabel("No movie", "frame-label");
    setMargins(ui.frameLabel, 6);
    gtk_box_append(GTK_BOX(stageBox), ui.frameLabel);
    ui.stagePanel = makePanelWidget("stage", "Stage", "video-display-symbolic", stageBox);
    panel_grid_add(ui.centerGrid, ui.stagePanel);
    panel_widget_raise(ui.stagePanel);

    ui.toolsPanel = makePanelWidget("tools", "Tools", "applications-graphics-symbolic", makeToolsPanel());
    panel_frame_add(PANEL_FRAME(toolsFrame), ui.toolsPanel);

    ui.scoreList = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ui.scoreList), GTK_SELECTION_NONE);
    ui.scorePanel = makePanelWidget("score", "Score", "media-playlist-repeat-symbolic", makeScrolled(ui.scoreList));
    panel_frame_add(PANEL_FRAME(scoreFrame), ui.scorePanel);

    ui.libraryList = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ui.libraryList), GTK_SELECTION_SINGLE);
    g_signal_connect(ui.libraryList, "row-selected", G_CALLBACK(libraryRowSelected), &ui);
    ui.libraryPanel = makePanelWidget("library", "Library", "folder-symbolic", makeScrolled(ui.libraryList));
    panel_frame_add(PANEL_FRAME(endFrame), ui.libraryPanel);

    ui.inspectorList = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ui.inspectorList), GTK_SELECTION_NONE);
    ui.inspectorPanel = makePanelWidget("inspector", "Property Inspector", "document-properties-symbolic", makeScrolled(ui.inspectorList));
    panel_frame_add(PANEL_FRAME(endFrame), ui.inspectorPanel);

    ui.bitmapPanel = makePanelWidget("bitmap", "Bitmap", "image-x-generic-symbolic", makeBitmapDetailContent(ui));
    wireMemberDetailPanel(ui, ui.bitmapPanel, AssetFilter::Bitmap);
    panel_frame_add(PANEL_FRAME(endFrame), ui.bitmapPanel);
    ui.textPanel = makePanelWidget("text", "Text", "text-x-generic-symbolic", makeDetailList(ui.textDetailList));
    wireMemberDetailPanel(ui, ui.textPanel, AssetFilter::Text);
    panel_frame_add(PANEL_FRAME(endFrame), ui.textPanel);
    ui.scriptPanel = makePanelWidget("script", "Script", "application-x-executable-symbolic", makeScriptDetailContent(ui));
    wireMemberDetailPanel(ui, ui.scriptPanel, AssetFilter::Script);
    panel_frame_add(PANEL_FRAME(endFrame), ui.scriptPanel);
    ui.soundPanel = makePanelWidget("sound", "Sound", "audio-x-generic-symbolic", makeDetailList(ui.soundDetailList));
    wireMemberDetailPanel(ui, ui.soundPanel, AssetFilter::Sound);
    panel_frame_add(PANEL_FRAME(endFrame), ui.soundPanel);
    ui.palettePanel = makePanelWidget("palette", "Palette", "color-select-symbolic", makePaletteDetailContent(ui));
    wireMemberDetailPanel(ui, ui.palettePanel, AssetFilter::Palette);
    panel_frame_add(PANEL_FRAME(endFrame), ui.palettePanel);
    ui.shapePanel = makePanelWidget("shape", "Shape", "insert-object-symbolic", makeDetailList(ui.shapeDetailList));
    wireMemberDetailPanel(ui, ui.shapePanel, AssetFilter::Shape);
    panel_frame_add(PANEL_FRAME(endFrame), ui.shapePanel);
    ui.filmLoopPanel = makePanelWidget("film-loop", "Film Loop", "media-playlist-repeat-symbolic", makeDetailList(ui.filmLoopDetailList));
    wireMemberDetailPanel(ui, ui.filmLoopPanel, AssetFilter::FilmLoop);
    panel_frame_add(PANEL_FRAME(endFrame), ui.filmLoopPanel);
    ui.moviePanel = makePanelWidget("movie", "Movie/Video", "video-x-generic-symbolic", makeDetailList(ui.movieDetailList));
    wireMemberDetailPanel(ui, ui.moviePanel, AssetFilter::Movie);
    panel_frame_add(PANEL_FRAME(endFrame), ui.moviePanel);
    ui.threeDPanel = makePanelWidget("shockwave-3d", "Shockwave 3D", "applications-graphics-symbolic", makeDetailList(ui.threeDDetailList));
    wireMemberDetailPanel(ui, ui.threeDPanel, AssetFilter::ThreeD);
    panel_frame_add(PANEL_FRAME(endFrame), ui.threeDPanel);
    ui.genericPanel = makePanelWidget("member", "Member", "text-x-generic-symbolic", makeDetailList(ui.genericDetailList));
    wireMemberDetailPanel(ui, ui.genericPanel, AssetFilter::Other);
    panel_frame_add(PANEL_FRAME(endFrame), ui.genericPanel);

    ui.castLists.clear();
    ui.castBrowserPanels.clear();
    PanelWidget* allCastPanel = makePanelWidget("cast", "Cast: All", iconForFilter(AssetFilter::All), makeCastPanelContent(ui));
    wireCastBrowserPanel(ui, allCastPanel, AssetFilter::All);
    ui.castBrowserPanels.emplace(static_cast<int>(AssetFilter::All), allCastPanel);
    panel_frame_add(PANEL_FRAME(bottomFrame), allCastPanel);

    for (const auto& spec : castPanelSpecs()) {
        if (spec.filter == AssetFilter::All) {
            continue;
        }
        PanelWidget* castPanel = makePanelWidget(spec.id,
                                                 spec.title,
                                                 iconForFilter(spec.filter),
                                                 makeCastTypePanelContent(ui, spec.filter));
        wireCastBrowserPanel(ui, castPanel, spec.filter);
        ui.castBrowserPanels.emplace(static_cast<int>(spec.filter), castPanel);
        panel_frame_add(PANEL_FRAME(bottomFrame), castPanel);
    }

    ui.chunkList = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ui.chunkList), GTK_SELECTION_SINGLE);
    g_signal_connect(ui.chunkList, "row-selected", G_CALLBACK(chunkRowSelected), &ui);
    ui.chunksPanel = makePanelWidget("chunks", "Chunks", "text-x-generic-symbolic", makeScrolled(ui.chunkList));
    panel_frame_add(PANEL_FRAME(bottomFrame), ui.chunksPanel);

    panel_paned_append(startPaned, toolsFrame);
    panel_paned_append(startPaned, scoreFrame);
    panel_paned_append(endPaned, endFrame);
    panel_paned_append(bottomPaned, bottomFrame);

    GtkWidget* dock = GTK_WIDGET(ui.dock);
    g_object_unref(builder);
    return dock;
}

GtkWidget* buildWorkspacePage(EditorUi& ui) {
    GtkWidget* workspace = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(workspace, "workspace-page");
    GtkWidget* dock = buildDock(ui);
    gtk_box_append(GTK_BOX(workspace), dock);
    g_object_unref(dock);
    return workspace;
}

GtkWidget* buildAppShell(EditorUi& ui) {
    GtkWidget* shell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(shell, "app-shell");
    gtk_widget_set_hexpand(shell, TRUE);
    gtk_widget_set_vexpand(shell, TRUE);

    ui.rootStack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(ui.rootStack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_add_named(GTK_STACK(ui.rootStack), buildWelcomePage(ui), "welcome");
    gtk_stack_add_named(GTK_STACK(ui.rootStack), buildWorkspacePage(ui), "workspace");
    gtk_widget_set_hexpand(ui.rootStack, TRUE);
    gtk_widget_set_vexpand(ui.rootStack, TRUE);

    gtk_box_append(GTK_BOX(shell), buildMenuStrip(ui));
    gtk_box_append(GTK_BOX(shell), buildToolbar(ui));
    gtk_box_append(GTK_BOX(shell), ui.rootStack);

    GtkWidget* status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(status, "status-strip");
    ui.statusLabel = makeLabel("Ready", "status-label");
    gtk_box_append(GTK_BOX(status), ui.statusLabel);
    gtk_box_append(GTK_BOX(shell), status);
    return shell;
}

gboolean windowCloseRequested(GtkWindow*, gpointer userData);
void holdApplicationForWindow(EditorUi& ui);

void activate(GtkApplication* app, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    ui.app = app;
    adw_style_manager_set_color_scheme(adw_style_manager_get_default(), ADW_COLOR_SCHEME_FORCE_DARK);
    loadEditorCss();

    ui.window = gtk_application_window_new(app);
    holdApplicationForWindow(ui);
    g_signal_connect(ui.window, "close-request", G_CALLBACK(windowCloseRequested), &ui);
    gtk_window_set_default_size(GTK_WINDOW(ui.window), 1440, 900);
    gtk_window_set_title(GTK_WINDOW(ui.window), "LibreShockwave Director Studio");
    gtk_window_set_resizable(GTK_WINDOW(ui.window), TRUE);

    gtk_window_set_titlebar(GTK_WINDOW(ui.window), buildTitleBar(ui));
    gtk_window_set_child(GTK_WINDOW(ui.window), buildAppShell(ui));

    refreshWorkspace(ui);
    updateWindowLabels(ui);
    updatePlaybackControls(ui);

    gtk_window_present(GTK_WINDOW(ui.window));
    if (!ui.initialProjectPath.empty()) {
        try {
            if (hasWorkspaceExtension(ui.initialProjectPath)) {
                loadWorkspaceFile(ui, ui.initialProjectPath);
            } else {
                openProject(ui, ui.initialProjectPath);
            }
            populateRecentList(ui);
        } catch (const std::exception& error) {
            setStatus(ui, error.what());
        }
    }
}

gboolean windowCloseRequested(GtkWindow*, gpointer userData) {
    auto& ui = *static_cast<EditorUi*>(userData);
    if (ui.applicationHeld && ui.app != nullptr) {
        g_application_release(G_APPLICATION(ui.app));
        ui.applicationHeld = false;
    }
    return FALSE;
}

void holdApplicationForWindow(EditorUi& ui) {
    if (!ui.applicationHeld && ui.app != nullptr) {
        g_application_hold(G_APPLICATION(ui.app));
        ui.applicationHeld = true;
    }
}

std::string usage(const char* argv0) {
    std::ostringstream out;
    out << "Usage: " << argv0 << " [project.dcr|project.dir|cast.cct|workspace.libresw]\n"
        << "Read-only GTK/libpanel browser for Director and Shockwave assets.";
    return out.str();
}

} // namespace

int runDirectorStudio(int argc, char** argv) {
    EditorUi ui;
    ui.settingsPath = defaultSettingsPath();
    ui.recentProjects = loadRecentProjects(ui.settingsPath);

    for (int index = 1; index < argc; ++index) {
        const std::string_view arg(argv[index]);
        if (arg == "--help" || arg == "-h") {
            std::cout << usage(argv[0]) << '\n';
            return 0;
        }
        if (arg.starts_with("-")) {
            std::cerr << "Unknown option: " << arg << "\n\n" << usage(argv[0]) << '\n';
            return 2;
        }
        if (ui.initialProjectPath.empty()) {
            ui.initialProjectPath = std::string(arg);
        }
    }

    adw_init();
    panel_init();
    AdwApplication* app = adw_application_new("net.libreshockwave.DirectorStudio", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &ui);
    ui.app = GTK_APPLICATION(app);
    g_application_hold(G_APPLICATION(app));
    ui.applicationHeld = true;
    char* appArgv[] = {argv[0], nullptr};
    const int status = g_application_run(G_APPLICATION(app), 1, appArgv);
    g_object_unref(app);
    cancelPlaybackTimer(ui);
    if (ui.player != nullptr) {
        ui.player->shutdown();
    }
    ui.player.reset();
    ui.file.reset();
    panel_finalize();
    return status;
}
