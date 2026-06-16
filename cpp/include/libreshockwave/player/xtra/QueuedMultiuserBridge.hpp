#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/lingo/xtra/MultiuserNetBridge.hpp"

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
        int instanceId{0};
        std::string host;
        int port{0};
        std::string senderID;
        std::vector<std::string> recipients;
        std::string subject;
        std::string content;
        std::optional<std::vector<std::uint8_t>> wireBytesOverride;

        [[nodiscard]] std::string wireContent() const;
        [[nodiscard]] std::vector<std::uint8_t> wireBytes() const;
    };

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

    [[nodiscard]] const std::vector<PendingRequest>& pendingRequests() const;
    [[nodiscard]] const PendingRequest* getRequest(int index) const;
    void drainPendingRequests();

    void notifyConnected(int instanceId);
    void notifyDisconnected(int instanceId);
    void notifyError(int instanceId, int errorCode);
    void deliverMessage(int instanceId,
                        int errorCode,
                        std::string senderID,
                        std::string subject,
                        std::string content);
    void deliverMessageBytes(int instanceId, const std::vector<std::uint8_t>& data);

    [[nodiscard]] static std::string serializeWireContent(std::string_view subject, std::string_view content);
    [[nodiscard]] static int decodeShockwaveCommand(char high, char low);
    [[nodiscard]] static bool isLegacyPlaintextPong(std::string_view content);

private:
    struct SmusFrame {
        int errorCode{0};
        std::string subject;
        std::string sender;
        std::vector<std::uint8_t> content;
    };

    [[nodiscard]] bool isSmusInstance(int instanceId) const;
    [[nodiscard]] std::string senderForInstance(int instanceId) const;
    [[nodiscard]] std::vector<std::uint8_t> packSmusLogon() const;
    [[nodiscard]] std::vector<std::uint8_t> packSmusMessage(const std::string& senderID,
                                                            const std::vector<std::string>& recipients,
                                                            const std::string& subject,
                                                            const lingo::Datum& content) const;
    void deliverSmusMessage(int instanceId, const std::vector<std::uint8_t>& data);
    void queueMessage(int instanceId, NetMessage message);
    [[nodiscard]] static std::vector<std::uint8_t> packSmusFrame(int errorCode,
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

    std::vector<PendingRequest> pendingRequests_;
    std::map<int, bool> connected_;
    std::map<int, std::vector<NetMessage>> messageQueues_;
    std::map<int, int> modes_;
    std::map<int, std::string> senderIDs_;
};

} // namespace libreshockwave::player::xtra
