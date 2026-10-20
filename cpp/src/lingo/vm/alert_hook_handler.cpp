#include "libreshockwave/lingo/vm/alert_hook_handler.hpp"

#include <cctype>
#include <utility>

#include "libreshockwave/lingo/vm/datum/datum_formatter.hpp"

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
    error_handler_skip_callback_ = std::move(callback);
}

int AlertHookHandler::getErrorHandlerDepth() const {
    return error_handler_depth_;
}

void AlertHookHandler::incrementDepth() {
    ++error_handler_depth_;
}

void AlertHookHandler::decrementDepth() {
    --error_handler_depth_;
}

bool AlertHookHandler::shouldSkipErrorHandler(std::string_view handler_name, std::span<const Datum> args) const {
    if (!isErrorHandler(handler_name)) {
        return false;
    }

    const std::string handler(handler_name);
    if (error_handler_depth_ > 0) {
        if (error_handler_skip_callback_) {
            error_handler_skip_callback_("SKIP:" + handler + " depth=" + std::to_string(error_handler_depth_));
        }
        return true;
    }

    if (error_handler_skip_callback_) {
        std::string message = "ENTER:" + handler + " depth=" + std::to_string(error_handler_depth_);
        if (args.size() > 1) {
            message += " msg=" + datum::format(args[1]);
        }
        error_handler_skip_callback_(message);
    }
    return false;
}

bool AlertHookHandler::shouldSkipErrorHandler(std::string_view handler_name, const std::vector<Datum>& args) const {
    return shouldSkipErrorHandler(handler_name, std::span<const Datum>(args));
}

bool AlertHookHandler::shouldSkipErrorHandler(std::string_view handler_name, std::initializer_list<Datum> args) const {
    return shouldSkipErrorHandler(handler_name, std::span<const Datum>(args.begin(), args.size()));
}

bool AlertHookHandler::isErrorHandler(std::string_view handler_name) const {
    return equalsIgnoreCase(handler_name, "alertHook");
}

bool AlertHookHandler::fireAlertHook(std::string_view error_type,
                                     std::string_view error_message,
                                     const HookInvoker& invoke_hook) const {
    if (error_handler_depth_ > 0 || !invoke_hook) {
        return false;
    }

    try {
        return invoke_hook(std::string(error_type), std::string(error_message));
    } catch (...) {
        return false;
    }
}

} // namespace libreshockwave::lingo::vm
