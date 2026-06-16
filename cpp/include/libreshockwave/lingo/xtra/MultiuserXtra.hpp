#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "libreshockwave/lingo/Datum.hpp"
#include "libreshockwave/lingo/xtra/MultiuserNetBridge.hpp"
#include "libreshockwave/lingo/xtra/ScriptCallback.hpp"
#include "libreshockwave/lingo/xtra/XtraManager.hpp"

namespace libreshockwave::lingo::xtra {

class MultiuserXtra : public Xtra {
public:
    MultiuserXtra(MultiuserNetBridge* bridge, ScriptCallback callback);

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] int createInstance(const std::vector<Datum>& args) override;
    void destroyInstance(int instanceId) override;
    [[nodiscard]] Datum callHandler(int instanceId,
                                    std::string_view handlerName,
                                    const std::vector<Datum>& args) override;
    [[nodiscard]] Datum getProperty(int instanceId, std::string_view propertyName) const override;
    void setProperty(int instanceId, std::string_view propertyName, const Datum& value) override;
    void tick() override;

private:
    struct InstanceState {
        std::string host;
        int port = 0;
        MultiuserNetBridge::ConnectOptions connectOptions;
        int bufferMin = 0;
        int bufferMax = 0;
        int bufferUrgency = 0;
        std::string callbackHandler;
        std::optional<Datum> callbackTarget;
        std::optional<MultiuserNetBridge::NetMessage> currentMessage;
        std::vector<MultiuserNetBridge::NetMessage> messageQueue;
    };

    [[nodiscard]] Datum setNetBufferLimits(InstanceState& state, const std::vector<Datum>& args);
    [[nodiscard]] Datum setNetMessageHandler(InstanceState& state, const std::vector<Datum>& args);
    [[nodiscard]] Datum connectToNetServer(int instanceId, InstanceState& state, const std::vector<Datum>& args);
    [[nodiscard]] Datum sendNetMessage(int instanceId, InstanceState& state, const std::vector<Datum>& args);
    [[nodiscard]] Datum getNetMessage(const InstanceState& state) const;
    [[nodiscard]] Datum checkNetMessages(int instanceId, InstanceState& state, const std::vector<Datum>& args);
    [[nodiscard]] Datum getNumberWaitingNetMessages(int instanceId, InstanceState& state);
    [[nodiscard]] static Datum getNetErrorString(const std::vector<Datum>& args);
    void pollIntoQueue(int instanceId, InstanceState& state);
    void invokeCallback(const InstanceState& state) const;

    MultiuserNetBridge* bridge_{nullptr};
    ScriptCallback scriptCallback_;
    std::unordered_map<int, InstanceState> instances_;
    int nextInstanceId_ = 1;
};

} // namespace libreshockwave::lingo::xtra
