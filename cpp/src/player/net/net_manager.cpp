#include "libreshockwave/player/net/net_manager.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

#include "libreshockwave/util/file_util.hpp"

namespace libreshockwave::player::net {

namespace {

std::string toLower(std::string_view value) {
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool isHttpUrl(std::string_view value) {
    return startsWith(value, "http://") || startsWith(value, "https://");
}

bool isLocalHttpUrl(std::string_view value) {
    return startsWith(value, "http://localhost") || startsWith(value, "http://127.0.0.1");
}

bool isExternalCastUrl(std::string_view url) {
    const auto path_end = url.find_first_of("?#");
    const auto path = url.substr(0, path_end);
    const auto file_name = util::getFileName(path);
    std::string lower_file_name(file_name);
    std::transform(lower_file_name.begin(), lower_file_name.end(), lower_file_name.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lower_file_name.ends_with(".cct") || lower_file_name.ends_with(".cst");
}

std::string httpUrlAgainstBase(std::string_view url, std::string_view base_path) {
    if (isHttpUrl(url) || !isHttpUrl(base_path)) {
        return std::string(url);
    }
    if (startsWith(url, "/")) {
        const auto origin = NetManager::extractOrigin(base_path);
        return origin.empty() ? std::string(url) : origin + std::string(url);
    }

    std::string base(base_path);
    if (const auto query = base.find('?'); query != std::string::npos) {
        base = base.substr(0, query);
    }
    const auto slash = base.find_last_of('/');
    if (slash == std::string::npos) {
        return std::string(url);
    }
    if (!base.ends_with('/')) {
        base = base.substr(0, slash + 1);
    }
    return base + std::string(url);
}

void putProp(lingo::Datum& prop_list, std::string key, lingo::Datum value) {
    prop_list.propListValue().put(lingo::Datum::of(std::move(key)), std::move(value));
}

std::string extensionLower(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext;
}

std::optional<std::filesystem::path> resolvePathWithFallbacks(const std::filesystem::path& path) {
    const auto parent = path.parent_path();
    const auto stem = path.stem().string();
    const auto file_name = path.filename().string();
    const auto ext = extensionLower(path);
    const auto sibling = [&](std::string_view suffix) {
        return parent.empty() ? std::filesystem::path(stem + std::string(suffix))
                              : parent / (stem + std::string(suffix));
    };

    if (ext == ".cst" || ext == ".cct") {
        for (const auto& candidate : {sibling(".cct"), sibling(".cst")}) {
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }
        }
    } else if (ext == ".dcr" || ext == ".dxr" || ext == ".dir") {
        for (const auto& candidate : {sibling(".dir"), sibling(".dcr"), sibling(".dxr")}) {
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }
        }
    } else if (file_name.find('.') == std::string::npos) {
        const auto extensionless_sibling = [&](std::string_view suffix) {
            return parent.empty() ? std::filesystem::path(file_name + std::string(suffix))
                                  : parent / (file_name + std::string(suffix));
        };
        for (const auto& candidate : {extensionless_sibling(".cct"), extensionless_sibling(".cst")}) {
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }
        }
    }

    if (std::filesystem::exists(path)) {
        return path;
    }

    return std::nullopt;
}

std::optional<std::vector<std::uint8_t>> readFileWithFallbacks(const std::filesystem::path& path) {
    const auto resolved = resolvePathWithFallbacks(path);
    if (!resolved.has_value() || !std::filesystem::is_regular_file(*resolved)) {
        return std::nullopt;
    }

    std::ifstream input(*resolved, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::optional<std::vector<std::uint8_t>> readLocalFetchFile(const std::filesystem::path& base,
                                                            std::string_view url) {
    for (const auto& candidate : util::getLocalFetchPathCandidates(base, url)) {
        if (auto data = readFileWithFallbacks(candidate)) {
            return data;
        }
    }
    return std::nullopt;
}

} // namespace

NetManager::LoadResult NetManager::LoadResult::success(std::vector<std::uint8_t> data) {
    return LoadResult{std::move(data), 0, ""};
}

NetManager::LoadResult NetManager::LoadResult::failure(int error_code, std::string error_message) {
    return LoadResult{std::nullopt, error_code, std::move(error_message)};
}

void NetManager::setBasePath(std::string base_path) {
    base_path_ = std::move(base_path);
}

const std::string& NetManager::basePath() const {
    return base_path_;
}

const std::string& NetManager::getBasePath() const {
    return base_path_;
}

void NetManager::setLocalHttpRoot(std::string root) {
    local_http_root_ = std::move(root);
}

const std::string& NetManager::localHttpRoot() const {
    return local_http_root_;
}

void NetManager::setRemoteFetchEnabled(bool enabled) {
    remote_fetch_enabled_ = enabled;
}

bool NetManager::remoteFetchEnabled() const {
    return remote_fetch_enabled_;
}

void NetManager::setFetchHandler(FetchHandler handler) {
    fetch_handler_ = std::move(handler);
}

void NetManager::setCompletionCallback(CompletionCallback callback) {
    completion_callback_ = std::move(callback);
}

void NetManager::setTaskCallback(TaskCallback callback) {
    task_callback_ = std::move(callback);
}

int NetManager::preloadNetThing(std::string url) {
    auto& task = createGetTask(std::move(url));
    const int task_id = task.taskId();

    const auto cached = getCachedData(task.originalUrl());
    if (cached.has_value()) {
        task.markInProgress();
        task.complete(*cached);
        notifyCompletion(task.originalUrl(), *cached);
        notifyTask(task);
        return task_id;
    }

    executeTask(task, true);
    return task_id;
}

int NetManager::postNetText(std::string url, std::string post_data) {
    auto& task = createPostTask(std::move(url), std::move(post_data));
    const int task_id = task.taskId();
    executeTask(task, true);
    return task_id;
}

bool NetManager::hasPendingCastLoads() const {
    return std::any_of(tasks_.begin(), tasks_.end(), [](const auto& entry) {
        const auto& task = entry.second;
        return task.method() == NetTaskMethod::Get && !task.isDone() &&
               isExternalCastUrl(task.originalUrl());
    });
}

bool NetManager::netDone(std::optional<int> task_id) const {
    const auto* task = getTask(task_id);
    return task != nullptr && task->isDone();
}

std::string NetManager::netTextResult(std::optional<int> task_id) const {
    const auto* task = getTask(task_id);
    if (task != nullptr && task->state() == NetTaskState::Completed) {
        return task->resultAsString();
    }
    return "";
}

int NetManager::netError(std::optional<int> task_id) const {
    const auto* task = getTask(task_id);
    return task == nullptr ? 0 : task->errorCode();
}

std::string_view NetManager::getStreamStatus(std::optional<int> task_id) const {
    const auto* task = getTask(task_id);
    return task == nullptr ? std::string_view("Error") : task->streamStatus();
}

lingo::Datum NetManager::getStreamStatusDatum(std::optional<int> task_id) const {
    const auto* task = getTask(task_id);
    auto props = lingo::Datum::propList();

    if (task == nullptr) {
        putProp(props, "URL", lingo::Datum::of(std::string()));
        putProp(props, "state", lingo::Datum::of(std::string("Error")));
        putProp(props, "bytesSoFar", lingo::Datum::of(0));
        putProp(props, "bytesTotal", lingo::Datum::of(0));
        putProp(props, "error", lingo::Datum::of(std::string("OK")));
        return props;
    }

    const int bytes_so_far = task->result().has_value() ? static_cast<int>(task->result()->size()) : 0;
    putProp(props, "URL", lingo::Datum::of(task->originalUrl()));
    putProp(props, "state", lingo::Datum::of(std::string(task->streamStatus())));
    putProp(props, "bytesSoFar", lingo::Datum::of(bytes_so_far));
    putProp(props, "bytesTotal", lingo::Datum::of(bytes_so_far));
    putProp(props, "error", task->errorCode() == 0
        ? lingo::Datum::of(std::string("OK"))
        : lingo::Datum::of(std::to_string(task->errorCode())));
    return props;
}

lingo::Datum NetManager::getStreamStatusDatum(std::string_view url) const {
    if (url.empty()) {
        return getStreamStatusDatum(std::nullopt);
    }

    const std::string url_value(url);
    const auto resolved_url = resolveUrl(url);
    const auto file_name = util::getFileName(url);

    const NetTask* best_match = nullptr;
    for (const auto& [id, task] : tasks_) {
        (void)id;
        const bool matches = url_value == task.originalUrl() ||
                             resolved_url == task.url() ||
                             (!file_name.empty() && file_name == util::getFileName(task.originalUrl()));
        if (!matches) {
            continue;
        }
        if (best_match == nullptr || task.taskId() > best_match->taskId()) {
            best_match = &task;
        }
    }

    return best_match == nullptr ? getStreamStatusDatum(std::nullopt)
                                : getStreamStatusDatum(best_match->taskId());
}

std::optional<std::vector<std::uint8_t>> NetManager::getNetBytes(std::optional<int> task_id) const {
    const auto* task = getTask(task_id);
    if (task != nullptr && task->state() == NetTaskState::Completed) {
        return task->result();
    }
    return std::nullopt;
}

NetTask* NetManager::getTask(std::optional<int> task_id) {
    if (!task_id.has_value() || *task_id == 0) {
        if (last_task_id_ <= 0) {
            return nullptr;
        }
        task_id = last_task_id_;
    }

    auto found = tasks_.find(*task_id);
    return found == tasks_.end() ? nullptr : &found->second;
}

const NetTask* NetManager::getTask(std::optional<int> task_id) const {
    if (!task_id.has_value() || *task_id == 0) {
        if (last_task_id_ <= 0) {
            return nullptr;
        }
        task_id = last_task_id_;
    }

    const auto found = tasks_.find(*task_id);
    return found == tasks_.end() ? nullptr : &found->second;
}

const std::unordered_map<int, NetTask>& NetManager::tasks() const {
    return tasks_;
}

void NetManager::cacheData(std::string url, std::vector<std::uint8_t> data) {
    url_cache_[cacheKeyForUrl(url)] = std::move(data);
}

std::optional<std::vector<std::uint8_t>> NetManager::getCachedData(std::string_view url) const {
    const auto cache_key = cacheKeyForUrl(url);
    if (const auto found = url_cache_.find(cache_key); found != url_cache_.end()) {
        return found->second;
    }

    const auto lower = toLower(cache_key);
    const auto base_name = util::getFileNameWithoutExtension(cache_key);
    if (lower.ends_with(".cct")) {
        if (const auto found = url_cache_.find(base_name + ".cst"); found != url_cache_.end()) {
            return found->second;
        }
        if (const auto found = url_cache_.find(base_name); found != url_cache_.end()) {
            return found->second;
        }
    } else if (lower.ends_with(".cst")) {
        if (const auto found = url_cache_.find(base_name + ".cct"); found != url_cache_.end()) {
            return found->second;
        }
        if (const auto found = url_cache_.find(base_name); found != url_cache_.end()) {
            return found->second;
        }
    } else {
        if (const auto found = url_cache_.find(base_name + ".cct"); found != url_cache_.end()) {
            return found->second;
        }
        if (const auto found = url_cache_.find(base_name + ".cst"); found != url_cache_.end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

const std::unordered_map<std::string, std::vector<std::uint8_t>>& NetManager::urlCache() const {
    return url_cache_;
}

void NetManager::clear() {
    tasks_.clear();
    url_cache_.clear();
    next_task_id_ = 1;
    last_task_id_ = 0;
}

void NetManager::shutdown() {
    fetch_handler_ = nullptr;
}

std::string NetManager::resolveUrl(std::string_view url) {
    if (url.empty()) {
        return "";
    }

    auto clean_url = std::string(url);
    if (const auto query_index = clean_url.find('?'); query_index != std::string::npos) {
        clean_url = clean_url.substr(0, query_index);
    }

    if (startsWith(clean_url, "http://") || startsWith(clean_url, "https://")) {
        const auto last_slash = clean_url.find_last_of('/');
        if (last_slash != std::string::npos && last_slash < clean_url.size() - 1) {
            return clean_url.substr(last_slash + 1);
        }
        return clean_url;
    }

    const auto separator = clean_url.find_last_of("\\/");
    if (separator != std::string::npos && separator < clean_url.size() - 1) {
        return clean_url.substr(separator + 1);
    }
    return clean_url;
}

std::string NetManager::cacheKeyForUrl(std::string_view url) {
    return util::getFileName(url);
}

std::string NetManager::extractUrlPath(std::string_view url) {
    if (url.empty()) {
        return "";
    }

    auto clean_url = std::string(url);
    if (const auto query_index = clean_url.find('?'); query_index != std::string::npos) {
        clean_url = clean_url.substr(0, query_index);
    }
    const auto scheme_end = clean_url.find("://");
    if (scheme_end == std::string::npos) {
        return "";
    }
    const auto path_start = clean_url.find('/', scheme_end + 3);
    if (path_start == std::string::npos) {
        return "/";
    }
    return clean_url.substr(path_start);
}

std::string NetManager::extractOrigin(std::string_view url) {
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) {
        return "";
    }
    const auto path_start = url.find('/', scheme_end + 3);
    return path_start == std::string_view::npos ? std::string(url) : std::string(url.substr(0, path_start));
}

NetTask& NetManager::createGetTask(std::string url) {
    const int task_id = next_task_id_++;
    last_task_id_ = task_id;
    auto task = NetTask::get(task_id, url, resolveUrl(url));
    auto [inserted, ok] = tasks_.emplace(task_id, std::move(task));
    (void)ok;
    return inserted->second;
}

NetTask& NetManager::createPostTask(std::string url, std::string post_data) {
    const int task_id = next_task_id_++;
    last_task_id_ = task_id;
    auto task = NetTask::post(task_id, url, resolveUrl(url), std::move(post_data));
    auto [inserted, ok] = tasks_.emplace(task_id, std::move(task));
    (void)ok;
    return inserted->second;
}

void NetManager::executeTask(NetTask& task, bool use_cache) {
    task.markInProgress();

    if (fetch_handler_ == nullptr) {
        if (task.method() == NetTaskMethod::Get) {
            std::optional<std::vector<std::uint8_t>> local_data;
            std::string cache_url = task.originalUrl();

            if (!local_http_root_.empty()) {
                std::string http_url = httpUrlAgainstBase(task.originalUrl(), base_path_);
                if (isLocalHttpUrl(http_url)) {
                    cache_url = http_url;
                    auto url_path = extractUrlPath(http_url);
                    if (!url_path.empty()) {
                        if (url_path.front() == '/') {
                            url_path.erase(url_path.begin());
                        }
                        local_data = readFileWithFallbacks(std::filesystem::path(local_http_root_) / url_path);
                    }
                }
            }

            if (!local_data.has_value() && !base_path_.empty() && !isHttpUrl(base_path_)) {
                local_data = readLocalFetchFile(std::filesystem::path(base_path_), task.originalUrl());
            }

            if (local_data.has_value()) {
                completeTask(task, std::move(*local_data), use_cache, cache_url);
                return;
            }
        }
        const auto request_url = task.method() == NetTaskMethod::Get
            ? httpUrlAgainstBase(task.originalUrl(), base_path_)
            : task.originalUrl();
        if (!remote_fetch_enabled_ && isHttpUrl(request_url)) {
            task.fail(403, "Remote network access disabled");
            notifyTask(task);
            return;
        }
        task.fail(404, "No fetch handler configured");
        notifyTask(task);
        return;
    }

    const NetTask* request_task = &task;
    std::optional<NetTask> normalized_request_task;
    if (task.method() == NetTaskMethod::Get) {
        const auto request_url = httpUrlAgainstBase(task.originalUrl(), base_path_);
        if (request_url != task.originalUrl()) {
            normalized_request_task.emplace(task.taskId(),
                                          request_url,
                                          resolveUrl(request_url),
                                          task.method(),
                                          task.postData());
            request_task = &*normalized_request_task;
        }
    }

    const auto effective_request_url = task.method() == NetTaskMethod::Get
        ? request_task->originalUrl()
        : httpUrlAgainstBase(task.originalUrl(), base_path_);
    if (!remote_fetch_enabled_ && isHttpUrl(effective_request_url)) {
        task.fail(403, "Remote network access disabled");
        notifyTask(task);
        return;
    }

    auto result = fetch_handler_(*request_task);
    if (!result.data.has_value()) {
        task.fail(result.error_code != 0 ? result.error_code : 404,
                  result.error_message.empty() ? "Load returned no data" : result.error_message);
        notifyTask(task);
        return;
    }

    completeTask(task, std::move(*result.data), use_cache, request_task->originalUrl());
}

void NetManager::completeTask(NetTask& task,
                              std::vector<std::uint8_t> data,
                              bool cache_result,
                              std::string_view cache_url) {
    if (cache_result) {
        url_cache_[cacheKeyForUrl(cache_url.empty() ? task.originalUrl() : cache_url)] = data;
    }
    task.complete(std::move(data));
    if (task.result().has_value()) {
        notifyCompletion(task.originalUrl(), *task.result());
    }
    notifyTask(task);
}

void NetManager::notifyCompletion(const std::string& url, const std::vector<std::uint8_t>& data) const {
    if (completion_callback_) {
        completion_callback_(url, data);
    }
}

void NetManager::notifyTask(const NetTask& task) const {
    if (task_callback_) {
        task_callback_(task);
    }
}

} // namespace libreshockwave::player::net
