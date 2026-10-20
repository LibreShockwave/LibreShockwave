#include "libreshockwave/player/debug/lifecycle_diagnostics.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace libreshockwave::player::debug {
namespace {

std::atomic_bool g_enabled{false};

std::string lowerAscii(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

std::string safe(std::string_view value) {
    if (value.empty()) {
        return "\"\"";
    }
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (const char ch : value) {
        result.push_back(ch == '\n' || ch == '\r' ? ' ' : ch);
    }
    result.push_back('"');
    return result;
}

std::string describeDatum(const lingo::Datum& datum) {
    try {
        if (datum.isVoid()) {
            return safe("<Void>");
        }
        if (datum.isString()) {
            return safe("\"" + datum.stringValue() + "\"");
        }
        if (const auto* symbol = datum.asSymbol()) {
            return safe("#" + symbol->name);
        }
        return safe(datum.stringValue());
    } catch (...) {
        return "<unprintable>";
    }
}

std::string describeArgs(const std::vector<lingo::Datum>& args) {
    if (args.empty()) {
        return "[]";
    }
    std::ostringstream out;
    out << '[';
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index > 0) {
            out << ", ";
        }
        out << describeDatum(args[index]);
    }
    out << ']';
    return out.str();
}

void log(std::string_view message) {
    std::cout << "[Lifecycle] " << message << '\n';
}

} // namespace

void LifecycleDiagnostics::setEnabled(bool enabled) {
    g_enabled.store(enabled, std::memory_order_relaxed);
}

bool LifecycleDiagnostics::isEnabled() {
    return g_enabled.load(std::memory_order_relaxed);
}

bool LifecycleDiagnostics::isInterestingHandler(std::string_view handler_name) {
    if (handler_name.empty()) {
        return false;
    }
    const auto lower = lowerAscii(handler_name);
    return lower == "createthread" ||
           lower == "initthread" ||
           lower == "closethread" ||
           lower == "createobject" ||
           lower == "removeobject" ||
           lower == "getobject" ||
           lower == "objectexists" ||
           lower == "showprogram" ||
           lower == "hideprogram" ||
           lower == "createvisualizer" ||
           lower == "removevisualizer" ||
           lower == "enterroom" ||
           lower == "leaveroom" ||
           lower == "changeroom";
}

void LifecycleDiagnostics::logHandlerEnter(const lingo::vm::TraceListener::HandlerInfo& info) {
    if (!isEnabled() || !isInterestingHandler(info.handler_name)) {
        return;
    }
    log("enter handler=" + safe(info.handler_name) +
        " script=" + safe(info.script_display_name) +
        " receiver=" + describeDatum(info.receiver) +
        " args=" + describeArgs(info.arguments));
}

void LifecycleDiagnostics::logHandlerExit(const lingo::vm::TraceListener::HandlerInfo& info,
                                          const lingo::Datum& return_value) {
    if (!isEnabled() || !isInterestingHandler(info.handler_name)) {
        return;
    }
    log("exit handler=" + safe(info.handler_name) +
        " script=" + safe(info.script_display_name) +
        " result=" + describeDatum(return_value));
}

void LifecycleDiagnostics::logExternalCastLoaded(int cast_lib_number, std::string_view file_name) {
    if (!isEnabled()) {
        return;
    }
    log("externalCastLoaded cast=" + std::to_string(cast_lib_number) + " file=" + safe(file_name));
}

void LifecycleDiagnostics::logSpriteRemoved(std::string_view reason, const sprite::SpriteState& state) {
    if (!isEnabled()) {
        return;
    }
    log(std::string(reason) +
        " channel=" + std::to_string(state.channel()) +
        " dynamic=" + (state.hasDynamicMember() ? "true" : "false") +
        " puppet=" + (state.isPuppet() ? "true" : "false") +
        " cast=" + std::to_string(state.effectiveCastLib()) +
        " member=" + std::to_string(state.effectiveCastMember()) +
        " scripts=" + std::to_string(state.scriptInstanceList().size()) +
        " loc=" + std::to_string(state.locH()) + "," + std::to_string(state.locV()) + "," +
            std::to_string(state.locZ()) +
        " size=" + std::to_string(state.width()) + "x" + std::to_string(state.height()));
}

void LifecycleDiagnostics::logSpriteMemberCleared(std::string_view reason,
                                                  const sprite::SpriteState& state,
                                                  int retired_cast_lib,
                                                  int retired_member_num) {
    if (!isEnabled()) {
        return;
    }
    log(std::string(reason) +
        " channel=" + std::to_string(state.channel()) +
        " retired=" + std::to_string(retired_cast_lib) + ":" + std::to_string(retired_member_num) +
        " fallback=" + std::to_string(state.effectiveCastLib()) + ":" +
            std::to_string(state.effectiveCastMember()) +
        " puppet=" + (state.isPuppet() ? "true" : "false") +
        " scripts=" + std::to_string(state.scriptInstanceList().size()));
}

void LifecycleDiagnostics::logSpriteEmptyOverride(std::string_view reason, const sprite::SpriteState& state) {
    if (!isEnabled()) {
        return;
    }
    log(std::string(reason) +
        " channel=" + std::to_string(state.channel()) +
        " puppet=" + (state.isPuppet() ? "true" : "false") +
        " scripts=" + std::to_string(state.scriptInstanceList().size()));
}

void LifecycleDiagnostics::logReleasedEmptyChannel(std::string_view reason, const sprite::SpriteState& state) {
    if (!isEnabled()) {
        return;
    }
    log(std::string(reason) +
        " channel=" + std::to_string(state.channel()) +
        " visible=" + (state.isVisible() ? "true" : "false") +
        " size=" + std::to_string(state.width()) + "x" + std::to_string(state.height()) +
        " scripts=" + std::to_string(state.scriptInstanceList().size()));
}

void LifecycleDiagnostics::logError(std::string_view message, std::string_view error_detail) {
    if (!isEnabled()) {
        return;
    }
    const auto details = !message.empty() ? message : error_detail;
    log("error " + safe(details));
}

} // namespace libreshockwave::player::debug
