#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace libreshockwave::player::net {

enum class NetTaskMethod {
    Get,
    Post
};

enum class NetTaskState {
    Pending,
    InProgress,
    Completed,
    Failed
};

[[nodiscard]] std::string_view name(NetTaskMethod method);
[[nodiscard]] std::string_view name(NetTaskState state);

class NetTask {
public:
    NetTask(int taskId,
            std::string originalUrl,
            std::string url,
            NetTaskMethod method,
            std::optional<std::string> postData);

    [[nodiscard]] static NetTask get(int taskId, std::string originalUrl, std::string url);
    [[nodiscard]] static NetTask post(int taskId, std::string originalUrl, std::string url, std::string postData);

    [[nodiscard]] int taskId() const;
    [[nodiscard]] const std::string& url() const;
    [[nodiscard]] const std::string& originalUrl() const;
    [[nodiscard]] NetTaskMethod method() const;
    [[nodiscard]] const std::optional<std::string>& postData() const;
    [[nodiscard]] NetTaskState state() const;
    [[nodiscard]] bool isDone() const;
    [[nodiscard]] const std::optional<std::vector<std::uint8_t>>& result() const;
    [[nodiscard]] std::string resultAsString() const;
    [[nodiscard]] int errorCode() const;
    [[nodiscard]] const std::optional<std::string>& errorMessage() const;

    void markInProgress();
    void complete(std::vector<std::uint8_t> data);
    void fail(int errorCode, std::string errorMessage);

    [[nodiscard]] std::string_view streamStatus() const;
    [[nodiscard]] std::string toString() const;

private:
    int taskId_;
    std::string originalUrl_;
    std::string url_;
    NetTaskMethod method_;
    std::optional<std::string> postData_;
    NetTaskState state_{NetTaskState::Pending};
    std::optional<std::vector<std::uint8_t>> result_;
    int errorCode_{0};
    std::optional<std::string> errorMessage_;
};

} // namespace libreshockwave::player::net
