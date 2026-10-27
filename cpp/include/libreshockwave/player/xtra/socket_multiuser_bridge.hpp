#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "libreshockwave/lingo/xtra/multiuser_net_bridge.hpp"

namespace libreshockwave::player::xtra {

class SocketMultiuserBridge final : public lingo::xtra::MultiuserNetBridge {
public:
    using IncomingDataReadyPredicate = std::function<bool()>;

    SocketMultiuserBridge() = default;
    ~SocketMultiuserBridge() override;

    SocketMultiuserBridge(const SocketMultiuserBridge&) = delete;
    SocketMultiuserBridge& operator=(const SocketMultiuserBridge&) = delete;
    SocketMultiuserBridge(SocketMultiuserBridge&&) = delete;
    SocketMultiuserBridge& operator=(SocketMultiuserBridge&&) = delete;

    void requestConnect(int instance_id,
                        const std::string& host,
                        int port,
                        int mode,
                        const ConnectOptions& options) override;
    void requestSend(int instance_id,
                     const lingo::Datum& recipients,
                     const std::string& subject,
                     const lingo::Datum& content) override;
    void requestDisconnect(int instance_id) override;
    [[nodiscard]] bool isConnected(int instance_id) const override;
    [[nodiscard]] bool hasCompletedHandshake() const;

    // True once the movie has asked for a text (non-SMUS) connection. Callers
    // that gate work on the handshake use this to avoid waiting before the
    // movie has even requested the connection.
    [[nodiscard]] bool hasRequestedTextConnection() const;
    [[nodiscard]] std::vector<NetMessage> pollMessages(int instance_id) override;
    void destroyInstance(int instance_id) override;

    // Once the server handshake has switched the connection to Director's
    // opaque encrypted stream, the movie is responsible for framing it.  A
    // caller can hold that stream until the runtime resources needed by the
    // next network notification have been applied.
    void setIncomingDataReadyPredicate(IncomingDataReadyPredicate predicate);

private:
    struct Connection;

    [[nodiscard]] std::shared_ptr<Connection> connectionFor(int instance_id) const;
    void closeAll();

    mutable std::mutex connections_mutex_;
    std::unordered_map<int, std::shared_ptr<Connection>> connections_;
    IncomingDataReadyPredicate incoming_data_ready_predicate_;
    bool text_connection_requested_{false};
};

} // namespace libreshockwave::player::xtra
