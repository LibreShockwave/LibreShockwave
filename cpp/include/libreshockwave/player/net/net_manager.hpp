#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/player/net/net_task.hpp"

namespace libreshockwave::player::net {

class NetProvider {
public:
    virtual ~NetProvider() = default;

    [[nodiscard]] virtual int preloadNetThing(std::string url) = 0;
    [[nodiscard]] virtual int postNetText(std::string url, std::string post_data) = 0;
    // External casts are loaded asynchronously by the movie's cast loader.
    // Multiuser notifications that depend on those casts must not overtake
    // their fetches, even after the request has left the pending queue.
    [[nodiscard]] virtual bool hasPendingCastLoads() const { return false; }

    [[nodiscard]] virtual bool netDone(std::optional<int> task_id = std::nullopt) const = 0;
    [[nodiscard]] virtual std::string netTextResult(std::optional<int> task_id = std::nullopt) const = 0;
    [[nodiscard]] virtual int netError(std::optional<int> task_id = std::nullopt) const = 0;
    [[nodiscard]] virtual std::string_view getStreamStatus(std::optional<int> task_id = std::nullopt) const = 0;
    [[nodiscard]] virtual lingo::Datum getStreamStatusDatum(std::optional<int> task_id = std::nullopt) const = 0;
    [[nodiscard]] virtual lingo::Datum getStreamStatusDatum(std::string_view url) const = 0;
};

class NetManager : public NetProvider {
public:
    struct LoadResult {
        std::optional<std::vector<std::uint8_t>> data;
        int error_code{0};
        std::string error_message;

        [[nodiscard]] static LoadResult success(std::vector<std::uint8_t> data);
        [[nodiscard]] static LoadResult failure(int error_code, std::string error_message);
    };

    using FetchHandler = std::function<LoadResult(const NetTask&)>;
    using CompletionCallback = std::function<void(const std::string&, const std::vector<std::uint8_t>&)>;
    using TaskCallback = std::function<void(const NetTask&)>;

    void setBasePath(std::string base_path);
    [[nodiscard]] const std::string& basePath() const;
    [[nodiscard]] const std::string& getBasePath() const;

    void setLocalHttpRoot(std::string root);
    [[nodiscard]] const std::string& localHttpRoot() const;

    void setRemoteFetchEnabled(bool enabled);
    [[nodiscard]] bool remoteFetchEnabled() const;

    void setFetchHandler(FetchHandler handler);
    void setCompletionCallback(CompletionCallback callback);
    void setTaskCallback(TaskCallback callback);

    [[nodiscard]] int preloadNetThing(std::string url) override;
    [[nodiscard]] int postNetText(std::string url, std::string post_data) override;
    [[nodiscard]] bool hasPendingCastLoads() const override;

    [[nodiscard]] bool netDone(std::optional<int> task_id = std::nullopt) const override;
    [[nodiscard]] std::string netTextResult(std::optional<int> task_id = std::nullopt) const override;
    [[nodiscard]] int netError(std::optional<int> task_id = std::nullopt) const override;
    [[nodiscard]] std::string_view getStreamStatus(std::optional<int> task_id = std::nullopt) const override;
    [[nodiscard]] lingo::Datum getStreamStatusDatum(std::optional<int> task_id = std::nullopt) const override;
    [[nodiscard]] lingo::Datum getStreamStatusDatum(std::string_view url) const override;
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> getNetBytes(std::optional<int> task_id = std::nullopt) const;

    [[nodiscard]] NetTask* getTask(std::optional<int> task_id = std::nullopt);
    [[nodiscard]] const NetTask* getTask(std::optional<int> task_id = std::nullopt) const;
    [[nodiscard]] const std::unordered_map<int, NetTask>& tasks() const;

    void cacheData(std::string url, std::vector<std::uint8_t> data);
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> getCachedData(std::string_view url) const;
    [[nodiscard]] const std::unordered_map<std::string, std::vector<std::uint8_t>>& urlCache() const;
    void clear();
    void shutdown();

    [[nodiscard]] static std::string resolveUrl(std::string_view url);
    [[nodiscard]] static std::string cacheKeyForUrl(std::string_view url);
    [[nodiscard]] static std::string extractUrlPath(std::string_view url);
    [[nodiscard]] static std::string extractOrigin(std::string_view url);

private:
    [[nodiscard]] NetTask& createGetTask(std::string url);
    [[nodiscard]] NetTask& createPostTask(std::string url, std::string post_data);
    void executeTask(NetTask& task, bool use_cache);
    void completeTask(NetTask& task,
                      std::vector<std::uint8_t> data,
                      bool cache_result,
                      std::string_view cache_url);
    void notifyCompletion(const std::string& url, const std::vector<std::uint8_t>& data) const;
    void notifyTask(const NetTask& task) const;

    std::unordered_map<int, NetTask> tasks_;
    std::unordered_map<std::string, std::vector<std::uint8_t>> url_cache_;
    int next_task_id_{1};
    int last_task_id_{0};
    std::string base_path_;
    std::string local_http_root_;
    bool remote_fetch_enabled_{true};
    FetchHandler fetch_handler_;
    CompletionCallback completion_callback_;
    TaskCallback task_callback_;
};

} // namespace libreshockwave::player::net
