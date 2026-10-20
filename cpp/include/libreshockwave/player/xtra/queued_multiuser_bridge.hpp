#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/lingo/xtra/multiuser_net_bridge.hpp"

namespace libreshockwave::player::xtra {

class QueuedMultiuserBridge final : public lingo::xtra::MultiuserNetBridge {
public:
    enum RequestType {
        REQ_CONNECT = 0,
        REQ_SEND = 1,
        REQ_DISCONNECT = 2
    };

    struct PendingRequest {
        int type{REQ_CONNECT};
        int instance_id{0};
        std::string host;
        int port{0};
        std::string sender_id;
        std::vector<std::string> recipients;
        std::string subject;
        std::string content;
        std::optional<std::vector<std::uint8_t>> wire_bytes_override;

        [[nodiscard]] std::string wireContent() const;
        [[nodiscard]] std::vector<std::uint8_t> wireBytes() const;
    };

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
    [[nodiscard]] std::vector<NetMessage> pollMessages(int instance_id) override;
    void destroyInstance(int instance_id) override;

    [[nodiscard]] const std::vector<PendingRequest>& pendingRequests() const;
    [[nodiscard]] const PendingRequest* getRequest(int index) const;
    void drainPendingRequests();

    void notifyConnected(int instance_id);
    void notifyDisconnected(int instance_id);
    void notifyError(int instance_id, int error_code);
    void deliverMessage(int instance_id,
                        int error_code,
                        std::string sender_id,
                        std::string subject,
                        std::string content);
    void deliverMessageBytes(int instance_id, const std::vector<std::uint8_t>& data);

    [[nodiscard]] static std::string serializeWireContent(std::string_view subject, std::string_view content);
    [[nodiscard]] static int decodeShockwaveCommand(char high, char low);
    [[nodiscard]] static bool isLegacyPlaintextPong(std::string_view content);

private:
    struct SmusFrame {
        int error_code{0};
        std::string subject;
        std::string sender;
        std::vector<std::uint8_t> content;
    };

    [[nodiscard]] bool isSmusInstance(int instance_id) const;
    [[nodiscard]] std::string senderForInstance(int instance_id) const;
    [[nodiscard]] std::vector<std::uint8_t> packSmusLogon() const;
    [[nodiscard]] std::vector<std::uint8_t> packSmusMessage(const std::string& sender_id,
                                                            const std::vector<std::string>& recipients,
                                                            const std::string& subject,
                                                            const lingo::Datum& content) const;
    void deliverSmusMessage(int instance_id, const std::vector<std::uint8_t>& data);
    void queueMessage(int instance_id, NetMessage message);
    [[nodiscard]] static std::vector<std::uint8_t> packSmusFrame(int error_code,
                                                                 int timestamp,
                                                                 std::string_view subject,
                                                                 std::string_view sender,
                                                                 const std::vector<std::string>& recipients,
                                                                 const std::vector<std::uint8_t>& content);
    [[nodiscard]] static SmusFrame unpackSmusBody(const std::vector<std::uint8_t>& data,
                                                  int offset,
                                                  int length);
    [[nodiscard]] static std::vector<std::uint8_t> encodeLingoValue(const lingo::Datum& value);
    [[nodiscard]] static lingo::Datum decodeLingoValue(const std::vector<std::uint8_t>& bytes);

    std::vector<PendingRequest> pending_requests_;
    std::map<int, bool> connected_;
    std::map<int, std::vector<NetMessage>> message_queues_;
    std::map<int, int> modes_;
    std::map<int, std::string> sender_i_ds_;
};

} // namespace libreshockwave::player::xtra
