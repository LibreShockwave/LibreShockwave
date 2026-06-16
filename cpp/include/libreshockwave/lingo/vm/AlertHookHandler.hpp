#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/lingo/Datum.hpp"

namespace libreshockwave::lingo::vm {

class AlertHookHandler {
public:
    using SkipCallback = std::function<void(const std::string& message)>;
    using HookInvoker = std::function<bool(const std::string& alertType, const std::string& message)>;

    void setErrorHandlerSkipCallback(SkipCallback callback);
    [[nodiscard]] int getErrorHandlerDepth() const;
    void incrementDepth();
    void decrementDepth();
    [[nodiscard]] bool shouldSkipErrorHandler(std::string_view handlerName, const std::vector<Datum>& args) const;
    [[nodiscard]] bool isErrorHandler(std::string_view handlerName) const;
    [[nodiscard]] bool fireAlertHook(std::string_view errorType,
                                     std::string_view errorMessage,
                                     const HookInvoker& invokeHook) const;

private:
    int errorHandlerDepth_{0};
    SkipCallback errorHandlerSkipCallback_;
};

} // namespace libreshockwave::lingo::vm
