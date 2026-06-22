#include "libreshockwave/player/net/NetManager.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

#include "libreshockwave/util/FileUtil.hpp"

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

std::string httpUrlAgainstBase(std::string_view url, std::string_view basePath) {
    if (isHttpUrl(url) || !isHttpUrl(basePath)) {
        return std::string(url);
    }
    if (startsWith(url, "/")) {
        const auto origin = NetManager::extractOrigin(basePath);
        return origin.empty() ? std::string(url) : origin + std::string(url);
    }

    std::string base(basePath);
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

void putProp(lingo::Datum& propList, std::string key, lingo::Datum value) {
    propList.propListValue().put(lingo::Datum::of(std::move(key)), std::move(value));
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
    const auto fileName = path.filename().string();
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
    } else if (fileName.find('.') == std::string::npos) {
        const auto extensionlessSibling = [&](std::string_view suffix) {
            return parent.empty() ? std::filesystem::path(fileName + std::string(suffix))
                                  : parent / (fileName + std::string(suffix));
        };
        for (const auto& candidate : {extensionlessSibling(".cct"), extensionlessSibling(".cst")}) {
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

NetManager::LoadResult NetManager::LoadResult::failure(int errorCode, std::string errorMessage) {
    return LoadResult{std::nullopt, errorCode, std::move(errorMessage)};
}

void NetManager::setBasePath(std::string basePath) {
    basePath_ = std::move(basePath);
}

const std::string& NetManager::basePath() const {
    return basePath_;
}

const std::string& NetManager::getBasePath() const {
    return basePath_;
}

void NetManager::setLocalHttpRoot(std::string root) {
    localHttpRoot_ = std::move(root);
}

const std::string& NetManager::localHttpRoot() const {
    return localHttpRoot_;
}

void NetManager::setRemoteFetchEnabled(bool enabled) {
    remoteFetchEnabled_ = enabled;
}

bool NetManager::remoteFetchEnabled() const {
    return remoteFetchEnabled_;
}

void NetManager::setFetchHandler(FetchHandler handler) {
    fetchHandler_ = std::move(handler);
}

void NetManager::setCompletionCallback(CompletionCallback callback) {
    completionCallback_ = std::move(callback);
}

void NetManager::setTaskCallback(TaskCallback callback) {
    taskCallback_ = std::move(callback);
}

int NetManager::preloadNetThing(std::string url) {
    auto& task = createGetTask(std::move(url));
    const int taskId = task.taskId();

    const auto cached = getCachedData(task.originalUrl());
    if (cached.has_value()) {
        task.markInProgress();
        task.complete(*cached);
        notifyCompletion(task.originalUrl(), *cached);
        notifyTask(task);
        return taskId;
    }

    executeTask(task, true);
    return taskId;
}

int NetManager::postNetText(std::string url, std::string postData) {
    auto& task = createPostTask(std::move(url), std::move(postData));
    const int taskId = task.taskId();
    executeTask(task, true);
    return taskId;
}

bool NetManager::netDone(std::optional<int> taskId) const {
    const auto* task = getTask(taskId);
    return task != nullptr && task->isDone();
}

std::string NetManager::netTextResult(std::optional<int> taskId) const {
    const auto* task = getTask(taskId);
    if (task != nullptr && task->state() == NetTaskState::Completed) {
        return task->resultAsString();
    }
    return "";
}

int NetManager::netError(std::optional<int> taskId) const {
    const auto* task = getTask(taskId);
    return task == nullptr ? 0 : task->errorCode();
}

std::string_view NetManager::getStreamStatus(std::optional<int> taskId) const {
    const auto* task = getTask(taskId);
    return task == nullptr ? std::string_view("Error") : task->streamStatus();
}

lingo::Datum NetManager::getStreamStatusDatum(std::optional<int> taskId) const {
    const auto* task = getTask(taskId);
    auto props = lingo::Datum::propList();

    if (task == nullptr) {
        putProp(props, "URL", lingo::Datum::of(std::string()));
        putProp(props, "state", lingo::Datum::of(std::string("Error")));
        putProp(props, "bytesSoFar", lingo::Datum::of(0));
        putProp(props, "bytesTotal", lingo::Datum::of(0));
        putProp(props, "error", lingo::Datum::of(std::string("OK")));
        return props;
    }

    const int bytesSoFar = task->result().has_value() ? static_cast<int>(task->result()->size()) : 0;
    putProp(props, "URL", lingo::Datum::of(task->originalUrl()));
    putProp(props, "state", lingo::Datum::of(std::string(task->streamStatus())));
    putProp(props, "bytesSoFar", lingo::Datum::of(bytesSoFar));
    putProp(props, "bytesTotal", lingo::Datum::of(bytesSoFar));
    putProp(props, "error", task->errorCode() == 0
        ? lingo::Datum::of(std::string("OK"))
        : lingo::Datum::of(std::to_string(task->errorCode())));
    return props;
}

lingo::Datum NetManager::getStreamStatusDatum(std::string_view url) const {
    if (url.empty()) {
        return getStreamStatusDatum(std::nullopt);
    }

    const std::string urlValue(url);
    const auto resolvedUrl = resolveUrl(url);
    const auto fileName = util::getFileName(url);

    const NetTask* bestMatch = nullptr;
    for (const auto& [id, task] : tasks_) {
        (void)id;
        const bool matches = urlValue == task.originalUrl() ||
                             resolvedUrl == task.url() ||
                             (!fileName.empty() && fileName == util::getFileName(task.originalUrl()));
        if (!matches) {
            continue;
        }
        if (bestMatch == nullptr || task.taskId() > bestMatch->taskId()) {
            bestMatch = &task;
        }
    }

    return bestMatch == nullptr ? getStreamStatusDatum(std::nullopt)
                                : getStreamStatusDatum(bestMatch->taskId());
}

std::optional<std::vector<std::uint8_t>> NetManager::getNetBytes(std::optional<int> taskId) const {
    const auto* task = getTask(taskId);
    if (task != nullptr && task->state() == NetTaskState::Completed) {
        return task->result();
    }
    return std::nullopt;
}

NetTask* NetManager::getTask(std::optional<int> taskId) {
    if (!taskId.has_value() || *taskId == 0) {
        if (lastTaskId_ <= 0) {
            return nullptr;
        }
        taskId = lastTaskId_;
    }

    auto found = tasks_.find(*taskId);
    return found == tasks_.end() ? nullptr : &found->second;
}

const NetTask* NetManager::getTask(std::optional<int> taskId) const {
    if (!taskId.has_value() || *taskId == 0) {
        if (lastTaskId_ <= 0) {
            return nullptr;
        }
        taskId = lastTaskId_;
    }

    const auto found = tasks_.find(*taskId);
    return found == tasks_.end() ? nullptr : &found->second;
}

const std::unordered_map<int, NetTask>& NetManager::tasks() const {
    return tasks_;
}

void NetManager::cacheData(std::string url, std::vector<std::uint8_t> data) {
    urlCache_[cacheKeyForUrl(url)] = std::move(data);
}

std::optional<std::vector<std::uint8_t>> NetManager::getCachedData(std::string_view url) const {
    const auto cacheKey = cacheKeyForUrl(url);
    if (const auto found = urlCache_.find(cacheKey); found != urlCache_.end()) {
        return found->second;
    }

    const auto lower = toLower(cacheKey);
    const auto baseName = util::getFileNameWithoutExtension(cacheKey);
    if (lower.ends_with(".cct")) {
        if (const auto found = urlCache_.find(baseName + ".cst"); found != urlCache_.end()) {
            return found->second;
        }
        if (const auto found = urlCache_.find(baseName); found != urlCache_.end()) {
            return found->second;
        }
    } else if (lower.ends_with(".cst")) {
        if (const auto found = urlCache_.find(baseName + ".cct"); found != urlCache_.end()) {
            return found->second;
        }
        if (const auto found = urlCache_.find(baseName); found != urlCache_.end()) {
            return found->second;
        }
    } else {
        if (const auto found = urlCache_.find(baseName + ".cct"); found != urlCache_.end()) {
            return found->second;
        }
        if (const auto found = urlCache_.find(baseName + ".cst"); found != urlCache_.end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

const std::unordered_map<std::string, std::vector<std::uint8_t>>& NetManager::urlCache() const {
    return urlCache_;
}

void NetManager::clear() {
    tasks_.clear();
    urlCache_.clear();
    nextTaskId_ = 1;
    lastTaskId_ = 0;
}

void NetManager::shutdown() {
    fetchHandler_ = nullptr;
}

std::string NetManager::resolveUrl(std::string_view url) {
    if (url.empty()) {
        return "";
    }

    auto cleanUrl = std::string(url);
    if (const auto queryIndex = cleanUrl.find('?'); queryIndex != std::string::npos) {
        cleanUrl = cleanUrl.substr(0, queryIndex);
    }

    if (startsWith(cleanUrl, "http://") || startsWith(cleanUrl, "https://")) {
        const auto lastSlash = cleanUrl.find_last_of('/');
        if (lastSlash != std::string::npos && lastSlash < cleanUrl.size() - 1) {
            return cleanUrl.substr(lastSlash + 1);
        }
        return cleanUrl;
    }

    const auto separator = cleanUrl.find_last_of("\\/");
    if (separator != std::string::npos && separator < cleanUrl.size() - 1) {
        return cleanUrl.substr(separator + 1);
    }
    return cleanUrl;
}

std::string NetManager::cacheKeyForUrl(std::string_view url) {
    return util::getFileName(url);
}

std::string NetManager::extractUrlPath(std::string_view url) {
    if (url.empty()) {
        return "";
    }

    auto cleanUrl = std::string(url);
    if (const auto queryIndex = cleanUrl.find('?'); queryIndex != std::string::npos) {
        cleanUrl = cleanUrl.substr(0, queryIndex);
    }
    const auto schemeEnd = cleanUrl.find("://");
    if (schemeEnd == std::string::npos) {
        return "";
    }
    const auto pathStart = cleanUrl.find('/', schemeEnd + 3);
    if (pathStart == std::string::npos) {
        return "/";
    }
    return cleanUrl.substr(pathStart);
}

std::string NetManager::extractOrigin(std::string_view url) {
    const auto schemeEnd = url.find("://");
    if (schemeEnd == std::string_view::npos) {
        return "";
    }
    const auto pathStart = url.find('/', schemeEnd + 3);
    return pathStart == std::string_view::npos ? std::string(url) : std::string(url.substr(0, pathStart));
}

NetTask& NetManager::createGetTask(std::string url) {
    const int taskId = nextTaskId_++;
    lastTaskId_ = taskId;
    auto task = NetTask::get(taskId, url, resolveUrl(url));
    auto [inserted, ok] = tasks_.emplace(taskId, std::move(task));
    (void)ok;
    return inserted->second;
}

NetTask& NetManager::createPostTask(std::string url, std::string postData) {
    const int taskId = nextTaskId_++;
    lastTaskId_ = taskId;
    auto task = NetTask::post(taskId, url, resolveUrl(url), std::move(postData));
    auto [inserted, ok] = tasks_.emplace(taskId, std::move(task));
    (void)ok;
    return inserted->second;
}

void NetManager::executeTask(NetTask& task, bool useCache) {
    task.markInProgress();

    if (fetchHandler_ == nullptr) {
        if (task.method() == NetTaskMethod::Get) {
            std::optional<std::vector<std::uint8_t>> localData;
            std::string cacheUrl = task.originalUrl();

            if (!localHttpRoot_.empty()) {
                std::string httpUrl = httpUrlAgainstBase(task.originalUrl(), basePath_);
                if (isLocalHttpUrl(httpUrl)) {
                    cacheUrl = httpUrl;
                    auto urlPath = extractUrlPath(httpUrl);
                    if (!urlPath.empty()) {
                        if (urlPath.front() == '/') {
                            urlPath.erase(urlPath.begin());
                        }
                        localData = readFileWithFallbacks(std::filesystem::path(localHttpRoot_) / urlPath);
                    }
                }
            }

            if (!localData.has_value() && !basePath_.empty() && !isHttpUrl(basePath_)) {
                localData = readLocalFetchFile(std::filesystem::path(basePath_), task.originalUrl());
            }

            if (localData.has_value()) {
                completeTask(task, std::move(*localData), useCache, cacheUrl);
                return;
            }
        }
        const auto requestUrl = task.method() == NetTaskMethod::Get
            ? httpUrlAgainstBase(task.originalUrl(), basePath_)
            : task.originalUrl();
        if (!remoteFetchEnabled_ && isHttpUrl(requestUrl)) {
            task.fail(403, "Remote network access disabled");
            notifyTask(task);
            return;
        }
        task.fail(404, "No fetch handler configured");
        notifyTask(task);
        return;
    }

    const NetTask* requestTask = &task;
    std::optional<NetTask> normalizedRequestTask;
    if (task.method() == NetTaskMethod::Get) {
        const auto requestUrl = httpUrlAgainstBase(task.originalUrl(), basePath_);
        if (requestUrl != task.originalUrl()) {
            normalizedRequestTask.emplace(task.taskId(),
                                          requestUrl,
                                          resolveUrl(requestUrl),
                                          task.method(),
                                          task.postData());
            requestTask = &*normalizedRequestTask;
        }
    }

    const auto effectiveRequestUrl = task.method() == NetTaskMethod::Get
        ? requestTask->originalUrl()
        : httpUrlAgainstBase(task.originalUrl(), basePath_);
    if (!remoteFetchEnabled_ && isHttpUrl(effectiveRequestUrl)) {
        task.fail(403, "Remote network access disabled");
        notifyTask(task);
        return;
    }

    auto result = fetchHandler_(*requestTask);
    if (!result.data.has_value()) {
        task.fail(result.errorCode != 0 ? result.errorCode : 404,
                  result.errorMessage.empty() ? "Load returned no data" : result.errorMessage);
        notifyTask(task);
        return;
    }

    completeTask(task, std::move(*result.data), useCache, requestTask->originalUrl());
}

void NetManager::completeTask(NetTask& task,
                              std::vector<std::uint8_t> data,
                              bool cacheResult,
                              std::string_view cacheUrl) {
    if (cacheResult) {
        urlCache_[cacheKeyForUrl(cacheUrl.empty() ? task.originalUrl() : cacheUrl)] = data;
    }
    task.complete(std::move(data));
    if (task.result().has_value()) {
        notifyCompletion(task.originalUrl(), *task.result());
    }
    notifyTask(task);
}

void NetManager::notifyCompletion(const std::string& url, const std::vector<std::uint8_t>& data) const {
    if (completionCallback_) {
        completionCallback_(url, data);
    }
}

void NetManager::notifyTask(const NetTask& task) const {
    if (taskCallback_) {
        taskCallback_(task);
    }
}

} // namespace libreshockwave::player::net
