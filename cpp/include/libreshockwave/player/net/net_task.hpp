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
    NetTask(int task_id,
            std::string original_url,
            std::string url,
            NetTaskMethod method,
            std::optional<std::string> post_data);

    [[nodiscard]] static NetTask get(int task_id, std::string original_url, std::string url);
    [[nodiscard]] static NetTask post(int task_id, std::string original_url, std::string url, std::string post_data);

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
    void fail(int error_code, std::string error_message);

    [[nodiscard]] std::string_view streamStatus() const;
    [[nodiscard]] std::string toString() const;

private:
    int task_id_;
    std::string original_url_;
    std::string url_;
    NetTaskMethod method_;
    std::optional<std::string> post_data_;
    NetTaskState state_{NetTaskState::Pending};
    std::optional<std::vector<std::uint8_t>> result_;
    int error_code_{0};
    std::optional<std::string> error_message_;
};

} // namespace libreshockwave::player::net
