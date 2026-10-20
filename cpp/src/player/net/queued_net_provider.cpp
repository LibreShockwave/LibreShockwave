#include "libreshockwave/player/net/queued_net_provider.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

#include "libreshockwave/util/file_util.hpp"

namespace libreshockwave::player::net {
namespace {

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool isAbsoluteHttpUrl(std::string_view value) {
    return startsWith(value, "http://") || startsWith(value, "https://");
}

std::string toLower(std::string_view value) {
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

bool isCastLikeFileName(std::string_view file_name) {
    const std::string lower_name = toLower(file_name);
    return lower_name.ends_with(".cct") || lower_name.ends_with(".cst") ||
           lower_name.find('.') == std::string::npos;
}

bool isExternalCastUrl(std::string_view url) {
    const auto path_end = url.find_first_of("?#");
    const auto path = url.substr(0, path_end);
    const auto file_name = util::getFileName(path);
    const auto lower_name = toLower(file_name);
    return lower_name.ends_with(".cct") || lower_name.ends_with(".cst");
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

void putProp(lingo::Datum& prop_list, std::string key, lingo::Datum value) {
    prop_list.propListValue().put(lingo::Datum::of(std::move(key)), std::move(value));
}

} // namespace

QueuedNetProvider::QueuedNetProvider(std::string base_path) : base_path_(std::move(base_path)) {}

void QueuedNetProvider::setFetchCompleteCallback(FetchCompleteCallback callback) {
    fetch_complete_callback_ = std::move(callback);
}

void QueuedNetProvider::setSatisfiedFetchPredicate(SatisfiedFetchPredicate predicate) {
    satisfied_fetch_predicate_ = std::move(predicate);
}

int QueuedNetProvider::preloadNetThing(std::string url) {
    if (url.empty()) {
        const int task_id = next_task_id_++;
        last_task_id_ = task_id;
        tasks_.emplace(task_id, Task{task_id, std::move(url), std::nullopt, 0, 0, true});
        indexTask(tasks_.at(task_id));
        return task_id;
    }

    const std::string resolved_url = resolveUrl(url);
    if (isDirectoryOnlyUrl(resolved_url)) {
        const int task_id = next_task_id_++;
        last_task_id_ = task_id;
        tasks_.emplace(task_id, Task{task_id, resolved_url, std::nullopt, 0, 0, true});
        indexTask(tasks_.at(task_id));
        return task_id;
    }

    auto fallbacks = withMovieDirectoryCastFallbacks(resolved_url, util::getUrlsWithFallbacks(resolved_url));
    if (fallbacks.empty()) {
        fallbacks.push_back(resolved_url);
    }

    // Cast libraries are often declared in many placeholder slots that all
    // point at the same external CCT/CST.  A second request can arrive before
    // the first HTTP result has populated urlCache_.  Reuse the pending task
    // in that window so one resource is fetched and parsed once; callers that
    // poll the returned task still observe the original task's completion.
    if (isExternalCastUrl(resolved_url)) {
        const auto* pending = getTask(resolved_url);
        if (pending != nullptr && !pending->done && isExternalCastUrl(pending->url)) {
            last_task_id_ = pending->id;
            return pending->id;
        }
        for (const auto& fallback : fallbacks) {
            pending = getTask(fallback);
            if (pending != nullptr && !pending->done && isExternalCastUrl(pending->url)) {
                last_task_id_ = pending->id;
                return pending->id;
            }
        }
    }

    const int task_id = next_task_id_++;
    last_task_id_ = task_id;

    auto [inserted, ok] = tasks_.emplace(task_id, Task{task_id, resolved_url});
    (void)ok;
    auto& task = inserted->second;
    task.fallback_urls = fallbacks;
    indexTask(task);

    if (auto cached = findCachedData(url, resolved_url); cached.has_value()) {
        task.data = *cached;
        task.byte_count = static_cast<int>(cached->size());
        task.done = true;
        if (fetch_complete_callback_) {
            fetch_complete_callback_(task.url, *cached);
        }
        return task_id;
    }

    if (!isCastLikeFileName(util::getFileName(resolved_url)) &&
        isFetchAlreadySatisfied(url, resolved_url, fallbacks)) {
        task.byte_count = 1;
        task.done = true;
        return task_id;
    }

    trackPendingCastLoad(task);
    pending_requests_.push_back(PendingRequest{task_id, fallbacks.front(), "GET", std::nullopt, fallbacks});
    return task_id;
}

int QueuedNetProvider::postNetText(std::string url, std::string post_data) {
    const int task_id = next_task_id_++;
    last_task_id_ = task_id;

    std::string resolved_url = resolveUrl(url);
    auto [inserted, ok] = tasks_.emplace(task_id, Task{task_id, resolved_url});
    (void)ok;
    indexTask(inserted->second);
    trackPendingCastLoad(inserted->second);
    pending_requests_.push_back(PendingRequest{task_id, std::move(resolved_url), "POST", std::move(post_data), {}});
    return task_id;
}

bool QueuedNetProvider::hasPendingCastLoads() const {
    return pending_external_cast_loads_ > 0;
}

int QueuedNetProvider::beginMovieNavigation(std::string url) {
    const int task_id = next_task_id_++;
    last_task_id_ = task_id;

    std::string resolved_url = resolveUrl(url);
    auto [inserted, ok] = tasks_.emplace(task_id, Task{task_id, std::move(resolved_url)});
    (void)ok;
    indexTask(inserted->second);
    trackPendingCastLoad(inserted->second);
    pending_movie_navigation_tasks_.push_back(task_id);
    return task_id;
}

void QueuedNetProvider::completeMovieNavigationTasks() {
    while (!pending_movie_navigation_tasks_.empty()) {
        const int task_id = pending_movie_navigation_tasks_.front();
        pending_movie_navigation_tasks_.pop_front();
        if (auto* task = getTask(task_id)) {
            markTaskDone(*task);
            task->error_code = 0;
        }
    }
}

bool QueuedNetProvider::netDone(std::optional<int> task_id) const {
    if (!task_id.has_value() && last_task_id_ > 0 && pending_requests_.empty()) {
        return true;
    }
    const auto* task = getTask(task_id);
    return task != nullptr && task->done;
}

std::string QueuedNetProvider::netTextResult(std::optional<int> task_id) const {
    const auto* task = getTask(task_id);
    if (task == nullptr || !task->done || !task->data.has_value()) {
        return "";
    }
    return std::string(task->data->begin(), task->data->end());
}

int QueuedNetProvider::netError(std::optional<int> task_id) const {
    const auto* task = getTask(task_id);
    return task == nullptr ? 0 : task->error_code;
}

std::string_view QueuedNetProvider::getStreamStatus(std::optional<int> task_id) const {
    const auto* task = getTask(task_id);
    if (task == nullptr) {
        return "Error";
    }
    if (task->done) {
        return task->error_code == 0 ? "Complete" : "Error";
    }
    return "Loading";
}

lingo::Datum QueuedNetProvider::getStreamStatusDatum(std::optional<int> task_id) const {
    return streamStatusDatum(getTask(task_id));
}

lingo::Datum QueuedNetProvider::getStreamStatusDatum(std::string_view url) const {
    const std::string normalized_url = normalizeLookupUrl(url);
    const Task* task = getTask(normalized_url);
    Task directory_task{0, std::string(url), std::nullopt, 0, 0, true};
    if (task == nullptr && isDirectoryOnlyUrl(normalized_url)) {
        task = &directory_task;
    }
    return streamStatusDatum(task);
}

std::optional<std::string> QueuedNetProvider::getTaskUrl(int task_id) const {
    if (const auto* task = getTask(task_id)) {
        return task->url;
    }
    return std::nullopt;
}

std::string QueuedNetProvider::getDebugStatus() const {
    std::ostringstream out;
    out << "netProvider: tasks=" << tasks_.size()
        << " pendingRequests=" << pending_requests_.size()
        << " pendingMovieNavigationTasks=" << pending_movie_navigation_tasks_.size()
        << " lastTaskId=" << last_task_id_
        << " latestTaskDone=" << (latestTaskDone() ? "true" : "false")
        << '\n';
    for (const auto& [id, task] : tasks_) {
        out << "task #" << id
            << " url=" << task.url
            << " done=" << (task.done ? "true" : "false")
            << " error=" << task.error_code
            << " byteCount=" << task.byte_count
            << " poll=" << task.poll_count
            << " fallbackCount=" << task.fallback_urls.size()
            << '\n';
    }
    for (std::size_t index = 0; index < pending_requests_.size(); ++index) {
        const auto& request = pending_requests_[index];
        out << "pending #" << index
            << " taskId=" << request.task_id
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
    return last_task_id_;
}

int QueuedNetProvider::pendingRequestCount() const {
    return static_cast<int>(pending_requests_.size());
}

int QueuedNetProvider::pendingMovieNavigationTaskCount() const {
    return static_cast<int>(pending_movie_navigation_tasks_.size());
}

std::vector<QueuedNetProvider::PendingRequest> QueuedNetProvider::pendingMovieNavigationRequests() const {
    std::vector<PendingRequest> requests;
    requests.reserve(pending_movie_navigation_tasks_.size());
    for (const int task_id : pending_movie_navigation_tasks_) {
        if (const auto* task = getTask(task_id)) {
            requests.push_back(PendingRequest{task_id, task->url, "GET", std::nullopt, task->fallback_urls});
        }
    }
    return requests;
}

bool QueuedNetProvider::latestTaskDone() const {
    const auto* task = getTask(std::nullopt);
    return task != nullptr && task->done;
}

const std::vector<QueuedNetProvider::PendingRequest>& QueuedNetProvider::pendingRequests() const {
    return pending_requests_;
}

const QueuedNetProvider::PendingRequest* QueuedNetProvider::getRequest(int index) const {
    if (index < 0 || index >= static_cast<int>(pending_requests_.size())) {
        return nullptr;
    }
    return &pending_requests_[static_cast<std::size_t>(index)];
}

void QueuedNetProvider::drainPendingRequests() {
    pending_requests_.clear();
}

void QueuedNetProvider::onFetchComplete(int task_id, std::vector<std::uint8_t> data) {
    auto* task = getTask(task_id);
    if (task == nullptr) {
        return;
    }

    task->byte_count = static_cast<int>(data.size());
    markTaskDone(*task);
    task->error_code = 0;
    task->data = std::move(data);

    if (task->data.has_value() && !task->url.empty()) {
        cacheData(task->url, *task->data);
        if (fetch_complete_callback_) {
            fetch_complete_callback_(task->url, *task->data);
        }
    }
}

void QueuedNetProvider::onMovieNavigationComplete(int task_id) {
    auto* task = getTask(task_id);
    if (task == nullptr) {
        return;
    }
    markTaskDone(*task);
    task->error_code = 0;

    const auto remove_it = std::remove(pending_movie_navigation_tasks_.begin(),
                                      pending_movie_navigation_tasks_.end(),
                                      task_id);
    pending_movie_navigation_tasks_.erase(remove_it, pending_movie_navigation_tasks_.end());
}

void QueuedNetProvider::onFetchStatusComplete(int task_id, int byte_count) {
    auto* task = getTask(task_id);
    if (task == nullptr) {
        return;
    }
    task->data = std::nullopt;
    task->byte_count = std::max(0, byte_count);
    markTaskDone(*task);
    task->error_code = 0;
}

void QueuedNetProvider::onFetchError(int task_id, int status) {
    auto* task = getTask(task_id);
    if (task == nullptr) {
        return;
    }
    task->error_code = status != 0 ? status : -1;
    markTaskDone(*task);
}

const std::string& QueuedNetProvider::basePath() const {
    return base_path_;
}

QueuedNetProvider::Task* QueuedNetProvider::getTask(std::optional<int> task_id) {
    if (!task_id.has_value() || *task_id == 0) {
        if (last_task_id_ <= 0) {
            return nullptr;
        }
        task_id = last_task_id_;
    }
    const auto found = tasks_.find(*task_id);
    return found == tasks_.end() ? nullptr : &found->second;
}

const QueuedNetProvider::Task* QueuedNetProvider::getTask(std::optional<int> task_id) const {
    if (!task_id.has_value() || *task_id == 0) {
        if (last_task_id_ <= 0) {
            return nullptr;
        }
        task_id = last_task_id_;
    }
    const auto found = tasks_.find(*task_id);
    return found == tasks_.end() ? nullptr : &found->second;
}

const QueuedNetProvider::Task* QueuedNetProvider::getTask(std::string_view url) const {
    if (url.empty()) {
        return nullptr;
    }
    const std::string normalized_url = normalizeLookupUrl(url);
    if (normalized_url.empty()) {
        return nullptr;
    }

    const Task* best_match = nullptr;
    const auto consider_task = [this, &best_match](int task_id) {
        const auto found = tasks_.find(task_id);
        if (found != tasks_.end() && (best_match == nullptr || task_id > best_match->id)) {
            best_match = &found->second;
        }
    };

    if (const auto found = latest_task_by_exact_url_.find(toLower(normalized_url));
        found != latest_task_by_exact_url_.end()) {
        consider_task(found->second);
    }
    for (const auto& key : buildLookupKeys(normalized_url)) {
        if (const auto found = latest_task_by_lookup_key_.find(key); found != latest_task_by_lookup_key_.end()) {
            consider_task(found->second);
        }
    }
    return best_match;
}

void QueuedNetProvider::indexTask(const Task& task) {
    const auto index_exact_url = [this, task_id = task.id](std::string_view url) {
        const std::string normalized_url = normalizeLookupUrl(url);
        if (!normalized_url.empty()) {
            latest_task_by_exact_url_[toLower(normalized_url)] = task_id;
        }
    };

    index_exact_url(task.url);
    for (const auto& fallback : task.fallback_urls) {
        index_exact_url(fallback);
    }
    for (const auto& key : buildTaskLookupKeys(task)) {
        latest_task_by_lookup_key_[key] = task.id;
    }
}

void QueuedNetProvider::trackPendingCastLoad(const Task& task) {
    if (!task.done && isExternalCastUrl(task.url)) {
        ++pending_external_cast_loads_;
    }
}

void QueuedNetProvider::markTaskDone(Task& task) {
    if (task.done) {
        return;
    }
    if (isExternalCastUrl(task.url) && pending_external_cast_loads_ > 0) {
        --pending_external_cast_loads_;
    }
    task.done = true;
}

std::vector<std::string> QueuedNetProvider::buildTaskLookupKeys(const Task& task) const {
    std::vector<std::string> keys;
    addLookupCacheKeys(keys, task.url);
    for (const auto& fallback : task.fallback_urls) {
        addLookupCacheKeys(keys, fallback);
    }
    return keys;
}

std::vector<std::string> QueuedNetProvider::buildLookupKeys(std::string_view url) const {
    std::vector<std::string> keys;
    const std::string normalized_url = normalizeLookupUrl(url);
    if (normalized_url.empty()) {
        return keys;
    }
    addLookupCacheKeys(keys, normalized_url);
    for (const auto& candidate : withMovieDirectoryCastFallbacks(normalized_url, util::getUrlsWithFallbacks(normalized_url))) {
        addLookupCacheKeys(keys, candidate);
    }
    return keys;
}

void QueuedNetProvider::addLookupCacheKeys(std::vector<std::string>& keys, std::string_view url) const {
    if (url.empty()) {
        return;
    }

    addCacheKeys(keys, url);
    const std::string normalized_url = normalizeLookupUrl(url);
    if (!normalized_url.empty() && normalized_url != url) {
        addCacheKeys(keys, normalized_url);
    }
    const std::string decoded_url = decodeLookupUrl(url);
    if (!decoded_url.empty() && decoded_url != url) {
        addCacheKeys(keys, decoded_url);
    }
    const std::string decoded_normalized = decodeLookupUrl(normalized_url);
    if (!decoded_normalized.empty() && decoded_normalized != url && decoded_normalized != normalized_url &&
        decoded_normalized != decoded_url) {
        addCacheKeys(keys, decoded_normalized);
    }

    const std::string file_name = util::getFileName(url);
    if (!file_name.empty() && file_name.find('.') == std::string::npos) {
        addCacheKeys(keys, std::string(url) + ".cct");
        addCacheKeys(keys, std::string(url) + ".cst");
        if (!normalized_url.empty()) {
            addCacheKeys(keys, normalized_url + ".cct");
            addCacheKeys(keys, normalized_url + ".cst");
        }
        if (!decoded_url.empty()) {
            addCacheKeys(keys, decoded_url + ".cct");
            addCacheKeys(keys, decoded_url + ".cst");
        }
    }
}

std::optional<std::vector<std::uint8_t>> QueuedNetProvider::findCachedData(
    std::string_view original_url,
    std::string_view resolved_url) const {
    for (const auto& key : buildCacheKeys(original_url, resolved_url)) {
        if (const auto found = url_cache_.find(key); found != url_cache_.end()) {
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
        url_cache_[key] = data;
    }
}

std::vector<std::string> QueuedNetProvider::buildCacheKeys(std::string_view original_url,
                                                           std::string_view resolved_url) const {
    std::vector<std::string> keys;
    addCacheKeys(keys, original_url);
    addCacheKeys(keys, resolved_url);
    return keys;
}

void QueuedNetProvider::addCacheKeys(std::vector<std::string>& keys, std::string_view url) const {
    if (url.empty()) {
        return;
    }
    const std::string file_name = util::getFileName(url);
    if (file_name.empty()) {
        return;
    }
    addUnique(keys, toLower(file_name));
    const std::string base_name = util::getFileNameWithoutExtension(file_name);
    if (!base_name.empty() && isCastLikeFileName(file_name)) {
        addUnique(keys, toLower(base_name));
    }
}

bool QueuedNetProvider::isFetchAlreadySatisfied(std::string_view original_url,
                                                std::string_view resolved_url,
                                                const std::vector<std::string>& fallbacks) const {
    if (!satisfied_fetch_predicate_) {
        return false;
    }
    if (satisfied_fetch_predicate_(original_url) || satisfied_fetch_predicate_(resolved_url)) {
        return true;
    }
    for (const auto& fallback : fallbacks) {
        if (satisfied_fetch_predicate_(fallback)) {
            return true;
        }
    }
    return false;
}

bool QueuedNetProvider::isDirectoryOnlyUrl(std::string_view url) const {
    return util::getFileName(url).empty();
}

std::string QueuedNetProvider::normalizeLookupUrl(std::string_view raw_url) const {
    std::string normalized(raw_url);
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
    if (const auto query_start = normalized.find('?'); query_start != std::string::npos) {
        normalized = normalized.substr(0, query_start);
    }
    if (const auto hash_start = normalized.find('#'); hash_start != std::string::npos) {
        normalized = normalized.substr(0, hash_start);
    }
    return normalized;
}

std::string QueuedNetProvider::decodeLookupUrl(std::string_view raw_url) const {
    std::string decoded;
    decoded.reserve(raw_url.size());
    for (std::size_t index = 0; index < raw_url.size(); ++index) {
        const char ch = raw_url[index];
        if (ch == '+') {
            decoded.push_back(' ');
            continue;
        }
        if (ch == '%' && index + 2 < raw_url.size()) {
            const int high = hexValue(raw_url[index + 1]);
            const int low = hexValue(raw_url[index + 2]);
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
        // Director's browser/plugin loader represents a movie-relative cast
        // as `movie.dcr?/cast.cct`.  The part after `?` is a virtual path,
        // not an HTTP query; sending it literally makes the server return
        // the movie's empty-cast fallback for every dynamic cast.
        const auto query = url.find('?');
        if (query != std::string_view::npos && query + 1 < url.size() && url[query + 1] == '/') {
            const auto movie_path = url.substr(0, query);
            const auto slash = movie_path.find_last_of('/');
            if (slash != std::string_view::npos) {
                return std::string(movie_path.substr(0, slash + 1)) +
                       std::string(url.substr(query + 2));
            }
        }
        return std::string(url);
    }
    if (startsWith(url, "/") && !base_path_.empty()) {
        const std::string origin = NetManager::extractOrigin(base_path_);
        if (!origin.empty()) {
            return origin + std::string(url);
        }
    }

    const std::string file_name = util::getFileName(url);
    if (!base_path_.empty()) {
        std::string base = base_path_;
        const auto base_slash = base.find_last_of('/');
        const auto base_dot = base.find_last_of('.');
        if (base_slash != std::string::npos && base_dot != std::string::npos && base_dot > base_slash) {
            base = base.substr(0, base_slash + 1);
        } else if (!base.ends_with('/')) {
            base.push_back('/');
        }
        return base + file_name;
    }
    return file_name;
}

std::vector<std::string> QueuedNetProvider::withMovieDirectoryCastFallbacks(
    std::string_view resolved_url,
    std::vector<std::string> fallbacks) const {
    if (resolved_url.empty() || base_path_.empty()) {
        return fallbacks;
    }

    const std::string file_name = util::getFileName(resolved_url);
    if (file_name.empty()) {
        return fallbacks;
    }
    if (!isCastLikeFileName(file_name)) {
        return fallbacks;
    }

    const std::string origin = NetManager::extractOrigin(base_path_);
    const std::string movie_dir = extractMovieDirectory(base_path_);
    if (origin.empty() || movie_dir.empty() || movie_dir == origin + "/") {
        return fallbacks;
    }

    const std::string root_url = origin + "/" + file_name;
    const std::string lower_resolved = toLower(resolved_url);
    if (lower_resolved != toLower(root_url) &&
        lower_resolved != toLower(root_url + ".cct") &&
        lower_resolved != toLower(root_url + ".cst")) {
        return fallbacks;
    }

    std::vector<std::string> urls;
    const std::string movie_dir_url = movie_dir + file_name;
    for (auto& fallback : util::getUrlsWithFallbacks(movie_dir_url)) {
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

    auto* mutable_task = const_cast<Task*>(task);
    int byte_count = task->byte_count;
    if (!task->done) {
        ++mutable_task->poll_count;
        byte_count = mutable_task->poll_count;
    }

    const std::string state = task->done ? (task->error_code == 0 ? "Complete" : "Error") : "Loading";
    putProp(props, "URL", lingo::Datum::of(task->url));
    putProp(props, "state", lingo::Datum::of(state));
    putProp(props, "bytesSoFar", lingo::Datum::of(byte_count));
    putProp(props, "bytesTotal", lingo::Datum::of(task->done ? task->byte_count : 0));
    putProp(props, "error", task->error_code == 0
        ? lingo::Datum::of(std::string("OK"))
        : lingo::Datum::of(std::to_string(task->error_code)));
    return props;
}

} // namespace libreshockwave::player::net
