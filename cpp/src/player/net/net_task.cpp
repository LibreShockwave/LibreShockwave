#include "libreshockwave/player/net/net_task.hpp"

#include <sstream>
#include <utility>

namespace libreshockwave::player::net {

std::string_view name(NetTaskMethod method) {
    switch (method) {
        case NetTaskMethod::Get: return "GET";
        case NetTaskMethod::Post: return "POST";
    }
    return "GET";
}

std::string_view name(NetTaskState state) {
    switch (state) {
        case NetTaskState::Pending: return "PENDING";
        case NetTaskState::InProgress: return "IN_PROGRESS";
        case NetTaskState::Completed: return "COMPLETED";
        case NetTaskState::Failed: return "FAILED";
    }
    return "FAILED";
}

NetTask::NetTask(int task_id,
                 std::string original_url,
                 std::string url,
                 NetTaskMethod method,
                 std::optional<std::string> post_data)
    : task_id_(task_id),
      original_url_(std::move(original_url)),
      url_(std::move(url)),
      method_(method),
      post_data_(std::move(post_data)) {}

NetTask NetTask::get(int task_id, std::string original_url, std::string url) {
    return NetTask(task_id, std::move(original_url), std::move(url), NetTaskMethod::Get, std::nullopt);
}

NetTask NetTask::post(int task_id, std::string original_url, std::string url, std::string post_data) {
    return NetTask(task_id, std::move(original_url), std::move(url), NetTaskMethod::Post, std::move(post_data));
}

int NetTask::taskId() const { return task_id_; }
const std::string& NetTask::url() const { return url_; }
const std::string& NetTask::originalUrl() const { return original_url_; }
NetTaskMethod NetTask::method() const { return method_; }
const std::optional<std::string>& NetTask::postData() const { return post_data_; }
NetTaskState NetTask::state() const { return state_; }

bool NetTask::isDone() const {
    return state_ == NetTaskState::Completed || state_ == NetTaskState::Failed;
}

const std::optional<std::vector<std::uint8_t>>& NetTask::result() const { return result_; }

std::string NetTask::resultAsString() const {
    if (!result_.has_value()) {
        return "";
    }
    return std::string(result_->begin(), result_->end());
}

int NetTask::errorCode() const { return error_code_; }
const std::optional<std::string>& NetTask::errorMessage() const { return error_message_; }

void NetTask::markInProgress() {
    state_ = NetTaskState::InProgress;
}

void NetTask::complete(std::vector<std::uint8_t> data) {
    result_ = std::move(data);
    state_ = NetTaskState::Completed;
    error_code_ = 0;
}

void NetTask::fail(int error_code, std::string error_message) {
    error_code_ = error_code;
    error_message_ = std::move(error_message);
    state_ = NetTaskState::Failed;
}

std::string_view NetTask::streamStatus() const {
    switch (state_) {
        case NetTaskState::Pending: return "Connecting";
        case NetTaskState::InProgress: return "Loading";
        case NetTaskState::Completed: return "Complete";
        case NetTaskState::Failed: return "Error";
    }
    return "Error";
}

std::string NetTask::toString() const {
    std::ostringstream out;
    out << "NetTask{id=" << task_id_ << ", util=" << url_
        << ", method=" << name(method_) << ", state=" << name(state_) << "}";
    return out.str();
}

} // namespace libreshockwave::player::net
