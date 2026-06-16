#include "libreshockwave/player/net/NetTask.hpp"

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

NetTask::NetTask(int taskId,
                 std::string originalUrl,
                 std::string url,
                 NetTaskMethod method,
                 std::optional<std::string> postData)
    : taskId_(taskId),
      originalUrl_(std::move(originalUrl)),
      url_(std::move(url)),
      method_(method),
      postData_(std::move(postData)) {}

NetTask NetTask::get(int taskId, std::string originalUrl, std::string url) {
    return NetTask(taskId, std::move(originalUrl), std::move(url), NetTaskMethod::Get, std::nullopt);
}

NetTask NetTask::post(int taskId, std::string originalUrl, std::string url, std::string postData) {
    return NetTask(taskId, std::move(originalUrl), std::move(url), NetTaskMethod::Post, std::move(postData));
}

int NetTask::taskId() const { return taskId_; }
const std::string& NetTask::url() const { return url_; }
const std::string& NetTask::originalUrl() const { return originalUrl_; }
NetTaskMethod NetTask::method() const { return method_; }
const std::optional<std::string>& NetTask::postData() const { return postData_; }
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

int NetTask::errorCode() const { return errorCode_; }
const std::optional<std::string>& NetTask::errorMessage() const { return errorMessage_; }

void NetTask::markInProgress() {
    state_ = NetTaskState::InProgress;
}

void NetTask::complete(std::vector<std::uint8_t> data) {
    result_ = std::move(data);
    state_ = NetTaskState::Completed;
    errorCode_ = 0;
}

void NetTask::fail(int errorCode, std::string errorMessage) {
    errorCode_ = errorCode;
    errorMessage_ = std::move(errorMessage);
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
    out << "NetTask{id=" << taskId_ << ", util=" << url_
        << ", method=" << name(method_) << ", state=" << name(state_) << "}";
    return out.str();
}

} // namespace libreshockwave::player::net
