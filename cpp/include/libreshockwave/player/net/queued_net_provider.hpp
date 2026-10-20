#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "libreshockwave/player/net/net_manager.hpp"

namespace libreshockwave::player::net {

class QueuedNetProvider final : public NetProvider {
public:
    struct PendingRequest {
        int task_id;
        std::string url;
        std::string method;
        std::optional<std::string> post_data;
        std::vector<std::string> fallbacks;
    };

    using FetchCompleteCallback = std::function<void(const std::string&, const std::vector<std::uint8_t>&)>;
    using SatisfiedFetchPredicate = std::function<bool(std::string_view)>;

    explicit QueuedNetProvider(std::string base_path = {});

    void setFetchCompleteCallback(FetchCompleteCallback callback);
    void setSatisfiedFetchPredicate(SatisfiedFetchPredicate predicate);

    [[nodiscard]] int preloadNetThing(std::string url) override;
    [[nodiscard]] int postNetText(std::string url, std::string post_data) override;
    [[nodiscard]] bool hasPendingCastLoads() const override;
    [[nodiscard]] int beginMovieNavigation(std::string url);
    void completeMovieNavigationTasks();

    [[nodiscard]] bool netDone(std::optional<int> task_id = std::nullopt) const override;
    [[nodiscard]] std::string netTextResult(std::optional<int> task_id = std::nullopt) const override;
    [[nodiscard]] int netError(std::optional<int> task_id = std::nullopt) const override;
    [[nodiscard]] std::string_view getStreamStatus(std::optional<int> task_id = std::nullopt) const override;
    [[nodiscard]] lingo::Datum getStreamStatusDatum(std::optional<int> task_id = std::nullopt) const override;
    [[nodiscard]] lingo::Datum getStreamStatusDatum(std::string_view url) const override;

    [[nodiscard]] std::optional<std::string> getTaskUrl(int task_id) const;
    [[nodiscard]] std::string getDebugStatus() const;
    [[nodiscard]] int taskCount() const;
    [[nodiscard]] int lastTaskId() const;
    [[nodiscard]] int pendingRequestCount() const;
    [[nodiscard]] int pendingMovieNavigationTaskCount() const;
    [[nodiscard]] std::vector<PendingRequest> pendingMovieNavigationRequests() const;
    [[nodiscard]] bool latestTaskDone() const;
    [[nodiscard]] const std::vector<PendingRequest>& pendingRequests() const;
    [[nodiscard]] const PendingRequest* getRequest(int index) const;
    void drainPendingRequests();

    void onFetchComplete(int task_id, std::vector<std::uint8_t> data);
    void onMovieNavigationComplete(int task_id);
    void onFetchStatusComplete(int task_id, int byte_count);
    void onFetchError(int task_id, int status);

    [[nodiscard]] const std::string& basePath() const;

private:
    struct Task {
        int id;
        std::string url;
        std::optional<std::vector<std::uint8_t>> data;
        int byte_count = 0;
        int error_code = 0;
        bool done = false;
        std::vector<std::string> fallback_urls;
        int poll_count = 0;
    };

    [[nodiscard]] Task* getTask(std::optional<int> task_id);
    [[nodiscard]] const Task* getTask(std::optional<int> task_id) const;
    [[nodiscard]] const Task* getTask(std::string_view url) const;
    void indexTask(const Task& task);
    void trackPendingCastLoad(const Task& task);
    void markTaskDone(Task& task);
    [[nodiscard]] std::vector<std::string> buildTaskLookupKeys(const Task& task) const;
    [[nodiscard]] std::vector<std::string> buildLookupKeys(std::string_view url) const;
    void addLookupCacheKeys(std::vector<std::string>& keys, std::string_view url) const;
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> findCachedData(std::string_view original_url,
                                                                          std::string_view resolved_url) const;
    void cacheData(std::string_view url, const std::vector<std::uint8_t>& data);
    [[nodiscard]] std::vector<std::string> buildCacheKeys(std::string_view original_url,
                                                          std::string_view resolved_url) const;
    void addCacheKeys(std::vector<std::string>& keys, std::string_view url) const;
    [[nodiscard]] bool isFetchAlreadySatisfied(std::string_view original_url,
                                               std::string_view resolved_url,
                                               const std::vector<std::string>& fallbacks) const;
    [[nodiscard]] bool isDirectoryOnlyUrl(std::string_view url) const;
    [[nodiscard]] std::string normalizeLookupUrl(std::string_view raw_url) const;
    [[nodiscard]] std::string decodeLookupUrl(std::string_view raw_url) const;
    [[nodiscard]] std::string resolveUrl(std::string_view url) const;
    [[nodiscard]] std::vector<std::string> withMovieDirectoryCastFallbacks(
        std::string_view resolved_url,
        std::vector<std::string> fallbacks) const;
    [[nodiscard]] static std::string extractMovieDirectory(std::string_view url);
    [[nodiscard]] lingo::Datum streamStatusDatum(const Task* task) const;

    std::string base_path_;
    std::unordered_map<int, Task> tasks_;
    std::unordered_map<std::string, int> latest_task_by_exact_url_;
    std::unordered_map<std::string, int> latest_task_by_lookup_key_;
    std::unordered_map<std::string, std::vector<std::uint8_t>> url_cache_;
    std::vector<PendingRequest> pending_requests_;
    std::deque<int> pending_movie_navigation_tasks_;
    int pending_external_cast_loads_ = 0;
    int next_task_id_ = 1;
    int last_task_id_ = 0;
    FetchCompleteCallback fetch_complete_callback_;
    SatisfiedFetchPredicate satisfied_fetch_predicate_;
};

} // namespace libreshockwave::player::net
