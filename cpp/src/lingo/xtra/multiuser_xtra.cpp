#include "libreshockwave/lingo/xtra/multiuser_xtra.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace libreshockwave::lingo::xtra {
namespace {

// A server burst can deliver hundreds of messages within a few milliseconds.
// Draining them all inside one frame would freeze the movie until the VM's
// tick deadline throws, so delivery is budgeted per frame instead.
/// Messages handed to the movie's handler per frame when the movie did not
/// call setNetBufferLimits (bufferMax overrides this).
constexpr std::size_t k_default_max_messages_per_frame = 100;
/// Hard ceiling: even a movie-tuned bufferMax must not exceed this, or a
/// flood could stall a frame again.
constexpr std::size_t k_max_messages_per_frame = 512;
/// Keep an unread burst bounded as well.  Once this queue is full, the socket
/// bridge stops being polled so TCP backpressure can slow the sender.
constexpr std::size_t k_max_queued_messages = 2048;

std::string lowerAscii(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

Datum messagePropList(const MultiuserNetBridge::NetMessage& message) {
    auto result = Datum::propList();
    auto& props = result.propListValue();
    props.put(Datum::symbol("errorCode"), Datum::of(message.error_code));
    props.put(Datum::symbol("senderID"), Datum::of(message.sender_id));
    props.put(Datum::symbol("subject"), Datum::of(message.subject));
    props.put(Datum::symbol("content"), message.content);
    return result;
}

std::string datumText(const Datum& value) {
    if (value.isVoid()) {
        return {};
    }
    if (const auto* symbol = value.asSymbol()) {
        return symbol->name;
    }
    try {
        return value.stringValue();
    } catch (...) {
        return {};
    }
}

Datum propValue(const Datum::PropList& props, std::string_view name) {
    return props.get(Datum::symbol(std::string(name)));
}

std::string propText(const Datum::PropList& props, std::string_view first, std::string_view second) {
    auto value = propValue(props, first);
    if (value.isVoid()) {
        value = propValue(props, second);
    }
    return datumText(value);
}

int modeValue(const Datum& value, int fallback) {
    if (value.isVoid()) {
        return fallback;
    }
    if (const auto* symbol = value.asSymbol()) {
        const std::string mode = lowerAscii(symbol->name);
        if (mode == "binary" || mode == "smus") {
            return 0;
        }
        if (mode == "text") {
            return 1;
        }
    }
    try {
        return value.intValue();
    } catch (...) {
        return fallback;
    }
}

} // namespace

MultiuserXtra::MultiuserXtra(MultiuserNetBridge* bridge,
                             ScriptCallback callback,
                             MessageDispatchPredicate message_dispatch_predicate)
    : bridge_(bridge),
      script_callback_(std::move(callback)),
      message_dispatch_predicate_(std::move(message_dispatch_predicate)) {}

std::string MultiuserXtra::name() const {
    return "Multiuser";
}

int MultiuserXtra::createInstance(const std::vector<Datum>&) {
    const int id = next_instance_id_++;
    instances_.emplace(id, InstanceState{});
    return id;
}

void MultiuserXtra::destroyInstance(int instance_id) {
    const auto erased = instances_.erase(instance_id);
    if (erased > 0 && bridge_ != nullptr) {
        bridge_->requestDisconnect(instance_id);
        bridge_->destroyInstance(instance_id);
    }
}

Datum MultiuserXtra::callHandler(int instance_id,
                                 std::string_view handler_name,
                                 const std::vector<Datum>& args) {
    const auto found = instances_.find(instance_id);
    if (found == instances_.end()) {
        return Datum::voidValue();
    }

    auto& state = found->second;
    const std::string method = lowerAscii(handler_name);
    if (method == "setnetbufferlimits") {
        return setNetBufferLimits(state, args);
    }
    if (method == "setnetmessagehandler") {
        return setNetMessageHandler(state, args);
    }
    if (method == "connecttonetserver") {
        return connectToNetServer(instance_id, state, args);
    }
    if (method == "sendnetmessage") {
        return sendNetMessage(instance_id, state, args);
    }
    if (method == "getnetmessage") {
        return getNetMessage(state);
    }
    if (method == "checknetmessages") {
        return checkNetMessages(instance_id, state, args);
    }
    if (method == "getnumberwaitingnetmessages") {
        return getNumberWaitingNetMessages(instance_id, state);
    }
    if (method == "getneterrorstring") {
        return getNetErrorString(args);
    }
    return Datum::voidValue();
}

Datum MultiuserXtra::getProperty(int, std::string_view) const {
    return Datum::voidValue();
}

void MultiuserXtra::setProperty(int, std::string_view, const Datum&) {}

void MultiuserXtra::tick() {
    for (auto& entry : instances_) {
        auto& state = entry.second;
        if (state.callback_handler.empty() || !state.callback_target.has_value()) {
            continue;
        }

        pollIntoQueue(entry.first, state);
        // Deliver a bounded batch per frame.  Each delivery runs the movie's
        // Lingo handler, which is expensive; a flood must stretch across
        // frames, not stall one frame.  Surplus messages carry over.
        const std::size_t max_this_frame = std::min(
            state.buffer_max > 0 ? static_cast<std::size_t>(state.buffer_max)
                                : k_default_max_messages_per_frame,
            k_max_messages_per_frame);
        std::size_t processed = 0;
        while (!state.message_queue.empty() && processed < max_this_frame) {
            if (message_dispatch_predicate_ && !message_dispatch_predicate_()) {
                break;
            }
            state.current_message = std::move(state.message_queue.front());
            state.message_queue.pop_front();
            ++processed;
            invokeCallback(state);
        }
        state.current_message.reset();
    }
}

Datum MultiuserXtra::setNetBufferLimits(InstanceState& state, const std::vector<Datum>& args) {
    if (args.size() >= 3) {
        state.buffer_min = args[0].intValue();
        state.buffer_max = args[1].intValue();
        state.buffer_urgency = args[2].intValue();
    }
    return Datum::of(0);
}

Datum MultiuserXtra::setNetMessageHandler(InstanceState& state, const std::vector<Datum>& args) {
    if (args.size() >= 2) {
        if (args[0].isVoid() || args[1].isVoid()) {
            state.callback_handler.clear();
            state.callback_target.reset();
        } else {
            if (const auto* symbol = args[0].asSymbol()) {
                state.callback_handler = symbol->name;
            } else {
                state.callback_handler = args[0].stringValue();
            }
            state.callback_target = args[1];
        }
    }
    return Datum::of(0);
}

Datum MultiuserXtra::connectToNetServer(int instance_id, InstanceState& state, const std::vector<Datum>& args) {
    int mode = 1;
    state.connect_options = {};
    if (args.size() >= 3 && args[2].isPropList()) {
        state.host = datumText(args[0]);
        state.port = args[1].intValue();
        const auto& props = args[2].propListValue();
        state.connect_options.user_name = propText(props, "userID", "userid");
        state.connect_options.password = propText(props, "password", "passWord");
        state.connect_options.movie_id = propText(props, "movieID", "movieid");
        mode = args.size() >= 4 ? modeValue(args[3], 0) : 0;
        state.connect_options.encryption_key = args.size() >= 5 ? datumText(args[4]) : std::string();
        if (bridge_ != nullptr) {
            bridge_->requestConnect(instance_id, state.host, state.port, mode, state.connect_options);
        }
    } else if (args.size() >= 4) {
        state.connect_options.user_name = datumText(args[0]);
        state.connect_options.password = datumText(args[1]);
        state.host = args[2].stringValue();
        state.port = args[3].intValue();
        state.connect_options.movie_id = args.size() >= 5 ? datumText(args[4]) : std::string();
        mode = args.size() >= 6 ? modeValue(args[5], 0) : 0;
        state.connect_options.encryption_key = args.size() >= 7 ? datumText(args[6]) : std::string();
        if (bridge_ != nullptr) {
            bridge_->requestConnect(instance_id, state.host, state.port, mode, state.connect_options);
        }
    }
    return Datum::of(0);
}

Datum MultiuserXtra::sendNetMessage(int instance_id, InstanceState&, const std::vector<Datum>& args) {
    if (args.size() >= 3 && bridge_ != nullptr) {
        bridge_->requestSend(instance_id, args[0], datumText(args[1]), args[2]);
    }
    return Datum::of(0);
}

Datum MultiuserXtra::getNetMessage(const InstanceState& state) const {
    if (!state.current_message.has_value()) {
        return Datum::voidValue();
    }
    return messagePropList(*state.current_message);
}

Datum MultiuserXtra::checkNetMessages(int instance_id, InstanceState& state, const std::vector<Datum>& args) {
    const int requested_count = args.empty() ? 1 : args[0].intValue();
    const std::size_t count = static_cast<std::size_t>(std::max(0, requested_count));
    pollIntoQueue(instance_id, state);

    int processed = 0;
    while (static_cast<std::size_t>(processed) < std::min(count, k_max_messages_per_frame) &&
           !state.message_queue.empty()) {
        if (message_dispatch_predicate_ && !message_dispatch_predicate_()) {
            break;
        }
        state.current_message = std::move(state.message_queue.front());
        state.message_queue.pop_front();
        ++processed;
        if (!state.callback_handler.empty() && state.callback_target.has_value()) {
            invokeCallback(state);
        }
    }

    state.current_message.reset();
    return Datum::of(processed);
}

Datum MultiuserXtra::getNumberWaitingNetMessages(int instance_id, InstanceState& state) {
    pollIntoQueue(instance_id, state);
    return Datum::of(static_cast<int>(state.message_queue.size()));
}

Datum MultiuserXtra::getNetErrorString(const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::of(std::string());
    }
    switch (args[0].intValue()) {
        case 0: return Datum::of(std::string("No error"));
        case -1: return Datum::of(std::string("Memory allocation error"));
        case -2: return Datum::of(std::string("Network error"));
        case -3: return Datum::of(std::string("Connection refused"));
        case -4: return Datum::of(std::string("Connection timed out"));
        case -5: return Datum::of(std::string("Invalid message"));
        case -6: return Datum::of(std::string("Invalid server address"));
        default: return Datum::of("Unknown error (" + std::to_string(args[0].intValue()) + ")");
    }
}

void MultiuserXtra::pollIntoQueue(int instance_id, InstanceState& state) {
    if (bridge_ == nullptr) {
        return;
    }
    // Low-water throttle: when the movie configured a minimum buffer, don't
    // drain the socket further while we are already behind.  The kernel
    // socket buffer absorbs the excess and TCP backpressure slows the server.
    if (state.buffer_min > 0 &&
        state.message_queue.size() >= static_cast<std::size_t>(state.buffer_min)) {
        return;
    }
    if (state.message_queue.size() >= k_max_queued_messages) {
        return;
    }
    auto messages = bridge_->pollMessages(instance_id);
    const auto capacity = k_max_queued_messages - state.message_queue.size();
    const auto accepted = std::min(capacity, messages.size());
    state.message_queue.insert(state.message_queue.end(),
                              messages.begin(),
                              messages.begin() + static_cast<std::ptrdiff_t>(accepted));
}

void MultiuserXtra::invokeCallback(const InstanceState& state) const {
    if (!script_callback_ || !state.callback_target.has_value() || state.callback_handler.empty()) {
        return;
    }
    try {
        script_callback_(*state.callback_target, state.callback_handler, {});
    } catch (...) {
        // Match the Java Xtra path: callback failures are isolated from Xtra polling.
    }
}

} // namespace libreshockwave::lingo::xtra
