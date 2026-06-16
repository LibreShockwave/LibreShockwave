#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "libreshockwave/player/net/NetManager.hpp"

namespace libreshockwave::player::net {

class QueuedNetProvider final : public NetProvider {
public:
    struct PendingRequest {
        int taskId;
        std::string url;
        std::string method;
        std::optional<std::string> postData;
        std::vector<std::string> fallbacks;
    };

    using FetchCompleteCallback = std::function<void(const std::string&, const std::vector<std::uint8_t>&)>;
    using SatisfiedFetchPredicate = std::function<bool(std::string_view)>;

    explicit QueuedNetProvider(std::string basePath = {});

    void setFetchCompleteCallback(FetchCompleteCallback callback);
    void setSatisfiedFetchPredicate(SatisfiedFetchPredicate predicate);

    [[nodiscard]] int preloadNetThing(std::string url) override;
    [[nodiscard]] int postNetText(std::string url, std::string postData) override;
    [[nodiscard]] int beginMovieNavigation(std::string url);
    void completeMovieNavigationTasks();

    [[nodiscard]] bool netDone(std::optional<int> taskId = std::nullopt) const override;
    [[nodiscard]] std::string netTextResult(std::optional<int> taskId = std::nullopt) const override;
    [[nodiscard]] int netError(std::optional<int> taskId = std::nullopt) const override;
    [[nodiscard]] std::string_view getStreamStatus(std::optional<int> taskId = std::nullopt) const override;
    [[nodiscard]] lingo::Datum getStreamStatusDatum(std::optional<int> taskId = std::nullopt) const override;
    [[nodiscard]] lingo::Datum getStreamStatusDatum(std::string_view url) const override;

    [[nodiscard]] std::optional<std::string> getTaskUrl(int taskId) const;
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

    void onFetchComplete(int taskId, std::vector<std::uint8_t> data);
    void onMovieNavigationComplete(int taskId);
    void onFetchStatusComplete(int taskId, int byteCount);
    void onFetchError(int taskId, int status);

    [[nodiscard]] const std::string& basePath() const;

private:
    struct Task {
        int id;
        std::string url;
        std::optional<std::vector<std::uint8_t>> data;
        int byteCount = 0;
        int errorCode = 0;
        bool done = false;
        std::vector<std::string> fallbackUrls;
        int pollCount = 0;
    };

    [[nodiscard]] Task* getTask(std::optional<int> taskId);
    [[nodiscard]] const Task* getTask(std::optional<int> taskId) const;
    [[nodiscard]] const Task* getTask(std::string_view url) const;
    [[nodiscard]] bool taskMatchesUrl(const Task& task, std::string_view url) const;
    [[nodiscard]] std::vector<std::string> buildTaskLookupKeys(const Task& task) const;
    [[nodiscard]] std::vector<std::string> buildLookupKeys(std::string_view url) const;
    void addLookupCacheKeys(std::vector<std::string>& keys, std::string_view url) const;
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> findCachedData(std::string_view originalUrl,
                                                                          std::string_view resolvedUrl) const;
    void cacheData(std::string_view url, const std::vector<std::uint8_t>& data);
    [[nodiscard]] std::vector<std::string> buildCacheKeys(std::string_view originalUrl,
                                                          std::string_view resolvedUrl) const;
    void addCacheKeys(std::vector<std::string>& keys, std::string_view url) const;
    [[nodiscard]] bool isFetchAlreadySatisfied(std::string_view originalUrl,
                                               std::string_view resolvedUrl,
                                               const std::vector<std::string>& fallbacks) const;
    [[nodiscard]] bool isDirectoryOnlyUrl(std::string_view url) const;
    [[nodiscard]] std::string normalizeLookupUrl(std::string_view rawUrl) const;
    [[nodiscard]] std::string decodeLookupUrl(std::string_view rawUrl) const;
    [[nodiscard]] std::string resolveUrl(std::string_view url) const;
    [[nodiscard]] std::vector<std::string> withMovieDirectoryCastFallbacks(
        std::string_view resolvedUrl,
        std::vector<std::string> fallbacks) const;
    [[nodiscard]] static std::string extractMovieDirectory(std::string_view url);
    [[nodiscard]] lingo::Datum streamStatusDatum(const Task* task) const;

    std::string basePath_;
    std::unordered_map<int, Task> tasks_;
    std::unordered_map<std::string, std::vector<std::uint8_t>> urlCache_;
    std::vector<PendingRequest> pendingRequests_;
    std::deque<int> pendingMovieNavigationTasks_;
    int nextTaskId_ = 1;
    int lastTaskId_ = 0;
    FetchCompleteCallback fetchCompleteCallback_;
    SatisfiedFetchPredicate satisfiedFetchPredicate_;
};

} // namespace libreshockwave::player::net
