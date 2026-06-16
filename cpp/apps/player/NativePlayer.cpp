#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <curl/curl.h>

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/player/Player.hpp"
#include "libreshockwave/player/input/DirectorKeyCodes.hpp"
#include "libreshockwave/player/net/QueuedNetProvider.hpp"
#include "libreshockwave/player/xtra/SocketMultiuserBridge.hpp"
#include "libreshockwave/util/FileUtil.hpp"

#define GDK_DISABLE_DEPRECATION_WARNINGS
#define GTK_DISABLE_DEPRECATION_WARNINGS
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

namespace {

namespace fs = std::filesystem;

struct PlayerSettings {
    std::string movieSource;
    std::vector<std::pair<std::string, std::string>> params;
    std::optional<int> tempoOverride;
    int scriptTimeoutMs = 0;
    bool preloadCasts = true;
};

struct PlayerUi {
    PlayerSettings settings;
    fs::path settingsPath;
    GtkApplication* app{nullptr};
    GtkWidget* window{nullptr};
    GtkWidget* stageArea{nullptr};
    GtkWidget* titleLabel{nullptr};
    GtkWidget* subtitleLabel{nullptr};
    GtkWidget* statusLabel{nullptr};
    GtkWidget* frameLabel{nullptr};
    GtkWidget* playButton{nullptr};
    GtkWidget* stopButton{nullptr};
    GtkWidget* stepButton{nullptr};
    GtkWidget* reloadButton{nullptr};
    GtkWidget* paramsButton{nullptr};
    GtkWidget* tempoOverrideCheck{nullptr};
    GtkWidget* tempoSpin{nullptr};
    std::shared_ptr<libreshockwave::DirectorFile> file;
    std::unique_ptr<libreshockwave::player::net::QueuedNetProvider> netProvider;
    std::unique_ptr<libreshockwave::player::xtra::SocketMultiuserBridge> multiuserBridge;
    std::unique_ptr<libreshockwave::player::Player> player;
    std::unordered_map<std::string, std::vector<std::uint8_t>> httpCache;
    std::string currentMovieSource;
    std::string rememberedMovieSource;
    std::vector<std::uint32_t> stagePixels;
    int stageWidth = 0;
    int stageHeight = 0;
    int stageBackground = 0;
    guint playbackTimer = 0;
    bool playing = false;
    bool updatingControls = false;
};

struct StageTransform {
    double x = 0.0;
    double y = 0.0;
    double scale = 1.0;
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

bool asciiEqualsIgnoreCase(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
            std::tolower(static_cast<unsigned char>(rhs[index]))) {
            return false;
        }
    }
    return true;
}

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
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

fs::path defaultSettingsPath() {
    if (const char* xdgConfigHome = std::getenv("XDG_CONFIG_HOME");
        xdgConfigHome != nullptr && *xdgConfigHome != '\0') {
        return fs::path(xdgConfigHome) / "libreshockwave" / "player.conf";
    }

    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return fs::path(home) / ".config" / "libreshockwave" / "player.conf";
    }

    return fs::path(".libreshockwave-player.conf");
}

int parseInt(std::string_view value, std::string_view optionName, int minimum) {
    std::size_t consumed = 0;
    long long parsed = 0;
    try {
        parsed = std::stoll(std::string(value), &consumed, 10);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid value for " + std::string(optionName) + ": " + std::string(value));
    }

    if (consumed != value.size() || parsed < minimum ||
        parsed > static_cast<long long>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Invalid value for " + std::string(optionName) + ": " + std::string(value));
    }
    return static_cast<int>(parsed);
}

void setParam(PlayerSettings& settings, std::string key, std::string value) {
    if (key.empty()) {
        throw std::runtime_error("Parameter names cannot be empty");
    }

    for (auto& entry : settings.params) {
        if (asciiEqualsIgnoreCase(entry.first, key)) {
            entry.first = std::move(key);
            entry.second = std::move(value);
            return;
        }
    }
    settings.params.emplace_back(std::move(key), std::move(value));
}

void setParamAssignment(PlayerSettings& settings, std::string_view assignment, std::string_view optionName) {
    const auto equals = assignment.find('=');
    if (equals == std::string_view::npos) {
        throw std::runtime_error(std::string(optionName) + " expects key=value");
    }
    setParam(settings, trim(assignment.substr(0, equals)), std::string(assignment.substr(equals + 1)));
}

PlayerSettings loadSettings(const fs::path& path) {
    PlayerSettings settings;
    std::ifstream input(path);
    if (!input) {
        return settings;
    }

    std::string line;
    int lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        const std::string cleaned = trim(line);
        if (cleaned.empty() || cleaned.front() == '#') {
            continue;
        }

        const auto equals = cleaned.find('=');
        if (equals == std::string::npos) {
            throw std::runtime_error("Invalid settings line " + std::to_string(lineNumber) +
                                     " in " + pathString(path));
        }

        const std::string key = trim(std::string_view(cleaned).substr(0, equals));
        const std::string value = trim(std::string_view(cleaned).substr(equals + 1));
        if (key == "movie") {
            settings.movieSource = value;
        } else if (key == "tempoOverride") {
            settings.tempoOverride = value.empty()
                ? std::optional<int>{}
                : std::optional<int>{parseInt(value, "tempoOverride", 1)};
        } else if (key == "scriptTimeoutMs") {
            settings.scriptTimeoutMs = parseInt(value, "scriptTimeoutMs", 0);
        } else if (key == "preloadCasts") {
            settings.preloadCasts = value == "1" || value == "true" || value == "yes";
        } else if (startsWith(key, "param.")) {
            setParam(settings, key.substr(6), value);
        }
    }
    return settings;
}

void saveSettings(const PlayerSettings& settings, const fs::path& path) {
    if (path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        if (ec) {
            throw std::runtime_error("Unable to create settings directory " +
                                     pathString(path.parent_path()) + ": " + ec.message());
        }
    }

    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Unable to write settings: " + pathString(path));
    }

    output << "# LibreShockwave GTK player settings\n";
    output << "movie=" << settings.movieSource << '\n';
    output << "tempoOverride=";
    if (settings.tempoOverride.has_value()) {
        output << *settings.tempoOverride;
    }
    output << '\n';
    output << "scriptTimeoutMs=" << settings.scriptTimeoutMs << '\n';
    output << "preloadCasts=" << (settings.preloadCasts ? 1 : 0) << '\n';
    for (const auto& [key, value] : settings.params) {
        output << "param." << key << '=' << value << '\n';
    }
}

std::vector<std::uint8_t> readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open movie: " + pathString(path));
    }

    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end == std::ifstream::pos_type(-1)) {
        throw std::runtime_error("Unable to determine movie size: " + pathString(path));
    }
    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> data(static_cast<std::size_t>(end));
    if (!data.empty()) {
        input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!input) {
            throw std::runtime_error("Unable to read complete movie: " + pathString(path));
        }
    }
    return data;
}

fs::path stripUrlQueryAndFragment(std::string value) {
    const auto query = value.find('?');
    const auto fragment = value.find('#');
    std::size_t cut = std::string::npos;
    if (query != std::string::npos) {
        cut = query;
    }
    if (fragment != std::string::npos) {
        cut = std::min(cut, fragment);
    }
    if (cut != std::string::npos) {
        value.erase(cut);
    }
    return fs::path(value);
}

bool isHttpUrl(std::string_view value) {
    return startsWith(value, "http://") || startsWith(value, "https://");
}

std::size_t writeCurlData(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* data = static_cast<std::vector<std::uint8_t>*>(userdata);
    const std::size_t byteCount = size * nmemb;
    const auto* begin = reinterpret_cast<const std::uint8_t*>(ptr);
    data->insert(data->end(), begin, begin + byteCount);
    return byteCount;
}

std::vector<std::uint8_t> fetchHttpUrl(const std::string& url,
                                       const std::optional<std::string>& postData = std::nullopt) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        throw std::runtime_error("Unable to initialize libcurl");
    }

    std::vector<std::uint8_t> data;
    char errorBuffer[CURL_ERROR_SIZE]{};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "LibreShockwave/0.1");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCurlData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);

    if (postData.has_value()) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, postData->c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(postData->size()));
    }

    const CURLcode result = curl_easy_perform(curl);
    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        const std::string detail = errorBuffer[0] != '\0' ? errorBuffer : curl_easy_strerror(result);
        throw std::runtime_error("Unable to fetch " + url + ": " + detail);
    }
    if (statusCode >= 400) {
        throw std::runtime_error("Unable to fetch " + url + ": HTTP " + std::to_string(statusCode));
    }
    return data;
}

std::string normalizeMovieSource(std::string_view value) {
    const std::string source = trim(value);
    if (source.empty() || isHttpUrl(source)) {
        return source;
    }
    return pathString(absolutePath(fs::path(source)));
}

std::string sourceFileName(std::string_view source) {
    const std::string fileName = libreshockwave::util::getFileName(source);
    if (!fileName.empty()) {
        return fileName;
    }
    return std::string(source);
}

std::string urlOrigin(std::string_view url) {
    const auto schemeEnd = url.find("://");
    if (schemeEnd == std::string_view::npos) {
        return "";
    }
    const auto pathStart = url.find('/', schemeEnd + 3);
    return pathStart == std::string_view::npos ? std::string(url) : std::string(url.substr(0, pathStart));
}

std::string cleanUrl(std::string value);
std::string decodeUrlPath(std::string_view value);

std::string sourceDirectory(std::string_view source) {
    if (isHttpUrl(source)) {
        std::string cleaned = cleanUrl(std::string(source));
        const auto slash = cleaned.find_last_of('/');
        if (slash == std::string::npos) {
            return cleaned + "/";
        }
        return cleaned.substr(0, slash + 1);
    }

    const fs::path path(source);
    return path.has_parent_path() ? path.parent_path().string() : std::string();
}

std::vector<std::uint8_t> readMovieSource(const std::string& source) {
    if (isHttpUrl(source)) {
        return fetchHttpUrl(source);
    }
    return readFile(fs::path(source));
}

std::vector<std::pair<std::string, std::string>> parseUrlQueryParams(std::string_view url) {
    const auto queryBegin = url.find('?');
    if (queryBegin == std::string_view::npos) {
        return {};
    }

    const auto fragmentBegin = url.find('#', queryBegin + 1);
    std::string_view query = fragmentBegin == std::string_view::npos
        ? url.substr(queryBegin + 1)
        : url.substr(queryBegin + 1, fragmentBegin - queryBegin - 1);

    std::vector<std::pair<std::string, std::string>> params;
    while (!query.empty()) {
        const auto separator = query.find('&');
        const std::string_view part = separator == std::string_view::npos
            ? query
            : query.substr(0, separator);
        if (!part.empty()) {
            const auto equals = part.find('=');
            const std::string key = decodeUrlPath(equals == std::string_view::npos
                ? part
                : part.substr(0, equals));
            if (!key.empty()) {
                const std::string value = equals == std::string_view::npos
                    ? std::string()
                    : decodeUrlPath(part.substr(equals + 1));
                params.emplace_back(key, value);
            }
        }
        if (separator == std::string_view::npos) {
            break;
        }
        query.remove_prefix(separator + 1);
    }
    return params;
}

void applyUrlParams(PlayerSettings& settings,
                    const std::string& source,
                    bool importQueryParams,
                    bool fillSrcParam) {
    bool queryHadSrc = false;
    if (importQueryParams) {
        for (auto [key, value] : parseUrlQueryParams(source)) {
            if (asciiEqualsIgnoreCase(key, "src")) {
                queryHadSrc = true;
            }
            setParam(settings, std::move(key), std::move(value));
        }
    }

    if (fillSrcParam && !queryHadSrc) {
        setParam(settings, "src", cleanUrl(source));
    }
}

std::string toLower(std::string_view value) {
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) {
    return toLower(lhs) == toLower(rhs);
}

int hexValue(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    return -1;
}

std::string cleanUrl(std::string value) {
    if (const auto query = value.find('?'); query != std::string::npos) {
        value.erase(query);
    }
    if (const auto fragment = value.find('#'); fragment != std::string::npos) {
        value.erase(fragment);
    }
    return value;
}

std::string decodeUrlPath(std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char ch = value[index];
        if (ch == '%' && index + 2 < value.size()) {
            const int high = hexValue(value[index + 1]);
            const int low = hexValue(value[index + 2]);
            if (high >= 0 && low >= 0) {
                decoded.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }
        decoded.push_back(ch == '+' ? ' ' : ch);
    }
    return decoded;
}

std::string resolveMovieNavigationSource(const PlayerUi& ui, std::string_view url) {
    const std::string rawUrl = trim(url);
    if (rawUrl.empty()) {
        return {};
    }

    if (isHttpUrl(rawUrl)) {
        return rawUrl;
    }

    const std::string baseSource = !ui.currentMovieSource.empty()
        ? ui.currentMovieSource
        : ui.settings.movieSource;
    if (isHttpUrl(baseSource)) {
        if (startsWith(rawUrl, "/")) {
            const std::string origin = urlOrigin(baseSource);
            return origin.empty() ? rawUrl : origin + rawUrl;
        }
        return sourceDirectory(baseSource) + rawUrl;
    }

    const fs::path path = stripUrlQueryAndFragment(rawUrl);
    if (path.empty()) {
        return {};
    }
    if (path.is_absolute()) {
        const fs::path localWebPath = fs::path("/var/www/html") / path.relative_path();
        if (fs::exists(localWebPath)) {
            return pathString(localWebPath);
        }
        return pathString(path);
    }

    const fs::path base = !ui.currentMovieSource.empty()
        ? fs::path(ui.currentMovieSource).parent_path()
        : fs::path(ui.settings.movieSource).parent_path();
    return pathString(base / path);
}

void addFetchPathCandidates(std::vector<fs::path>& candidates,
                            std::string_view url,
                            const fs::path& movieDir) {
    const std::string cleaned = cleanUrl(std::string(url));
    const std::string decoded = decodeUrlPath(cleaned);
    for (const auto& value : {cleaned, decoded}) {
        if (value.empty()) {
            continue;
        }
        for (const auto& fallback : libreshockwave::util::getUrlsWithFallbacks(value)) {
            fs::path path(fallback);
            if (path.is_absolute()) {
                candidates.push_back(path);
                candidates.push_back(fs::path("/var/www/html") / path.relative_path());
            } else {
                candidates.push_back(movieDir / path);
            }

            const auto fileName = path.filename();
            if (!fileName.empty()) {
                candidates.push_back(movieDir / fileName);
            }

            const auto localCandidates = libreshockwave::util::getLocalFetchPathCandidates(movieDir, fallback);
            candidates.insert(candidates.end(), localCandidates.begin(), localCandidates.end());
        }
    }
}

std::optional<fs::path> resolveFetchPath(const std::vector<std::string>& urls, const fs::path& movieDir) {
    std::vector<fs::path> candidates;
    for (const auto& url : urls) {
        addFetchPathCandidates(candidates, url, movieDir);
    }

    std::error_code ec;
    for (const auto& candidate : candidates) {
        if (candidate.empty()) {
            continue;
        }
        if (fs::is_regular_file(candidate, ec) && !ec) {
            return candidate;
        }
        ec.clear();
    }
    return std::nullopt;
}

std::string resolveRemoteFetchUrl(std::string_view url, std::string_view movieSource) {
    const std::string value = trim(url);
    if (value.empty() || isHttpUrl(value)) {
        return value;
    }
    if (startsWith(value, "/")) {
        const std::string origin = urlOrigin(movieSource);
        return origin.empty() ? value : origin + value;
    }
    return sourceDirectory(movieSource) + value;
}

void setStatus(PlayerUi& ui, std::string_view message) {
    if (ui.statusLabel != nullptr) {
        gtk_label_set_text(GTK_LABEL(ui.statusLabel), std::string(message).c_str());
    }
}

bool isAlreadyLoadedCastRequest(const PlayerUi& ui, std::string_view url) {
    if (ui.player == nullptr) {
        return false;
    }

    const std::string fileName = libreshockwave::util::getFileName(url);
    const std::string lowerFileName = toLower(fileName);
    const bool castLikeRequest = lowerFileName.ends_with(".cct") ||
                                 lowerFileName.ends_with(".cst") ||
                                 lowerFileName.find('.') == std::string::npos;
    if (!castLikeRequest) {
        return false;
    }

    const std::string baseName = libreshockwave::util::getFileNameWithoutExtension(fileName);
    if (baseName.empty()) {
        return false;
    }

    for (const auto& [number, castLib] : ui.player->castLibManager().castLibs()) {
        (void)number;
        if (castLib == nullptr) {
            continue;
        }

        const bool nameMatches = !castLib->name().empty() && equalsIgnoreCase(castLib->name(), baseName);
        const std::string castFileBaseName =
            libreshockwave::util::getFileNameWithoutExtension(libreshockwave::util::getFileName(castLib->fileName()));
        const bool fileMatches = !castFileBaseName.empty() && equalsIgnoreCase(castFileBaseName, baseName);
        if (!nameMatches && !fileMatches) {
            continue;
        }

        if (!castLib->isExternal() && castLib->isLoaded()) {
            return true;
        }
        if (castLib->isExternal() && castLib->isFetched()) {
            return true;
        }
    }
    return false;
}

void pumpHostFetches(PlayerUi& ui) {
    if (ui.netProvider == nullptr) {
        return;
    }

    const std::string movieSource = !ui.currentMovieSource.empty()
        ? ui.currentMovieSource
        : ui.settings.movieSource;
    const bool remoteMovie = isHttpUrl(movieSource);
    const fs::path movieDir = remoteMovie ? fs::path() : fs::path(movieSource).parent_path();
    std::size_t deliveredCount = 0;
    std::size_t failedCount = 0;

    for (int round = 0; round < 64 && ui.netProvider->pendingRequestCount() > 0; ++round) {
        const auto pendingRequests = ui.netProvider->pendingRequests();
        ui.netProvider->drainPendingRequests();

        for (const auto& request : pendingRequests) {
            std::vector<std::string> urls;
            urls.push_back(request.url);
            urls.insert(urls.end(), request.fallbacks.begin(), request.fallbacks.end());

            if (remoteMovie) {
                std::string lastError;
                bool delivered = false;
                for (const auto& candidate : urls) {
                    const std::string fetchUrl = resolveRemoteFetchUrl(candidate, movieSource);
                    if (fetchUrl.empty()) {
                        continue;
                    }
                    try {
                        auto cached = request.method == "POST" ? ui.httpCache.end() : ui.httpCache.find(fetchUrl);
                        std::vector<std::uint8_t> data = cached == ui.httpCache.end()
                            ? fetchHttpUrl(fetchUrl, request.method == "POST" ? request.postData : std::nullopt)
                            : cached->second;
                        if (request.method != "POST" && cached == ui.httpCache.end()) {
                            ui.httpCache.emplace(fetchUrl, data);
                        }
                        ui.netProvider->onFetchComplete(request.taskId, std::move(data));
                        ++deliveredCount;
                        lastError.clear();
                        delivered = true;
                        break;
                    } catch (const std::exception& error) {
                        lastError = error.what();
                    }
                }
                if (!delivered) {
                    if (lastError.empty()) {
                        lastError = "no fetch URL candidates";
                    }
                    ui.netProvider->onFetchError(request.taskId, 404);
                    ++failedCount;
                }
                continue;
            }

            const auto fetchPath = resolveFetchPath(urls, movieDir);
            if (fetchPath.has_value()) {
                auto data = readFile(*fetchPath);
                ui.netProvider->onFetchComplete(request.taskId, std::move(data));
                ++deliveredCount;
            } else {
                ui.netProvider->onFetchError(request.taskId, 404);
                ++failedCount;
            }
        }
    }

    if (failedCount > 0) {
        setStatus(ui, "Delivered " + std::to_string(deliveredCount) +
                      " external fetches; " + std::to_string(failedCount) + " failed");
    }
}

void saveSettingsQuietly(PlayerUi& ui) {
    try {
        saveSettings(ui.settings, ui.settingsPath);
    } catch (const std::exception& error) {
        setStatus(ui, error.what());
    }
}

void cancelPlaybackTimer(PlayerUi& ui) {
    if (ui.playbackTimer != 0) {
        g_source_remove(ui.playbackTimer);
        ui.playbackTimer = 0;
    }
}

void shutdownMovie(PlayerUi& ui) {
    cancelPlaybackTimer(ui);
    ui.playing = false;
    if (ui.netProvider != nullptr) {
        ui.netProvider->completeMovieNavigationTasks();
    }
    if (ui.player != nullptr) {
        ui.player->shutdown();
        ui.player.reset();
    }
    ui.netProvider.reset();
    ui.multiuserBridge.reset();
    ui.file.reset();
    ui.httpCache.clear();
    ui.currentMovieSource.clear();
    ui.stagePixels.clear();
    ui.stageWidth = 0;
    ui.stageHeight = 0;
    ui.stageBackground = 0;
}

int movieBaseTempo(const PlayerUi& ui) {
    if (ui.file != nullptr && ui.file->tempo() > 0) {
        return ui.file->tempo();
    }
    return 15;
}

int effectiveTempo(const PlayerUi& ui) {
    if (ui.settings.tempoOverride.has_value()) {
        return *ui.settings.tempoOverride;
    }
    if (ui.player != nullptr) {
        return std::max(1, ui.player->tempo());
    }
    return movieBaseTempo(ui);
}

void applyTempo(PlayerUi& ui) {
    if (ui.player == nullptr) {
        return;
    }
    ui.player->setTempo(ui.settings.tempoOverride.value_or(movieBaseTempo(ui)));
}

std::uint8_t blendChannel(std::uint8_t foreground, std::uint8_t background, std::uint8_t alpha) {
    return static_cast<std::uint8_t>(
        (static_cast<int>(foreground) * alpha + static_cast<int>(background) * (255 - alpha) + 127) / 255);
}

std::optional<StageTransform> stageTransformForSize(const PlayerUi& ui, int width, int height) {
    if (ui.stageWidth <= 0 || ui.stageHeight <= 0 || width <= 0 || height <= 0) {
        return std::nullopt;
    }

    const double scale = std::min(static_cast<double>(width) / static_cast<double>(ui.stageWidth),
                                  static_cast<double>(height) / static_cast<double>(ui.stageHeight));
    if (scale <= 0.0 || !std::isfinite(scale)) {
        return std::nullopt;
    }

    const double targetWidth = static_cast<double>(ui.stageWidth) * scale;
    const double targetHeight = static_cast<double>(ui.stageHeight) * scale;
    return StageTransform{
        (static_cast<double>(width) - targetWidth) / 2.0,
        (static_cast<double>(height) - targetHeight) / 2.0,
        scale
    };
}

std::optional<std::pair<int, int>> stagePointFromWidgetPoint(PlayerUi& ui, double widgetX, double widgetY) {
    if (ui.stageArea == nullptr) {
        return std::nullopt;
    }

    const auto transform = stageTransformForSize(ui,
                                                 gtk_widget_get_width(ui.stageArea),
                                                 gtk_widget_get_height(ui.stageArea));
    if (!transform.has_value()) {
        return std::nullopt;
    }

    const int stageX = static_cast<int>(std::lround((widgetX - transform->x) / transform->scale));
    const int stageY = static_cast<int>(std::lround((widgetY - transform->y) / transform->scale));
    return std::pair<int, int>{stageX, stageY};
}

void setStageBitmap(PlayerUi& ui, const libreshockwave::bitmap::Bitmap& bitmap, int backgroundRgb) {
    ui.stageWidth = bitmap.width();
    ui.stageHeight = bitmap.height();
    ui.stageBackground = backgroundRgb;
    ui.stagePixels.clear();

    if (ui.stageWidth <= 0 || ui.stageHeight <= 0 ||
        bitmap.pixels().size() != static_cast<std::size_t>(ui.stageWidth * ui.stageHeight)) {
        ui.stageWidth = 0;
        ui.stageHeight = 0;
        return;
    }

    const auto backgroundRed = static_cast<std::uint8_t>((backgroundRgb >> 16) & 0xFF);
    const auto backgroundGreen = static_cast<std::uint8_t>((backgroundRgb >> 8) & 0xFF);
    const auto backgroundBlue = static_cast<std::uint8_t>(backgroundRgb & 0xFF);

    ui.stagePixels.reserve(bitmap.pixels().size());
    for (const std::uint32_t argb : bitmap.pixels()) {
        const auto alpha = static_cast<std::uint8_t>((argb >> 24) & 0xFF);
        const auto red = static_cast<std::uint8_t>((argb >> 16) & 0xFF);
        const auto green = static_cast<std::uint8_t>((argb >> 8) & 0xFF);
        const auto blue = static_cast<std::uint8_t>(argb & 0xFF);
        ui.stagePixels.push_back(
            0xFF000000U |
            (static_cast<std::uint32_t>(blendChannel(red, backgroundRed, alpha)) << 16) |
            (static_cast<std::uint32_t>(blendChannel(green, backgroundGreen, alpha)) << 8) |
            static_cast<std::uint32_t>(blendChannel(blue, backgroundBlue, alpha)));
    }
}

void updateWindowLabels(PlayerUi& ui) {
    const bool hasMovie = ui.player != nullptr && ui.file != nullptr;
    const std::string source = !ui.settings.movieSource.empty()
        ? ui.settings.movieSource
        : ui.currentMovieSource;
    const std::string title = hasMovie && !source.empty()
        ? sourceFileName(source)
        : "LibreShockwave Player";
    const std::string subtitle = hasMovie
        ? source
        : "Open a Director or Shockwave movie";

    if (ui.titleLabel != nullptr) {
        gtk_label_set_text(GTK_LABEL(ui.titleLabel), title.c_str());
    }
    if (ui.subtitleLabel != nullptr) {
        gtk_label_set_text(GTK_LABEL(ui.subtitleLabel), subtitle.c_str());
    }
    if (ui.window != nullptr) {
        const std::string windowTitle = hasMovie ? "LibreShockwave Player - " + title : "LibreShockwave Player";
        gtk_window_set_title(GTK_WINDOW(ui.window), windowTitle.c_str());
    }
}

void updateControls(PlayerUi& ui) {
    ui.updatingControls = true;
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
    if (ui.stepButton != nullptr) {
        gtk_widget_set_sensitive(ui.stepButton, loaded);
    }
    if (ui.reloadButton != nullptr) {
        gtk_widget_set_sensitive(ui.reloadButton, !ui.settings.movieSource.empty());
    }
    if (ui.paramsButton != nullptr) {
        gtk_widget_set_tooltip_text(ui.paramsButton,
                                    ("External parameters (" + std::to_string(ui.settings.params.size()) + ")").c_str());
    }
    if (ui.tempoOverrideCheck != nullptr) {
        gtk_check_button_set_active(GTK_CHECK_BUTTON(ui.tempoOverrideCheck), ui.settings.tempoOverride.has_value());
    }
    if (ui.tempoSpin != nullptr) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui.tempoSpin),
                                  static_cast<double>(ui.settings.tempoOverride.value_or(effectiveTempo(ui))));
        gtk_widget_set_sensitive(ui.tempoSpin, ui.settings.tempoOverride.has_value());
    }
    ui.updatingControls = false;
}

void renderCurrentFrame(PlayerUi& ui) {
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
        updateControls(ui);
        updateWindowLabels(ui);
        return;
    }

    const auto snapshot = ui.player->frameSnapshot();
    auto frame = snapshot.renderFrame();
    libreshockwave::player::InputHandler::applyEditableFieldOverlay(
        frame,
        ui.player->inputHandler().editableFieldOverlay());
    setStageBitmap(ui, frame, snapshot.backgroundColor);
    if (ui.stageArea != nullptr) {
        gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(ui.stageArea), std::max(320, ui.stageWidth));
        gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(ui.stageArea), std::max(240, ui.stageHeight));
        gtk_widget_queue_draw(ui.stageArea);
    }

    if (ui.frameLabel != nullptr) {
        std::ostringstream text;
        text << "Frame " << ui.player->currentFrame() << " / " << ui.player->frameCount()
             << "    Tempo " << effectiveTempo(ui) << " fps";
        if (ui.settings.tempoOverride.has_value()) {
            text << " override";
        }
        gtk_label_set_text(GTK_LABEL(ui.frameLabel), text.str().c_str());
    }
    updateControls(ui);
    updateWindowLabels(ui);
}

guint playbackDelayMs(const PlayerUi& ui) {
    const int tempo = std::clamp(effectiveTempo(ui), 1, 240);
    return static_cast<guint>(std::max(1, static_cast<int>(std::lround(1000.0 / static_cast<double>(tempo)))));
}

gboolean playbackTick(gpointer userData);

void schedulePlaybackTick(PlayerUi& ui) {
    if (!ui.playing || ui.player == nullptr || ui.playbackTimer != 0) {
        return;
    }
    ui.playbackTimer = g_timeout_add_full(G_PRIORITY_DEFAULT,
                                          playbackDelayMs(ui),
                                          playbackTick,
                                          &ui,
                                          nullptr);
}

gboolean playbackTick(gpointer userData) {
    auto& ui = *static_cast<PlayerUi*>(userData);
    ui.playbackTimer = 0;
    if (!ui.playing || ui.player == nullptr) {
        return G_SOURCE_REMOVE;
    }

    try {
        ui.player->resume();
        const bool keepGoing = ui.player->tick();
        pumpHostFetches(ui);
        renderCurrentFrame(ui);
        if (!keepGoing) {
            ui.playing = false;
            updateControls(ui);
            return G_SOURCE_REMOVE;
        }
    } catch (const std::exception& error) {
        ui.playing = false;
        setStatus(ui, error.what());
        updateControls(ui);
        return G_SOURCE_REMOVE;
    }

    schedulePlaybackTick(ui);
    return G_SOURCE_REMOVE;
}

void setPlaying(PlayerUi& ui, bool playing) {
    if (ui.player == nullptr) {
        return;
    }
    ui.playing = playing;
    if (playing) {
        ui.player->resume();
        pumpHostFetches(ui);
        schedulePlaybackTick(ui);
    } else {
        cancelPlaybackTimer(ui);
        ui.player->pause();
    }
    updateControls(ui);
}

bool loadMovie(PlayerUi& ui, const std::string& source, bool rememberSource) {
    try {
        shutdownMovie(ui);
        const std::string movieSource = normalizeMovieSource(source);
        const auto movieBytes = readMovieSource(movieSource);
        auto file = libreshockwave::DirectorFile::load(movieBytes);
        file->setBasePath(sourceDirectory(movieSource));

        auto netProvider = std::make_unique<libreshockwave::player::net::QueuedNetProvider>(movieSource);
        auto multiuserBridge = std::make_unique<libreshockwave::player::xtra::SocketMultiuserBridge>();
        auto player = std::make_unique<libreshockwave::player::Player>(file, netProvider.get());
        player->registerMultiuserXtra(*multiuserBridge);
        player->setExternalParams(ui.settings.params);
        player->vm().setTickDeadlineMs(ui.settings.scriptTimeoutMs);
        player->setErrorListener([&ui](std::string_view message, std::string_view detail) {
            std::string status = "[script] " + std::string(message);
            if (!detail.empty()) {
                status += ": ";
                status += detail;
            }
            setStatus(ui, status);
        });

        ui.file = std::move(file);
        ui.netProvider = std::move(netProvider);
        ui.multiuserBridge = std::move(multiuserBridge);
        ui.player = std::move(player);
        ui.currentMovieSource = movieSource;
        if (rememberSource) {
            ui.rememberedMovieSource = ui.currentMovieSource;
        }
        ui.player->movieProperties().setGotoNetMovieHandler([&ui](const std::string& url) {
            const int requestId = ui.netProvider != nullptr ? ui.netProvider->beginMovieNavigation(url) : 0;
            const std::string resolvedSource = resolveMovieNavigationSource(ui, url);
            if (resolvedSource.empty()) {
                setStatus(ui, "Movie navigation requested an empty URL");
                return -1;
            }

            auto* payload = new std::pair<PlayerUi*, std::string>{&ui, resolvedSource};
            g_idle_add_full(G_PRIORITY_DEFAULT,
                            [](gpointer data) -> gboolean {
                                std::unique_ptr<std::pair<PlayerUi*, std::string>> payload(
                                    static_cast<std::pair<PlayerUi*, std::string>*>(data));
                                PlayerUi& targetUi = *payload->first;
                                const std::string rememberedSource = targetUi.rememberedMovieSource.empty()
                                    ? targetUi.settings.movieSource
                                    : targetUi.rememberedMovieSource;
                                loadMovie(targetUi, payload->second, false);
                                if (!rememberedSource.empty()) {
                                    targetUi.settings.movieSource = rememberedSource;
                                    targetUi.rememberedMovieSource = rememberedSource;
                                    saveSettingsQuietly(targetUi);
                                }
                                return G_SOURCE_REMOVE;
                            },
                            payload,
                            nullptr);
            return requestId;
        });
        ui.netProvider->setFetchCompleteCallback([&ui](const std::string& url,
                                                       const std::vector<std::uint8_t>& data) {
            if (ui.player != nullptr) {
                ui.player->onNetFetchComplete(url, data);
            }
        });
        ui.netProvider->setSatisfiedFetchPredicate([&ui](std::string_view url) {
            return isAlreadyLoadedCastRequest(ui, url);
        });
        pumpHostFetches(ui);
        applyTempo(ui);
        int explicitCastRequests = 0;
        if (ui.settings.preloadCasts) {
            explicitCastRequests = ui.player->preloadAllCasts();
            pumpHostFetches(ui);
        }
        ui.player->play();
        pumpHostFetches(ui);
        ui.playing = true;

        if (rememberSource) {
            ui.settings.movieSource = ui.currentMovieSource;
        }
        saveSettingsQuietly(ui);
        renderCurrentFrame(ui);

        std::ostringstream status;
        status << "Loaded " << movieSource
               << "    Stage " << ui.file->stageWidth() << 'x' << ui.file->stageHeight()
               << "    Movie tempo " << movieBaseTempo(ui) << " fps"
               << "    External cast requests " << explicitCastRequests;
        setStatus(ui, status.str());
        schedulePlaybackTick(ui);
        return true;
    } catch (const std::exception& error) {
        shutdownMovie(ui);
        renderCurrentFrame(ui);
        setStatus(ui, error.what());
        return false;
    }
}

void drawStage(GtkDrawingArea*, cairo_t* cr, int width, int height, gpointer userData) {
    auto& ui = *static_cast<PlayerUi*>(userData);
    cairo_set_source_rgb(cr, 0.08, 0.08, 0.08);
    cairo_paint(cr);

    if (ui.stagePixels.empty() || ui.stageWidth <= 0 || ui.stageHeight <= 0) {
        cairo_set_source_rgb(cr, 0.58, 0.58, 0.58);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 15.0);
        const char* message = "Open a .dcr, .dir, or .dxr movie";
        cairo_text_extents_t extents{};
        cairo_text_extents(cr, message, &extents);
        cairo_move_to(cr,
                      (static_cast<double>(width) - extents.width) / 2.0 - extents.x_bearing,
                      (static_cast<double>(height) - extents.height) / 2.0 - extents.y_bearing);
        cairo_show_text(cr, message);
        return;
    }

    const auto transform = stageTransformForSize(ui, width, height);
    if (!transform.has_value()) {
        return;
    }
    const double targetWidth = static_cast<double>(ui.stageWidth) * transform->scale;
    const double targetHeight = static_cast<double>(ui.stageHeight) * transform->scale;

    cairo_save(cr);
    cairo_rectangle(cr, transform->x, transform->y, targetWidth, targetHeight);
    cairo_clip(cr);
    cairo_translate(cr, transform->x, transform->y);
    cairo_scale(cr, transform->scale, transform->scale);
    cairo_surface_t* surface = cairo_image_surface_create_for_data(
        reinterpret_cast<unsigned char*>(ui.stagePixels.data()),
        CAIRO_FORMAT_ARGB32,
        ui.stageWidth,
        ui.stageHeight,
        ui.stageWidth * 4);
    cairo_set_source_surface(cr, surface, 0.0, 0.0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_surface_destroy(surface);
    cairo_restore(cr);
}

bool hasControlModifier(GdkModifierType state) {
    return (state & GDK_CONTROL_MASK) != 0;
}

bool hasAltModifier(GdkModifierType state) {
    return (state & GDK_ALT_MASK) != 0;
}

bool hasShiftModifier(GdkModifierType state) {
    return (state & GDK_SHIFT_MASK) != 0;
}

int browserKeyCodeFromGdkKeyval(guint keyval) {
    switch (keyval) {
        case GDK_KEY_BackSpace: return 8;
        case GDK_KEY_Tab:
        case GDK_KEY_ISO_Left_Tab: return 9;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter: return 13;
        case GDK_KEY_Escape: return 27;
        case GDK_KEY_space: return 32;
        case GDK_KEY_Page_Up:
        case GDK_KEY_KP_Page_Up: return 33;
        case GDK_KEY_Page_Down:
        case GDK_KEY_KP_Page_Down: return 34;
        case GDK_KEY_End:
        case GDK_KEY_KP_End: return 35;
        case GDK_KEY_Home:
        case GDK_KEY_KP_Home: return 36;
        case GDK_KEY_Left:
        case GDK_KEY_KP_Left: return 37;
        case GDK_KEY_Up:
        case GDK_KEY_KP_Up: return 38;
        case GDK_KEY_Right:
        case GDK_KEY_KP_Right: return 39;
        case GDK_KEY_Down:
        case GDK_KEY_KP_Down: return 40;
        case GDK_KEY_Insert:
        case GDK_KEY_KP_Insert: return 45;
        case GDK_KEY_Delete:
        case GDK_KEY_KP_Delete: return 46;
        case GDK_KEY_F1: return 112;
        case GDK_KEY_F2: return 113;
        case GDK_KEY_F3: return 114;
        case GDK_KEY_F4: return 115;
        case GDK_KEY_F5: return 116;
        case GDK_KEY_F6: return 117;
        case GDK_KEY_F7: return 118;
        case GDK_KEY_F8: return 119;
        case GDK_KEY_F9: return 120;
        case GDK_KEY_F10: return 121;
        case GDK_KEY_F11: return 122;
        case GDK_KEY_F12: return 123;
        default:
            break;
    }

    const guint upper = gdk_keyval_to_upper(keyval);
    if (upper >= GDK_KEY_A && upper <= GDK_KEY_Z) {
        return 65 + static_cast<int>(upper - GDK_KEY_A);
    }
    if (keyval >= GDK_KEY_0 && keyval <= GDK_KEY_9) {
        return 48 + static_cast<int>(keyval - GDK_KEY_0);
    }
    if (keyval >= GDK_KEY_KP_0 && keyval <= GDK_KEY_KP_9) {
        return 48 + static_cast<int>(keyval - GDK_KEY_KP_0);
    }

    const gunichar unicode = gdk_keyval_to_unicode(keyval);
    if (unicode > 0 && unicode <= 255) {
        return static_cast<int>(unicode);
    }
    return static_cast<int>(keyval);
}

std::string keyTextFromGdkKeyval(guint keyval, GdkModifierType state) {
    if (hasControlModifier(state) || hasAltModifier(state)) {
        return {};
    }

    if (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab) {
        return "\t";
    }
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        return "\r";
    }

    const gunichar unicode = gdk_keyval_to_unicode(keyval);
    if (unicode == 0 || !g_unichar_isprint(unicode)) {
        return {};
    }

    char buffer[7]{};
    const int length = g_unichar_to_utf8(unicode, buffer);
    return length > 0 ? std::string(buffer, static_cast<std::size_t>(length)) : std::string();
}

bool processQueuedInput(PlayerUi& ui) {
    if (ui.player == nullptr) {
        return false;
    }

    try {
        const bool processed = ui.player->inputHandler().processInputEvents();
        pumpHostFetches(ui);
        renderCurrentFrame(ui);
        return processed;
    } catch (const std::exception& error) {
        setStatus(ui, error.what());
        return false;
    }
}

void stageMotion(GtkEventControllerMotion*, double x, double y, gpointer userData) {
    auto& ui = *static_cast<PlayerUi*>(userData);
    if (ui.player == nullptr) {
        return;
    }
    if (const auto point = stagePointFromWidgetPoint(ui, x, y)) {
        ui.player->inputHandler().onMouseMove(point->first, point->second);
        processQueuedInput(ui);
    }
}

void stagePointerLeave(GtkEventControllerMotion*, gpointer userData) {
    auto& ui = *static_cast<PlayerUi*>(userData);
    if (ui.player == nullptr) {
        return;
    }
    ui.player->inputHandler().onBlur();
    processQueuedInput(ui);
}

void stagePressed(GtkGestureClick* gesture, int, double x, double y, gpointer userData) {
    auto& ui = *static_cast<PlayerUi*>(userData);
    if (ui.player == nullptr) {
        return;
    }
    gtk_widget_grab_focus(ui.stageArea);
    if (const auto point = stagePointFromWidgetPoint(ui, x, y)) {
        const guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
        if (button == GDK_BUTTON_PRIMARY || button == GDK_BUTTON_SECONDARY) {
            ui.player->inputHandler().onMouseDown(point->first, point->second, button == GDK_BUTTON_SECONDARY);
            processQueuedInput(ui);
        }
    }
}

void stageReleased(GtkGestureClick* gesture, int, double x, double y, gpointer userData) {
    auto& ui = *static_cast<PlayerUi*>(userData);
    if (ui.player == nullptr) {
        return;
    }
    if (const auto point = stagePointFromWidgetPoint(ui, x, y)) {
        const guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
        if (button == GDK_BUTTON_PRIMARY || button == GDK_BUTTON_SECONDARY) {
            ui.player->inputHandler().onMouseUp(point->first, point->second, button == GDK_BUTTON_SECONDARY);
            processQueuedInput(ui);
        }
    }
}

bool isShortcutKey(guint keyval, char expected) {
    const guint lower = gdk_keyval_to_lower(keyval);
    return lower == static_cast<guint>(expected);
}

void copySelectedTextToClipboard(PlayerUi& ui) {
    if (ui.player == nullptr || ui.stageArea == nullptr) {
        return;
    }
    const auto selected = ui.player->inputHandler().getSelectedText();
    if (!selected.has_value()) {
        return;
    }
    GdkClipboard* clipboard = gtk_widget_get_clipboard(ui.stageArea);
    if (clipboard != nullptr) {
        gdk_clipboard_set_text(clipboard, selected->c_str());
    }
}

void pasteClipboardTextReady(GObject* source, GAsyncResult* result, gpointer userData) {
    auto& ui = *static_cast<PlayerUi*>(userData);
    if (ui.player == nullptr) {
        return;
    }

    GError* error = nullptr;
    char* text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), result, &error);
    if (error != nullptr) {
        setStatus(ui, error->message);
        g_error_free(error);
        return;
    }
    if (text != nullptr) {
        ui.player->inputHandler().onPasteText(text);
        g_free(text);
        processQueuedInput(ui);
    }
}

void pasteTextFromClipboard(PlayerUi& ui) {
    if (ui.stageArea == nullptr) {
        return;
    }
    GdkClipboard* clipboard = gtk_widget_get_clipboard(ui.stageArea);
    if (clipboard != nullptr) {
        gdk_clipboard_read_text_async(clipboard, nullptr, pasteClipboardTextReady, &ui);
    }
}

bool handleStageShortcut(PlayerUi& ui, guint keyval, GdkModifierType state) {
    if (!hasControlModifier(state) || ui.player == nullptr) {
        return false;
    }

    if (isShortcutKey(keyval, 'a')) {
        ui.player->inputHandler().selectAll();
        renderCurrentFrame(ui);
        return true;
    }
    if (isShortcutKey(keyval, 'c')) {
        copySelectedTextToClipboard(ui);
        return true;
    }
    if (isShortcutKey(keyval, 'x')) {
        copySelectedTextToClipboard(ui);
        (void)ui.player->inputHandler().cutSelectedText();
        renderCurrentFrame(ui);
        return true;
    }
    if (isShortcutKey(keyval, 'v')) {
        pasteTextFromClipboard(ui);
        return true;
    }
    return false;
}

gboolean stageKeyPressed(GtkEventControllerKey*, guint keyval, guint, GdkModifierType state, gpointer userData) {
    auto& ui = *static_cast<PlayerUi*>(userData);
    if (ui.player == nullptr) {
        return FALSE;
    }
    if (handleStageShortcut(ui, keyval, state)) {
        return TRUE;
    }

    const int directorKeyCode =
        libreshockwave::player::input::DirectorKeyCodes::fromBrowserKeyCode(browserKeyCodeFromGdkKeyval(keyval));
    ui.player->inputHandler().onKeyDown(directorKeyCode,
                                        keyTextFromGdkKeyval(keyval, state),
                                        hasShiftModifier(state),
                                        hasControlModifier(state),
                                        hasAltModifier(state));
    processQueuedInput(ui);
    return TRUE;
}

void stageKeyReleased(GtkEventControllerKey*, guint keyval, guint, GdkModifierType state, gpointer userData) {
    auto& ui = *static_cast<PlayerUi*>(userData);
    if (ui.player == nullptr) {
        return;
    }

    const int directorKeyCode =
        libreshockwave::player::input::DirectorKeyCodes::fromBrowserKeyCode(browserKeyCodeFromGdkKeyval(keyval));
    ui.player->inputHandler().onKeyUp(directorKeyCode,
                                      keyTextFromGdkKeyval(keyval, state),
                                      hasShiftModifier(state),
                                      hasControlModifier(state),
                                      hasAltModifier(state));
    processQueuedInput(ui);
}

void stageFocusLeave(GtkEventControllerFocus*, gpointer userData) {
    auto& ui = *static_cast<PlayerUi*>(userData);
    if (ui.player == nullptr) {
        return;
    }
    ui.player->inputHandler().onBlur();
    processQueuedInput(ui);
}

GtkWidget* iconButton(const char* iconName, const char* tooltip) {
    GtkWidget* button = gtk_button_new_from_icon_name(iconName);
    gtk_widget_set_size_request(button, 38, 34);
    gtk_widget_set_tooltip_text(button, tooltip);
    return button;
}

constexpr int OpenMovieLocalResponse = 1;
constexpr int OpenMovieUrlResponse = 2;

struct UrlDialogData {
    PlayerUi* ui{nullptr};
    GtkWidget* urlEntry{nullptr};
    GtkWidget* importQueryCheck{nullptr};
    GtkWidget* fillSrcCheck{nullptr};
    GtkWidget* errorLabel{nullptr};
};

void openLocalMovieResponse(GtkNativeDialog* dialog, int response, gpointer userData) {
    auto& ui = *static_cast<PlayerUi*>(userData);
    if (response == GTK_RESPONSE_ACCEPT) {
        GFile* file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        if (file != nullptr) {
            char* path = g_file_get_path(file);
            if (path != nullptr) {
                loadMovie(ui, pathString(absolutePath(path)), true);
                g_free(path);
            }
            g_object_unref(file);
        }
    }
    gtk_native_dialog_destroy(dialog);
}

void showOpenLocalMovieDialog(PlayerUi& ui) {
    GtkFileChooserNative* dialog = gtk_file_chooser_native_new("Open Movie",
                                                               GTK_WINDOW(ui.window),
                                                               GTK_FILE_CHOOSER_ACTION_OPEN,
                                                               "_Open",
                                                               "_Cancel");
    GtkFileFilter* directorFilter = gtk_file_filter_new();
    gtk_file_filter_set_name(directorFilter, "Director and Shockwave movies");
    gtk_file_filter_add_pattern(directorFilter, "*.dcr");
    gtk_file_filter_add_pattern(directorFilter, "*.dir");
    gtk_file_filter_add_pattern(directorFilter, "*.dxr");
    gtk_file_filter_add_pattern(directorFilter, "*.cct");
    gtk_file_filter_add_pattern(directorFilter, "*.cst");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), directorFilter);

    if (!ui.settings.movieSource.empty() && !isHttpUrl(ui.settings.movieSource)) {
        GFile* current = g_file_new_for_path(ui.settings.movieSource.c_str());
        gtk_file_chooser_set_file(GTK_FILE_CHOOSER(dialog), current, nullptr);
        g_object_unref(current);
    }

    g_signal_connect(dialog, "response", G_CALLBACK(openLocalMovieResponse), &ui);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}

void openUrlMovieResponse(GtkDialog* dialog, int response, gpointer userData) {
    auto* data = static_cast<UrlDialogData*>(userData);
    if (response == GTK_RESPONSE_ACCEPT) {
        const char* text = gtk_editable_get_text(GTK_EDITABLE(data->urlEntry));
        const std::string source = normalizeMovieSource(text == nullptr ? "" : text);
        if (source.empty() || !isHttpUrl(source)) {
            gtk_label_set_text(GTK_LABEL(data->errorLabel), "Enter an HTTP or HTTPS movie URL.");
            return;
        }

        applyUrlParams(data->ui->settings,
                       source,
                       gtk_check_button_get_active(GTK_CHECK_BUTTON(data->importQueryCheck)),
                       gtk_check_button_get_active(GTK_CHECK_BUTTON(data->fillSrcCheck)));
        loadMovie(*data->ui, source, true);
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
    delete data;
}

void showOpenUrlMovieDialog(PlayerUi& ui) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Open Movie URL",
                                                    GTK_WINDOW(ui.window),
                                                    GTK_DIALOG_MODAL,
                                                    "_Cancel",
                                                    GTK_RESPONSE_CANCEL,
                                                    "_Open",
                                                    GTK_RESPONSE_ACCEPT,
                                                    nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 520, -1);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_box_append(GTK_BOX(content), box);

    GtkWidget* label = gtk_label_new("Movie URL");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget* urlEntry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(urlEntry), "https://example.com/path/movie.dcr");
    gtk_entry_set_input_purpose(GTK_ENTRY(urlEntry), GTK_INPUT_PURPOSE_URL);
    gtk_entry_set_activates_default(GTK_ENTRY(urlEntry), TRUE);
    gtk_widget_set_hexpand(urlEntry, TRUE);
    if (isHttpUrl(ui.settings.movieSource)) {
        gtk_editable_set_text(GTK_EDITABLE(urlEntry), ui.settings.movieSource.c_str());
    }
    gtk_box_append(GTK_BOX(box), urlEntry);

    GtkWidget* importQueryCheck = gtk_check_button_new_with_label("Use URL query string as external parameters");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(importQueryCheck), TRUE);
    gtk_box_append(GTK_BOX(box), importQueryCheck);

    GtkWidget* fillSrcCheck = gtk_check_button_new_with_label("Set src to the movie URL when the query does not provide it");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(fillSrcCheck), TRUE);
    gtk_box_append(GTK_BOX(box), fillSrcCheck);

    GtkWidget* errorLabel = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(errorLabel), 0.0F);
    gtk_widget_add_css_class(errorLabel, "error");
    gtk_box_append(GTK_BOX(box), errorLabel);

    auto* data = new UrlDialogData{&ui, urlEntry, importQueryCheck, fillSrcCheck, errorLabel};
    g_signal_connect(dialog, "response", G_CALLBACK(openUrlMovieResponse), data);
    gtk_window_present(GTK_WINDOW(dialog));
    gtk_widget_grab_focus(urlEntry);
}

void openMovieChoiceResponse(GtkDialog* dialog, int response, gpointer userData) {
    auto& ui = *static_cast<PlayerUi*>(userData);
    gtk_window_destroy(GTK_WINDOW(dialog));

    if (response == OpenMovieLocalResponse) {
        showOpenLocalMovieDialog(ui);
    } else if (response == OpenMovieUrlResponse) {
        showOpenUrlMovieDialog(ui);
    }
}

void openMovieClicked(GtkButton*, gpointer userData) {
    auto& ui = *static_cast<PlayerUi*>(userData);
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Open Movie",
                                                    GTK_WINDOW(ui.window),
                                                    GTK_DIALOG_MODAL,
                                                    "_Cancel",
                                                    GTK_RESPONSE_CANCEL,
                                                    "Local _File",
                                                    OpenMovieLocalResponse,
                                                    "_URL",
                                                    OpenMovieUrlResponse,
                                                    nullptr);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_box_append(GTK_BOX(content), box);

    GtkWidget* label = gtk_label_new("Open a local Director/Shockwave file or load one from an HTTP(S) URL.");
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    gtk_box_append(GTK_BOX(box), label);

    g_signal_connect(dialog, "response", G_CALLBACK(openMovieChoiceResponse), &ui);
    gtk_window_present(GTK_WINDOW(dialog));
}

void reloadClicked(GtkButton*, gpointer userData) {
    auto& ui = *static_cast<PlayerUi*>(userData);
    if (!ui.settings.movieSource.empty()) {
        loadMovie(ui, ui.settings.movieSource, false);
    }
}

void playClicked(GtkButton*, gpointer userData) {
    auto& ui = *static_cast<PlayerUi*>(userData);
    setPlaying(ui, !ui.playing);
}

void stopClicked(GtkButton*, gpointer userData) {
    auto& ui = *static_cast<PlayerUi*>(userData);
    if (ui.player == nullptr) {
        return;
    }
    cancelPlaybackTimer(ui);
    ui.playing = false;
    ui.player->stop();
    ui.player->play();
    pumpHostFetches(ui);
    ui.player->pause();
    renderCurrentFrame(ui);
}

void stepClicked(GtkButton*, gpointer userData) {
    auto& ui = *static_cast<PlayerUi*>(userData);
    if (ui.player == nullptr) {
        return;
    }
    setPlaying(ui, false);
    try {
        ui.player->stepFrame();
        pumpHostFetches(ui);
        renderCurrentFrame(ui);
    } catch (const std::exception& error) {
        setStatus(ui, error.what());
    }
}

void tempoOverrideToggled(GtkCheckButton*, gpointer userData) {
    auto& ui = *static_cast<PlayerUi*>(userData);
    if (ui.updatingControls) {
        return;
    }
    const bool active = gtk_check_button_get_active(GTK_CHECK_BUTTON(ui.tempoOverrideCheck));
    ui.settings.tempoOverride = active
        ? std::optional<int>{gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ui.tempoSpin))}
        : std::optional<int>{};
    applyTempo(ui);
    saveSettingsQuietly(ui);
    renderCurrentFrame(ui);
}

void tempoChanged(GtkSpinButton*, gpointer userData) {
    auto& ui = *static_cast<PlayerUi*>(userData);
    if (ui.updatingControls || !gtk_check_button_get_active(GTK_CHECK_BUTTON(ui.tempoOverrideCheck))) {
        return;
    }
    ui.settings.tempoOverride = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ui.tempoSpin));
    applyTempo(ui);
    saveSettingsQuietly(ui);
    renderCurrentFrame(ui);
}

struct ParamsDialogData {
    PlayerUi* ui{nullptr};
    GtkWidget* rowsBox{nullptr};
    GtkWidget* errorLabel{nullptr};
    struct Row {
        GtkWidget* container{nullptr};
        GtkWidget* keyEntry{nullptr};
        GtkWidget* valueEntry{nullptr};
        GtkWidget* removeButton{nullptr};
    };
    std::vector<Row> rows;
};

void appendParamRow(ParamsDialogData& data,
                    std::string_view key = {},
                    std::string_view value = {},
                    bool focusKey = false);

void setParamsDialogError(ParamsDialogData& data, std::string_view message) {
    if (data.errorLabel != nullptr) {
        gtk_label_set_text(GTK_LABEL(data.errorLabel), std::string(message).c_str());
    }
}

ParamsDialogData::Row* findParamRow(ParamsDialogData& data, GtkWidget* widget) {
    for (auto& row : data.rows) {
        if (row.container == widget || row.keyEntry == widget ||
            row.valueEntry == widget || row.removeButton == widget) {
            return &row;
        }
    }
    return nullptr;
}

void paramKeyActivated(GtkEntry* entry, gpointer userData) {
    auto& data = *static_cast<ParamsDialogData*>(userData);
    if (auto* row = findParamRow(data, GTK_WIDGET(entry)); row != nullptr) {
        gtk_widget_grab_focus(row->valueEntry);
    }
}

void paramValueActivated(GtkEntry* entry, gpointer userData) {
    auto& data = *static_cast<ParamsDialogData*>(userData);
    for (std::size_t index = 0; index < data.rows.size(); ++index) {
        if (data.rows[index].valueEntry != GTK_WIDGET(entry)) {
            continue;
        }
        if (index + 1 < data.rows.size()) {
            gtk_widget_grab_focus(data.rows[index + 1].keyEntry);
        } else {
            appendParamRow(data, {}, {}, true);
        }
        return;
    }
}

void removeParamRowClicked(GtkButton* button, gpointer userData) {
    auto& data = *static_cast<ParamsDialogData*>(userData);
    const auto it = std::find_if(data.rows.begin(), data.rows.end(), [button](const ParamsDialogData::Row& row) {
        return row.removeButton == GTK_WIDGET(button);
    });
    if (it == data.rows.end()) {
        return;
    }

    gtk_box_remove(GTK_BOX(data.rowsBox), it->container);
    data.rows.erase(it);
    if (data.rows.empty()) {
        appendParamRow(data, {}, {}, true);
    }
}

void appendParamRow(ParamsDialogData& data,
                    std::string_view key,
                    std::string_view value,
                    bool focusKey) {
    GtkWidget* rowBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_hexpand(rowBox, TRUE);

    GtkWidget* keyEntry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(keyEntry), "name");
    gtk_entry_set_activates_default(GTK_ENTRY(keyEntry), FALSE);
    gtk_widget_set_size_request(keyEntry, 180, -1);
    gtk_editable_set_text(GTK_EDITABLE(keyEntry), std::string(key).c_str());
    gtk_box_append(GTK_BOX(rowBox), keyEntry);

    GtkWidget* valueEntry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(valueEntry), "value");
    gtk_entry_set_activates_default(GTK_ENTRY(valueEntry), FALSE);
    gtk_widget_set_hexpand(valueEntry, TRUE);
    gtk_editable_set_text(GTK_EDITABLE(valueEntry), std::string(value).c_str());
    gtk_box_append(GTK_BOX(rowBox), valueEntry);

    GtkWidget* removeButton = iconButton("list-remove-symbolic", "Remove parameter");
    gtk_box_append(GTK_BOX(rowBox), removeButton);

    data.rows.push_back({rowBox, keyEntry, valueEntry, removeButton});
    gtk_box_append(GTK_BOX(data.rowsBox), rowBox);

    g_signal_connect(keyEntry, "activate", G_CALLBACK(paramKeyActivated), &data);
    g_signal_connect(valueEntry, "activate", G_CALLBACK(paramValueActivated), &data);
    g_signal_connect(removeButton, "clicked", G_CALLBACK(removeParamRowClicked), &data);

    if (focusKey) {
        gtk_widget_grab_focus(keyEntry);
    }
}

std::vector<std::pair<std::string, std::string>> collectParamRows(const ParamsDialogData& data) {
    PlayerSettings parsed;
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
        setParam(parsed, std::move(key), std::move(value));
    }
    return parsed.params;
}

void addParamClicked(GtkButton*, gpointer userData) {
    auto& data = *static_cast<ParamsDialogData*>(userData);
    appendParamRow(data, {}, {}, true);
}

void paramsDialogResponse(GtkDialog* dialog, int response, gpointer userData) {
    auto* data = static_cast<ParamsDialogData*>(userData);
    if (response == GTK_RESPONSE_APPLY || response == GTK_RESPONSE_ACCEPT) {
        try {
            data->ui->settings.params = collectParamRows(*data);
            if (data->ui->player != nullptr) {
                data->ui->player->setExternalParams(data->ui->settings.params);
            }
            saveSettingsQuietly(*data->ui);
            updateControls(*data->ui);
            setStatus(*data->ui, "External parameters updated; reload the movie for startup-only parameters");
        } catch (const std::exception& error) {
            setParamsDialogError(*data, error.what());
            return;
        }
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
    delete data;
}

void paramsClicked(GtkButton*, gpointer userData) {
    auto& ui = *static_cast<PlayerUi*>(userData);
    GtkWidget* dialog = gtk_dialog_new_with_buttons("External Parameters",
                                                    GTK_WINDOW(ui.window),
                                                    GTK_DIALOG_MODAL,
                                                    "_Cancel",
                                                    GTK_RESPONSE_CANCEL,
                                                    "_Apply",
                                                    GTK_RESPONSE_APPLY,
                                                    nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 760, 420);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_box_append(GTK_BOX(content), box);

    GtkWidget* label = gtk_label_new("External parameters");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* keyHeader = gtk_label_new("Name");
    gtk_label_set_xalign(GTK_LABEL(keyHeader), 0.0F);
    gtk_widget_set_size_request(keyHeader, 180, -1);
    gtk_box_append(GTK_BOX(header), keyHeader);

    GtkWidget* valueHeader = gtk_label_new("Value");
    gtk_label_set_xalign(GTK_LABEL(valueHeader), 0.0F);
    gtk_widget_set_hexpand(valueHeader, TRUE);
    gtk_box_append(GTK_BOX(header), valueHeader);

    GtkWidget* spacer = gtk_label_new("");
    gtk_widget_set_size_request(spacer, 38, -1);
    gtk_box_append(GTK_BOX(header), spacer);
    gtk_box_append(GTK_BOX(box), header);

    GtkWidget* scroller = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroller, TRUE);
    GtkWidget* rowsBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(rowsBox, 2);
    gtk_widget_set_margin_bottom(rowsBox, 2);
    gtk_widget_set_margin_start(rowsBox, 2);
    gtk_widget_set_margin_end(rowsBox, 2);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), rowsBox);
    gtk_box_append(GTK_BOX(box), scroller);

    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* addButton = gtk_button_new_with_label("Add Parameter");
    gtk_box_append(GTK_BOX(actions), addButton);

    GtkWidget* errorLabel = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(errorLabel), 0.0F);
    gtk_widget_add_css_class(errorLabel, "error");
    gtk_widget_set_hexpand(errorLabel, TRUE);
    gtk_box_append(GTK_BOX(actions), errorLabel);
    gtk_box_append(GTK_BOX(box), actions);

    auto* data = new ParamsDialogData{&ui, rowsBox, errorLabel};
    for (const auto& [key, value] : ui.settings.params) {
        appendParamRow(*data, key, value);
    }
    if (data->rows.empty()) {
        appendParamRow(*data);
    }

    g_signal_connect(addButton, "clicked", G_CALLBACK(addParamClicked), data);
    g_signal_connect(dialog, "response", G_CALLBACK(paramsDialogResponse), data);
    gtk_window_present(GTK_WINDOW(dialog));
}

void installCss() {
    GtkCssProvider* provider = gtk_css_provider_new();
    constexpr const char* css =
        ".stage-view { background: #141414; }"
        ".control-bar { padding: 7px 8px; border-top: 1px solid alpha(currentColor, 0.12); }"
        ".status-line { padding: 0 8px 7px 8px; color: alpha(currentColor, 0.72); }"
        ".subtitle { color: alpha(currentColor, 0.68); font-size: 0.86em; }";
    gtk_css_provider_load_from_data(provider, css, -1);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

void buildUi(PlayerUi& ui) {
    installCss();

    ui.window = gtk_application_window_new(ui.app);
    gtk_window_set_default_size(GTK_WINDOW(ui.window), 980, 720);

    GtkWidget* header = gtk_header_bar_new();
    GtkWidget* openButton = iconButton("document-open-symbolic", "Open movie");
    ui.reloadButton = iconButton("view-refresh-symbolic", "Reload movie");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), openButton);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), ui.reloadButton);

    GtkWidget* titleBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    ui.titleLabel = gtk_label_new("LibreShockwave Player");
    ui.subtitleLabel = gtk_label_new("Open a Director or Shockwave movie");
    gtk_widget_add_css_class(ui.subtitleLabel, "subtitle");
    gtk_label_set_ellipsize(GTK_LABEL(ui.subtitleLabel), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_append(GTK_BOX(titleBox), ui.titleLabel);
    gtk_box_append(GTK_BOX(titleBox), ui.subtitleLabel);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), titleBox);
    gtk_window_set_titlebar(GTK_WINDOW(ui.window), header);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(ui.window), root);

    ui.stageArea = gtk_drawing_area_new();
    gtk_widget_add_css_class(ui.stageArea, "stage-view");
    gtk_widget_set_hexpand(ui.stageArea, TRUE);
    gtk_widget_set_vexpand(ui.stageArea, TRUE);
    gtk_widget_set_focusable(ui.stageArea, TRUE);
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(ui.stageArea), 640);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(ui.stageArea), 480);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(ui.stageArea), drawStage, &ui, nullptr);

    GtkEventController* motionController = gtk_event_controller_motion_new();
    g_signal_connect(motionController, "motion", G_CALLBACK(stageMotion), &ui);
    g_signal_connect(motionController, "leave", G_CALLBACK(stagePointerLeave), &ui);
    gtk_widget_add_controller(ui.stageArea, motionController);

    GtkGesture* clickGesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(clickGesture), 0);
    g_signal_connect(clickGesture, "pressed", G_CALLBACK(stagePressed), &ui);
    g_signal_connect(clickGesture, "released", G_CALLBACK(stageReleased), &ui);
    gtk_widget_add_controller(ui.stageArea, GTK_EVENT_CONTROLLER(clickGesture));

    GtkEventController* keyController = gtk_event_controller_key_new();
    g_signal_connect(keyController, "key-pressed", G_CALLBACK(stageKeyPressed), &ui);
    g_signal_connect(keyController, "key-released", G_CALLBACK(stageKeyReleased), &ui);
    gtk_widget_add_controller(ui.stageArea, keyController);

    GtkEventController* focusController = gtk_event_controller_focus_new();
    g_signal_connect(focusController, "leave", G_CALLBACK(stageFocusLeave), &ui);
    gtk_widget_add_controller(ui.stageArea, focusController);

    gtk_box_append(GTK_BOX(root), ui.stageArea);

    GtkWidget* controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
    gtk_widget_add_css_class(controls, "control-bar");
    gtk_box_append(GTK_BOX(root), controls);

    ui.stopButton = iconButton("media-playback-stop-symbolic", "Stop");
    ui.playButton = iconButton("media-playback-start-symbolic", "Play");
    ui.stepButton = iconButton("media-skip-forward-symbolic", "Step frame");
    gtk_box_append(GTK_BOX(controls), ui.stopButton);
    gtk_box_append(GTK_BOX(controls), ui.playButton);
    gtk_box_append(GTK_BOX(controls), ui.stepButton);

    ui.frameLabel = gtk_label_new("No movie");
    gtk_label_set_xalign(GTK_LABEL(ui.frameLabel), 0.0F);
    gtk_widget_set_hexpand(ui.frameLabel, TRUE);
    gtk_box_append(GTK_BOX(controls), ui.frameLabel);

    ui.tempoOverrideCheck = gtk_check_button_new_with_label("Override tempo");
    gtk_box_append(GTK_BOX(controls), ui.tempoOverrideCheck);
    ui.tempoSpin = gtk_spin_button_new_with_range(1.0, 240.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui.tempoSpin), 15.0);
    gtk_widget_set_size_request(ui.tempoSpin, 92, -1);
    gtk_box_append(GTK_BOX(controls), ui.tempoSpin);

    ui.paramsButton = iconButton("preferences-system-symbolic", "External parameters");
    gtk_box_append(GTK_BOX(controls), ui.paramsButton);

    ui.statusLabel = gtk_label_new("Ready");
    gtk_widget_add_css_class(ui.statusLabel, "status-line");
    gtk_label_set_xalign(GTK_LABEL(ui.statusLabel), 0.0F);
    gtk_label_set_ellipsize(GTK_LABEL(ui.statusLabel), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_append(GTK_BOX(root), ui.statusLabel);

    g_signal_connect(openButton, "clicked", G_CALLBACK(openMovieClicked), &ui);
    g_signal_connect(ui.reloadButton, "clicked", G_CALLBACK(reloadClicked), &ui);
    g_signal_connect(ui.playButton, "clicked", G_CALLBACK(playClicked), &ui);
    g_signal_connect(ui.stopButton, "clicked", G_CALLBACK(stopClicked), &ui);
    g_signal_connect(ui.stepButton, "clicked", G_CALLBACK(stepClicked), &ui);
    g_signal_connect(ui.tempoOverrideCheck, "toggled", G_CALLBACK(tempoOverrideToggled), &ui);
    g_signal_connect(ui.tempoSpin, "value-changed", G_CALLBACK(tempoChanged), &ui);
    g_signal_connect(ui.paramsButton, "clicked", G_CALLBACK(paramsClicked), &ui);

    updateControls(ui);
    updateWindowLabels(ui);
}

gboolean loadInitialMovie(gpointer userData) {
    auto& ui = *static_cast<PlayerUi*>(userData);
    if (!ui.settings.movieSource.empty()) {
        loadMovie(ui, ui.settings.movieSource, false);
    }
    return G_SOURCE_REMOVE;
}

void appActivate(GtkApplication* app, gpointer) {
    auto* ui = static_cast<PlayerUi*>(g_object_get_data(G_OBJECT(app), "player-ui"));
    ui->app = app;
    buildUi(*ui);
    gtk_window_present(GTK_WINDOW(ui->window));
    g_idle_add(loadInitialMovie, ui);
}

void destroyPlayerUi(gpointer data) {
    auto* ui = static_cast<PlayerUi*>(data);
    shutdownMovie(*ui);
    delete ui;
}

std::string usage(const char* argv0, const fs::path& settingsPath) {
    std::ostringstream out;
    out << "Usage: " << argv0 << " [options] [movie.dcr]\n\n"
        << "Starts the GTK LibreShockwave movie player. Loaded movies autoplay by default;\n"
        << "the player remembers the last movie, external parameters, and tempo override.\n\n"
        << "Options:\n"
        << "  --movie SOURCE            Local path or HTTP(S) movie URL to load. A positional source is also accepted.\n"
        << "  --param KEY=VALUE         Set or replace an external parameter. Repeatable.\n"
        << "  --clear-params            Remove remembered external parameters.\n"
        << "  --tempo FPS               Override playback cadence in frames per second.\n"
        << "  --use-movie-tempo         Clear the remembered tempo override.\n"
        << "  --script-timeout-ms N     Per-tick Lingo script deadline, or 0 to disable.\n"
        << "  --preload-casts           Run an explicit local external-cast preload pass before playback.\n"
        << "  --no-preload-casts        Skip the explicit pre-play external-cast preload pass.\n"
        << "  --settings PATH           Settings file to read and update.\n"
        << "  --help                    Show this help.\n\n"
        << "Default settings file: " << pathString(settingsPath) << '\n';
    return out.str();
}

fs::path findSettingsPath(int argc, char** argv) {
    fs::path settingsPath = defaultSettingsPath();
    for (int index = 1; index < argc; ++index) {
        const std::string_view arg(argv[index]);
        if (arg == "--settings") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--settings requires a path");
            }
            settingsPath = argv[++index];
            continue;
        }
        constexpr std::string_view prefix = "--settings=";
        if (startsWith(arg, prefix)) {
            settingsPath = std::string(arg.substr(prefix.size()));
        }
    }
    return absolutePath(settingsPath);
}

void applyCommandLine(PlayerSettings& settings, int argc, char** argv) {
    bool positionalMovieSeen = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view arg(argv[index]);
        if (arg == "--help" || arg == "-h") {
            continue;
        }
        if (arg == "--settings") {
            ++index;
            continue;
        }
        if (startsWith(arg, "--settings=")) {
            continue;
        }
        if (arg == "--movie") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--movie requires a path");
            }
            settings.movieSource = normalizeMovieSource(argv[++index]);
            continue;
        }
        constexpr std::string_view moviePrefix = "--movie=";
        if (startsWith(arg, moviePrefix)) {
            settings.movieSource = normalizeMovieSource(arg.substr(moviePrefix.size()));
            continue;
        }
        if (arg == "--param") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--param requires key=value");
            }
            setParamAssignment(settings, argv[++index], "--param");
            continue;
        }
        constexpr std::string_view paramPrefix = "--param=";
        if (startsWith(arg, paramPrefix)) {
            setParamAssignment(settings, arg.substr(paramPrefix.size()), "--param");
            continue;
        }
        if (arg == "--clear-params") {
            settings.params.clear();
            continue;
        }
        if (arg == "--tempo") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--tempo requires a positive integer");
            }
            settings.tempoOverride = parseInt(argv[++index], "--tempo", 1);
            continue;
        }
        constexpr std::string_view tempoPrefix = "--tempo=";
        if (startsWith(arg, tempoPrefix)) {
            settings.tempoOverride = parseInt(arg.substr(tempoPrefix.size()), "--tempo", 1);
            continue;
        }
        if (arg == "--use-movie-tempo") {
            settings.tempoOverride.reset();
            continue;
        }
        if (arg == "--script-timeout-ms") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--script-timeout-ms requires a non-negative integer");
            }
            settings.scriptTimeoutMs = parseInt(argv[++index], "--script-timeout-ms", 0);
            continue;
        }
        constexpr std::string_view scriptTimeoutPrefix = "--script-timeout-ms=";
        if (startsWith(arg, scriptTimeoutPrefix)) {
            settings.scriptTimeoutMs = parseInt(arg.substr(scriptTimeoutPrefix.size()), "--script-timeout-ms", 0);
            continue;
        }
        if (arg == "--preload-casts") {
            settings.preloadCasts = true;
            continue;
        }
        if (arg == "--no-preload-casts") {
            settings.preloadCasts = false;
            continue;
        }
        if (!arg.empty() && arg.front() == '-') {
            throw std::runtime_error("Unknown option: " + std::string(arg));
        }
        if (positionalMovieSeen) {
            throw std::runtime_error("Only one positional movie path is supported");
        }
        settings.movieSource = normalizeMovieSource(arg);
        positionalMovieSeen = true;
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const CURLcode curlInit = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (curlInit != CURLE_OK) {
            throw std::runtime_error(std::string("Unable to initialize libcurl: ") +
                                     curl_easy_strerror(curlInit));
        }
        struct CurlGlobalCleanup {
            ~CurlGlobalCleanup() {
                curl_global_cleanup();
            }
        } curlGlobalCleanup;

        const fs::path settingsPath = findSettingsPath(argc, argv);
        for (int index = 1; index < argc; ++index) {
            const std::string_view arg(argv[index]);
            if (arg == "--help" || arg == "-h") {
                std::cout << usage(argv[0], settingsPath);
                return 0;
            }
        }

        auto* ui = new PlayerUi;
        ui->settingsPath = settingsPath;
        ui->settings = loadSettings(settingsPath);
        applyCommandLine(ui->settings, argc, argv);

        GtkApplication* app = gtk_application_new("net.libreshockwave.Player", G_APPLICATION_NON_UNIQUE);
        g_object_set_data_full(G_OBJECT(app), "player-ui", ui, destroyPlayerUi);
        g_signal_connect(app, "activate", G_CALLBACK(appActivate), nullptr);

        char* gtkArgv[] = {argv[0], nullptr};
        const int status = g_application_run(G_APPLICATION(app), 1, gtkArgv);
        g_object_unref(app);
        return status;
    } catch (const std::exception& error) {
        std::cerr << "libreshockwave_player: " << error.what() << '\n';
        return 1;
    }
}
