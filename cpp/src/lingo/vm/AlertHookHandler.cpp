#include "libreshockwave/lingo/vm/AlertHookHandler.hpp"

#include <cctype>
#include <utility>

#include "libreshockwave/lingo/vm/datum/DatumFormatter.hpp"

namespace libreshockwave::lingo::vm {
namespace {

bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
            std::tolower(static_cast<unsigned char>(rhs[index]))) {
            return false;
        }
    }
    return true;
}

} // namespace

void AlertHookHandler::setErrorHandlerSkipCallback(SkipCallback callback) {
    errorHandlerSkipCallback_ = std::move(callback);
}

int AlertHookHandler::getErrorHandlerDepth() const {
    return errorHandlerDepth_;
}

void AlertHookHandler::incrementDepth() {
    ++errorHandlerDepth_;
}

void AlertHookHandler::decrementDepth() {
    --errorHandlerDepth_;
}

bool AlertHookHandler::shouldSkipErrorHandler(std::string_view handlerName, const std::vector<Datum>& args) const {
    if (!isErrorHandler(handlerName)) {
        return false;
    }

    const std::string handler(handlerName);
    if (errorHandlerDepth_ > 0) {
        if (errorHandlerSkipCallback_) {
            errorHandlerSkipCallback_("SKIP:" + handler + " depth=" + std::to_string(errorHandlerDepth_));
        }
        return true;
    }

    if (errorHandlerSkipCallback_) {
        std::string message = "ENTER:" + handler + " depth=" + std::to_string(errorHandlerDepth_);
        if (args.size() > 1) {
            message += " msg=" + datum::format(args[1]);
        }
        errorHandlerSkipCallback_(message);
    }
    return false;
}

bool AlertHookHandler::isErrorHandler(std::string_view handlerName) const {
    return equalsIgnoreCase(handlerName, "alertHook");
}

bool AlertHookHandler::fireAlertHook(std::string_view errorType,
                                     std::string_view errorMessage,
                                     const HookInvoker& invokeHook) const {
    if (errorHandlerDepth_ > 0 || !invokeHook) {
        return false;
    }

    try {
        return invokeHook(std::string(errorType), std::string(errorMessage));
    } catch (...) {
        return false;
    }
}

} // namespace libreshockwave::lingo::vm
