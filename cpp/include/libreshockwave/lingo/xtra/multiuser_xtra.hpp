#pragma once

#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/lingo/xtra/multiuser_net_bridge.hpp"
#include "libreshockwave/lingo/xtra/script_callback.hpp"
#include "libreshockwave/lingo/xtra/xtra_manager.hpp"

namespace libreshockwave::lingo::xtra {

class MultiuserXtra : public Xtra {
public:
    using MessageDispatchPredicate = std::function<bool()>;

    MultiuserXtra(MultiuserNetBridge* bridge,
                  ScriptCallback callback,
                  MessageDispatchPredicate message_dispatch_predicate = {});

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] int createInstance(const std::vector<Datum>& args) override;
    void destroyInstance(int instance_id) override;
    [[nodiscard]] Datum callHandler(int instance_id,
                                    std::string_view handler_name,
                                    const std::vector<Datum>& args) override;
    [[nodiscard]] Datum getProperty(int instance_id, std::string_view property_name) const override;
    void setProperty(int instance_id, std::string_view property_name, const Datum& value) override;
    void tick() override;

private:
    struct InstanceState {
        std::string host;
        int port = 0;
        MultiuserNetBridge::ConnectOptions connect_options;
        int buffer_min = 0;
        int buffer_max = 0;
        int buffer_urgency = 0;
        std::string callback_handler;
        std::optional<Datum> callback_target;
        std::optional<MultiuserNetBridge::NetMessage> current_message;
        // A deque so draining a burst is O(1) per message instead of
        // O(n) vector erase-from-front (quadratic when catching up).
        std::deque<MultiuserNetBridge::NetMessage> message_queue;
    };

    [[nodiscard]] Datum setNetBufferLimits(InstanceState& state, const std::vector<Datum>& args);
    [[nodiscard]] Datum setNetMessageHandler(InstanceState& state, const std::vector<Datum>& args);
    [[nodiscard]] Datum connectToNetServer(int instance_id, InstanceState& state, const std::vector<Datum>& args);
    [[nodiscard]] Datum sendNetMessage(int instance_id, InstanceState& state, const std::vector<Datum>& args);
    [[nodiscard]] Datum getNetMessage(const InstanceState& state) const;
    [[nodiscard]] Datum checkNetMessages(int instance_id, InstanceState& state, const std::vector<Datum>& args);
    [[nodiscard]] Datum getNumberWaitingNetMessages(int instance_id, InstanceState& state);
    [[nodiscard]] static Datum getNetErrorString(const std::vector<Datum>& args);
    void pollIntoQueue(int instance_id, InstanceState& state);
    void invokeCallback(const InstanceState& state) const;

    MultiuserNetBridge* bridge_{nullptr};
    ScriptCallback script_callback_;
    MessageDispatchPredicate message_dispatch_predicate_;
    std::unordered_map<int, InstanceState> instances_;
    int next_instance_id_ = 1;
};

} // namespace libreshockwave::lingo::xtra
