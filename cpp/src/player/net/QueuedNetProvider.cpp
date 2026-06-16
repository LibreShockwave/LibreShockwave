#include "libreshockwave/player/net/QueuedNetProvider.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <utility>

#include "libreshockwave/util/FileUtil.hpp"

namespace libreshockwave::player::net {
namespace {

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool isAbsoluteHttpUrl(std::string_view value) {
    return startsWith(value, "http://") || startsWith(value, "https://");
}

bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) {
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

std::string toLower(std::string_view value) {
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

bool isCastLikeFileName(std::string_view fileName) {
    const std::string lowerName = toLower(fileName);
    return lowerName.ends_with(".cct") || lowerName.ends_with(".cst") ||
           lowerName.find('.') == std::string::npos;
}

void addUnique(std::vector<std::string>& values, std::string value) {
    if (!value.empty() && std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(std::move(value));
    }
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

void putProp(lingo::Datum& propList, std::string key, lingo::Datum value) {
    propList.propListValue().put(lingo::Datum::of(std::move(key)), std::move(value));
}

} // namespace

QueuedNetProvider::QueuedNetProvider(std::string basePath) : basePath_(std::move(basePath)) {}

void QueuedNetProvider::setFetchCompleteCallback(FetchCompleteCallback callback) {
    fetchCompleteCallback_ = std::move(callback);
}

void QueuedNetProvider::setSatisfiedFetchPredicate(SatisfiedFetchPredicate predicate) {
    satisfiedFetchPredicate_ = std::move(predicate);
}

int QueuedNetProvider::preloadNetThing(std::string url) {
    const int taskId = nextTaskId_++;
    lastTaskId_ = taskId;

    if (url.empty()) {
        tasks_.emplace(taskId, Task{taskId, std::move(url), std::nullopt, 0, 0, true});
        return taskId;
    }

    const std::string resolvedUrl = resolveUrl(url);
    if (isDirectoryOnlyUrl(resolvedUrl)) {
        tasks_.emplace(taskId, Task{taskId, resolvedUrl, std::nullopt, 0, 0, true});
        return taskId;
    }

    auto fallbacks = withMovieDirectoryCastFallbacks(resolvedUrl, util::getUrlsWithFallbacks(resolvedUrl));
    if (fallbacks.empty()) {
        fallbacks.push_back(resolvedUrl);
    }

    auto [inserted, ok] = tasks_.emplace(taskId, Task{taskId, resolvedUrl});
    (void)ok;
    auto& task = inserted->second;
    task.fallbackUrls = fallbacks;

    if (auto cached = findCachedData(url, resolvedUrl); cached.has_value()) {
        task.data = *cached;
        task.byteCount = static_cast<int>(cached->size());
        task.done = true;
        if (fetchCompleteCallback_) {
            fetchCompleteCallback_(task.url, *cached);
        }
        return taskId;
    }

    if (isFetchAlreadySatisfied(url, resolvedUrl, fallbacks)) {
        task.byteCount = 1;
        task.done = true;
        return taskId;
    }

    pendingRequests_.push_back(PendingRequest{taskId, fallbacks.front(), "GET", std::nullopt, fallbacks});
    return taskId;
}

int QueuedNetProvider::postNetText(std::string url, std::string postData) {
    const int taskId = nextTaskId_++;
    lastTaskId_ = taskId;

    std::string resolvedUrl = resolveUrl(url);
    tasks_.emplace(taskId, Task{taskId, resolvedUrl});
    pendingRequests_.push_back(PendingRequest{taskId, std::move(resolvedUrl), "POST", std::move(postData), {}});
    return taskId;
}

int QueuedNetProvider::beginMovieNavigation(std::string url) {
    const int taskId = nextTaskId_++;
    lastTaskId_ = taskId;

    std::string resolvedUrl = resolveUrl(url);
    tasks_.emplace(taskId, Task{taskId, std::move(resolvedUrl)});
    pendingMovieNavigationTasks_.push_back(taskId);
    return taskId;
}

void QueuedNetProvider::completeMovieNavigationTasks() {
    while (!pendingMovieNavigationTasks_.empty()) {
        const int taskId = pendingMovieNavigationTasks_.front();
        pendingMovieNavigationTasks_.pop_front();
        if (auto* task = getTask(taskId)) {
            task->done = true;
            task->errorCode = 0;
        }
    }
}

bool QueuedNetProvider::netDone(std::optional<int> taskId) const {
    if (!taskId.has_value() && lastTaskId_ > 0 && pendingRequests_.empty()) {
        return true;
    }
    const auto* task = getTask(taskId);
    return task != nullptr && task->done;
}

std::string QueuedNetProvider::netTextResult(std::optional<int> taskId) const {
    const auto* task = getTask(taskId);
    if (task == nullptr || !task->done || !task->data.has_value()) {
        return "";
    }
    return std::string(task->data->begin(), task->data->end());
}

int QueuedNetProvider::netError(std::optional<int> taskId) const {
    const auto* task = getTask(taskId);
    return task == nullptr ? 0 : task->errorCode;
}

std::string_view QueuedNetProvider::getStreamStatus(std::optional<int> taskId) const {
    const auto* task = getTask(taskId);
    if (task == nullptr) {
        return "Error";
    }
    if (task->done) {
        return task->errorCode == 0 ? "Complete" : "Error";
    }
    return "Loading";
}

lingo::Datum QueuedNetProvider::getStreamStatusDatum(std::optional<int> taskId) const {
    return streamStatusDatum(getTask(taskId));
}

lingo::Datum QueuedNetProvider::getStreamStatusDatum(std::string_view url) const {
    const std::string normalizedUrl = normalizeLookupUrl(url);
    const Task* task = getTask(normalizedUrl);
    Task directoryTask{0, std::string(url), std::nullopt, 0, 0, true};
    if (task == nullptr && isDirectoryOnlyUrl(normalizedUrl)) {
        task = &directoryTask;
    }
    return streamStatusDatum(task);
}

std::optional<std::string> QueuedNetProvider::getTaskUrl(int taskId) const {
    if (const auto* task = getTask(taskId)) {
        return task->url;
    }
    return std::nullopt;
}

std::string QueuedNetProvider::getDebugStatus() const {
    std::ostringstream out;
    out << "netProvider: tasks=" << tasks_.size()
        << " pendingRequests=" << pendingRequests_.size()
        << " pendingMovieNavigationTasks=" << pendingMovieNavigationTasks_.size()
        << " lastTaskId=" << lastTaskId_
        << " latestTaskDone=" << (latestTaskDone() ? "true" : "false")
        << '\n';
    for (const auto& [id, task] : tasks_) {
        out << "task #" << id
            << " url=" << task.url
            << " done=" << (task.done ? "true" : "false")
            << " error=" << task.errorCode
            << " byteCount=" << task.byteCount
            << " poll=" << task.pollCount
            << " fallbackCount=" << task.fallbackUrls.size()
            << '\n';
    }
    for (std::size_t index = 0; index < pendingRequests_.size(); ++index) {
        const auto& request = pendingRequests_[index];
        out << "pending #" << index
            << " taskId=" << request.taskId
            << " method=" << request.method
            << " url=" << request.url
            << '\n';
    }
    return out.str();
}

int QueuedNetProvider::taskCount() const {
    return static_cast<int>(tasks_.size());
}

int QueuedNetProvider::lastTaskId() const {
    return lastTaskId_;
}

int QueuedNetProvider::pendingRequestCount() const {
    return static_cast<int>(pendingRequests_.size());
}

int QueuedNetProvider::pendingMovieNavigationTaskCount() const {
    return static_cast<int>(pendingMovieNavigationTasks_.size());
}

std::vector<QueuedNetProvider::PendingRequest> QueuedNetProvider::pendingMovieNavigationRequests() const {
    std::vector<PendingRequest> requests;
    requests.reserve(pendingMovieNavigationTasks_.size());
    for (const int taskId : pendingMovieNavigationTasks_) {
        if (const auto* task = getTask(taskId)) {
            requests.push_back(PendingRequest{taskId, task->url, "GET", std::nullopt, task->fallbackUrls});
        }
    }
    return requests;
}

bool QueuedNetProvider::latestTaskDone() const {
    const auto* task = getTask(std::nullopt);
    return task != nullptr && task->done;
}

const std::vector<QueuedNetProvider::PendingRequest>& QueuedNetProvider::pendingRequests() const {
    return pendingRequests_;
}

const QueuedNetProvider::PendingRequest* QueuedNetProvider::getRequest(int index) const {
    if (index < 0 || index >= static_cast<int>(pendingRequests_.size())) {
        return nullptr;
    }
    return &pendingRequests_[static_cast<std::size_t>(index)];
}

void QueuedNetProvider::drainPendingRequests() {
    pendingRequests_.clear();
}

void QueuedNetProvider::onFetchComplete(int taskId, std::vector<std::uint8_t> data) {
    auto* task = getTask(taskId);
    if (task == nullptr) {
        return;
    }

    task->byteCount = static_cast<int>(data.size());
    task->done = true;
    task->errorCode = 0;
    task->data = std::move(data);

    if (task->data.has_value() && !task->url.empty()) {
        cacheData(task->url, *task->data);
        if (fetchCompleteCallback_) {
            fetchCompleteCallback_(task->url, *task->data);
        }
    }
}

void QueuedNetProvider::onMovieNavigationComplete(int taskId) {
    auto* task = getTask(taskId);
    if (task == nullptr) {
        return;
    }
    task->done = true;
    task->errorCode = 0;

    const auto removeIt = std::remove(pendingMovieNavigationTasks_.begin(),
                                      pendingMovieNavigationTasks_.end(),
                                      taskId);
    pendingMovieNavigationTasks_.erase(removeIt, pendingMovieNavigationTasks_.end());
}

void QueuedNetProvider::onFetchStatusComplete(int taskId, int byteCount) {
    auto* task = getTask(taskId);
    if (task == nullptr) {
        return;
    }
    task->data = std::nullopt;
    task->byteCount = std::max(0, byteCount);
    task->done = true;
    task->errorCode = 0;
}

void QueuedNetProvider::onFetchError(int taskId, int status) {
    auto* task = getTask(taskId);
    if (task == nullptr) {
        return;
    }
    task->errorCode = status != 0 ? status : -1;
    task->done = true;
}

const std::string& QueuedNetProvider::basePath() const {
    return basePath_;
}

QueuedNetProvider::Task* QueuedNetProvider::getTask(std::optional<int> taskId) {
    if (!taskId.has_value() || *taskId == 0) {
        if (lastTaskId_ <= 0) {
            return nullptr;
        }
        taskId = lastTaskId_;
    }
    const auto found = tasks_.find(*taskId);
    return found == tasks_.end() ? nullptr : &found->second;
}

const QueuedNetProvider::Task* QueuedNetProvider::getTask(std::optional<int> taskId) const {
    if (!taskId.has_value() || *taskId == 0) {
        if (lastTaskId_ <= 0) {
            return nullptr;
        }
        taskId = lastTaskId_;
    }
    const auto found = tasks_.find(*taskId);
    return found == tasks_.end() ? nullptr : &found->second;
}

const QueuedNetProvider::Task* QueuedNetProvider::getTask(std::string_view url) const {
    if (url.empty()) {
        return nullptr;
    }
    const std::string normalizedUrl = normalizeLookupUrl(url);
    const Task* bestMatch = nullptr;
    for (const auto& [id, task] : tasks_) {
        (void)id;
        if (taskMatchesUrl(task, normalizedUrl)) {
            if (bestMatch == nullptr || task.id > bestMatch->id) {
                bestMatch = &task;
            }
        }
    }
    return bestMatch;
}

bool QueuedNetProvider::taskMatchesUrl(const Task& task, std::string_view url) const {
    const std::string normalizedUrl = normalizeLookupUrl(url);
    if (normalizedUrl.empty()) {
        return false;
    }

    const auto taskKeys = buildTaskLookupKeys(task);
    if (!taskKeys.empty()) {
        for (const auto& key : buildLookupKeys(normalizedUrl)) {
            if (std::find(taskKeys.begin(), taskKeys.end(), key) != taskKeys.end()) {
                return true;
            }
        }
    }

    const std::string normalizedTaskUrl = normalizeLookupUrl(task.url);
    if (!normalizedTaskUrl.empty() && equalsIgnoreCase(normalizedTaskUrl, normalizedUrl)) {
        return true;
    }
    for (const auto& fallback : task.fallbackUrls) {
        const std::string normalizedFallback = normalizeLookupUrl(fallback);
        if (!normalizedFallback.empty() && equalsIgnoreCase(normalizedFallback, normalizedUrl)) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> QueuedNetProvider::buildTaskLookupKeys(const Task& task) const {
    std::vector<std::string> keys;
    addLookupCacheKeys(keys, task.url);
    for (const auto& fallback : task.fallbackUrls) {
        addLookupCacheKeys(keys, fallback);
    }
    return keys;
}

std::vector<std::string> QueuedNetProvider::buildLookupKeys(std::string_view url) const {
    std::vector<std::string> keys;
    const std::string normalizedUrl = normalizeLookupUrl(url);
    if (normalizedUrl.empty()) {
        return keys;
    }
    addLookupCacheKeys(keys, normalizedUrl);
    for (const auto& candidate : withMovieDirectoryCastFallbacks(normalizedUrl, util::getUrlsWithFallbacks(normalizedUrl))) {
        addLookupCacheKeys(keys, candidate);
    }
    return keys;
}

void QueuedNetProvider::addLookupCacheKeys(std::vector<std::string>& keys, std::string_view url) const {
    if (url.empty()) {
        return;
    }

    addCacheKeys(keys, url);
    const std::string normalizedUrl = normalizeLookupUrl(url);
    if (!normalizedUrl.empty() && normalizedUrl != url) {
        addCacheKeys(keys, normalizedUrl);
    }
    const std::string decodedUrl = decodeLookupUrl(url);
    if (!decodedUrl.empty() && decodedUrl != url) {
        addCacheKeys(keys, decodedUrl);
    }
    const std::string decodedNormalized = decodeLookupUrl(normalizedUrl);
    if (!decodedNormalized.empty() && decodedNormalized != url && decodedNormalized != normalizedUrl &&
        decodedNormalized != decodedUrl) {
        addCacheKeys(keys, decodedNormalized);
    }

    const std::string fileName = util::getFileName(url);
    if (!fileName.empty() && fileName.find('.') == std::string::npos) {
        addCacheKeys(keys, std::string(url) + ".cct");
        addCacheKeys(keys, std::string(url) + ".cst");
        if (!normalizedUrl.empty()) {
            addCacheKeys(keys, normalizedUrl + ".cct");
            addCacheKeys(keys, normalizedUrl + ".cst");
        }
        if (!decodedUrl.empty()) {
            addCacheKeys(keys, decodedUrl + ".cct");
            addCacheKeys(keys, decodedUrl + ".cst");
        }
    }
}

std::optional<std::vector<std::uint8_t>> QueuedNetProvider::findCachedData(
    std::string_view originalUrl,
    std::string_view resolvedUrl) const {
    for (const auto& key : buildCacheKeys(originalUrl, resolvedUrl)) {
        if (const auto found = urlCache_.find(key); found != urlCache_.end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

void QueuedNetProvider::cacheData(std::string_view url, const std::vector<std::uint8_t>& data) {
    if (url.empty() || data.empty()) {
        return;
    }
    for (const auto& key : buildCacheKeys(url, url)) {
        urlCache_[key] = data;
    }
}

std::vector<std::string> QueuedNetProvider::buildCacheKeys(std::string_view originalUrl,
                                                           std::string_view resolvedUrl) const {
    std::vector<std::string> keys;
    addCacheKeys(keys, originalUrl);
    addCacheKeys(keys, resolvedUrl);
    return keys;
}

void QueuedNetProvider::addCacheKeys(std::vector<std::string>& keys, std::string_view url) const {
    if (url.empty()) {
        return;
    }
    const std::string fileName = util::getFileName(url);
    if (fileName.empty()) {
        return;
    }
    addUnique(keys, toLower(fileName));
    const std::string baseName = util::getFileNameWithoutExtension(fileName);
    if (!baseName.empty() && isCastLikeFileName(fileName)) {
        addUnique(keys, toLower(baseName));
    }
}

bool QueuedNetProvider::isFetchAlreadySatisfied(std::string_view originalUrl,
                                                std::string_view resolvedUrl,
                                                const std::vector<std::string>& fallbacks) const {
    if (!satisfiedFetchPredicate_) {
        return false;
    }
    if (satisfiedFetchPredicate_(originalUrl) || satisfiedFetchPredicate_(resolvedUrl)) {
        return true;
    }
    for (const auto& fallback : fallbacks) {
        if (satisfiedFetchPredicate_(fallback)) {
            return true;
        }
    }
    return false;
}

bool QueuedNetProvider::isDirectoryOnlyUrl(std::string_view url) const {
    return util::getFileName(url).empty();
}

std::string QueuedNetProvider::normalizeLookupUrl(std::string_view rawUrl) const {
    std::string normalized(rawUrl);
    auto first = std::find_if_not(normalized.begin(), normalized.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    auto last = std::find_if_not(normalized.rbegin(), normalized.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    if (first >= last) {
        return "";
    }
    normalized = std::string(first, last);
    if (const auto queryStart = normalized.find('?'); queryStart != std::string::npos) {
        normalized = normalized.substr(0, queryStart);
    }
    if (const auto hashStart = normalized.find('#'); hashStart != std::string::npos) {
        normalized = normalized.substr(0, hashStart);
    }
    return normalized;
}

std::string QueuedNetProvider::decodeLookupUrl(std::string_view rawUrl) const {
    std::string decoded;
    decoded.reserve(rawUrl.size());
    for (std::size_t index = 0; index < rawUrl.size(); ++index) {
        const char ch = rawUrl[index];
        if (ch == '+') {
            decoded.push_back(' ');
            continue;
        }
        if (ch == '%' && index + 2 < rawUrl.size()) {
            const int high = hexValue(rawUrl[index + 1]);
            const int low = hexValue(rawUrl[index + 2]);
            if (high >= 0 && low >= 0) {
                decoded.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }
        decoded.push_back(ch);
    }
    return decoded;
}

std::string QueuedNetProvider::resolveUrl(std::string_view url) const {
    if (url.empty()) {
        return "";
    }
    if (isAbsoluteHttpUrl(url)) {
        return std::string(url);
    }
    if (startsWith(url, "/") && !basePath_.empty()) {
        const std::string origin = NetManager::extractOrigin(basePath_);
        if (!origin.empty()) {
            return origin + std::string(url);
        }
    }

    const std::string fileName = util::getFileName(url);
    if (!basePath_.empty()) {
        std::string base = basePath_;
        const auto baseSlash = base.find_last_of('/');
        const auto baseDot = base.find_last_of('.');
        if (baseSlash != std::string::npos && baseDot != std::string::npos && baseDot > baseSlash) {
            base = base.substr(0, baseSlash + 1);
        } else if (!base.ends_with('/')) {
            base.push_back('/');
        }
        return base + fileName;
    }
    return fileName;
}

std::vector<std::string> QueuedNetProvider::withMovieDirectoryCastFallbacks(
    std::string_view resolvedUrl,
    std::vector<std::string> fallbacks) const {
    if (resolvedUrl.empty() || basePath_.empty()) {
        return fallbacks;
    }

    const std::string fileName = util::getFileName(resolvedUrl);
    if (fileName.empty()) {
        return fallbacks;
    }
    if (!isCastLikeFileName(fileName)) {
        return fallbacks;
    }

    const std::string origin = NetManager::extractOrigin(basePath_);
    const std::string movieDir = extractMovieDirectory(basePath_);
    if (origin.empty() || movieDir.empty() || movieDir == origin + "/") {
        return fallbacks;
    }

    const std::string rootUrl = origin + "/" + fileName;
    const std::string lowerResolved = toLower(resolvedUrl);
    if (lowerResolved != toLower(rootUrl) &&
        lowerResolved != toLower(rootUrl + ".cct") &&
        lowerResolved != toLower(rootUrl + ".cst")) {
        return fallbacks;
    }

    std::vector<std::string> urls;
    const std::string movieDirUrl = movieDir + fileName;
    for (auto& fallback : util::getUrlsWithFallbacks(movieDirUrl)) {
        addUnique(urls, std::move(fallback));
    }
    for (auto& fallback : fallbacks) {
        addUnique(urls, std::move(fallback));
    }
    return urls;
}

std::string QueuedNetProvider::extractMovieDirectory(std::string_view url) {
    if (url.empty()) {
        return "";
    }
    std::string clean(url);
    if (const auto query = clean.find('?'); query != std::string::npos) {
        clean = clean.substr(0, query);
    }
    const auto slash = clean.find_last_of('/');
    if (slash == std::string::npos) {
        return "";
    }
    return clean.substr(0, slash + 1);
}

lingo::Datum QueuedNetProvider::streamStatusDatum(const Task* task) const {
    auto props = lingo::Datum::propList();
    if (task == nullptr) {
        putProp(props, "URL", lingo::Datum::of(std::string()));
        putProp(props, "state", lingo::Datum::of(std::string("Error")));
        putProp(props, "bytesSoFar", lingo::Datum::of(0));
        putProp(props, "bytesTotal", lingo::Datum::of(0));
        putProp(props, "error", lingo::Datum::of(std::string("OK")));
        return props;
    }

    auto* mutableTask = const_cast<Task*>(task);
    int byteCount = task->byteCount;
    if (!task->done) {
        ++mutableTask->pollCount;
        byteCount = mutableTask->pollCount;
    }

    const std::string state = task->done ? (task->errorCode == 0 ? "Complete" : "Error") : "Loading";
    putProp(props, "URL", lingo::Datum::of(task->url));
    putProp(props, "state", lingo::Datum::of(state));
    putProp(props, "bytesSoFar", lingo::Datum::of(byteCount));
    putProp(props, "bytesTotal", lingo::Datum::of(task->done ? task->byteCount : 0));
    putProp(props, "error", task->errorCode == 0
        ? lingo::Datum::of(std::string("OK"))
        : lingo::Datum::of(std::to_string(task->errorCode)));
    return props;
}

} // namespace libreshockwave::player::net
