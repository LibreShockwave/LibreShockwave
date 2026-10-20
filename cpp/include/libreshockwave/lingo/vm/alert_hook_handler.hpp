#pragma once

#include <functional>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/lingo/datum.hpp"

namespace libreshockwave::lingo::vm {

class AlertHookHandler {
public:
    using SkipCallback = std::function<void(const std::string& message)>;
    using HookInvoker = std::function<bool(const std::string& alert_type, const std::string& message)>;

    void setErrorHandlerSkipCallback(SkipCallback callback);
    [[nodiscard]] int getErrorHandlerDepth() const;
    void incrementDepth();
    void decrementDepth();
    [[nodiscard]] bool shouldSkipErrorHandler(std::string_view handler_name, std::initializer_list<Datum> args) const;
    [[nodiscard]] bool shouldSkipErrorHandler(std::string_view handler_name, const std::vector<Datum>& args) const;
    [[nodiscard]] bool shouldSkipErrorHandler(std::string_view handler_name, std::span<const Datum> args) const;
    [[nodiscard]] bool isErrorHandler(std::string_view handler_name) const;
    [[nodiscard]] bool fireAlertHook(std::string_view error_type,
                                     std::string_view error_message,
                                     const HookInvoker& invoke_hook) const;

private:
    int error_handler_depth_{0};
    SkipCallback error_handler_skip_callback_;
};

} // namespace libreshockwave::lingo::vm
