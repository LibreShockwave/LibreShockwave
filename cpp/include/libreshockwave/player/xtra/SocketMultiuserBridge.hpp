#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "libreshockwave/lingo/xtra/MultiuserNetBridge.hpp"

namespace libreshockwave::player::xtra {

class SocketMultiuserBridge final : public lingo::xtra::MultiuserNetBridge {
public:
    SocketMultiuserBridge() = default;
    ~SocketMultiuserBridge() override;

    SocketMultiuserBridge(const SocketMultiuserBridge&) = delete;
    SocketMultiuserBridge& operator=(const SocketMultiuserBridge&) = delete;
    SocketMultiuserBridge(SocketMultiuserBridge&&) = delete;
    SocketMultiuserBridge& operator=(SocketMultiuserBridge&&) = delete;

    void requestConnect(int instanceId,
                        const std::string& host,
                        int port,
                        int mode,
                        const ConnectOptions& options) override;
    void requestSend(int instanceId,
                     const lingo::Datum& recipients,
                     const std::string& subject,
                     const lingo::Datum& content) override;
    void requestDisconnect(int instanceId) override;
    [[nodiscard]] bool isConnected(int instanceId) const override;
    [[nodiscard]] std::vector<NetMessage> pollMessages(int instanceId) override;
    void destroyInstance(int instanceId) override;

private:
    struct Connection;

    [[nodiscard]] std::shared_ptr<Connection> connectionFor(int instanceId) const;
    void closeAll();

    mutable std::mutex connectionsMutex_;
    std::unordered_map<int, std::shared_ptr<Connection>> connections_;
};

} // namespace libreshockwave::player::xtra
