#pragma once

#include <string>
#include <vector>

#include "libreshockwave/lingo/datum.hpp"

namespace libreshockwave::lingo::xtra {

class MultiuserNetBridge {
public:
    struct ConnectOptions {
        std::string user_name;
        std::string password;
        std::string movie_id;
        std::string encryption_key;
    };

    struct NetMessage {
        int error_code = 0;
        std::string sender_id;
        std::string subject;
        Datum content = Datum::voidValue();
    };

    virtual ~MultiuserNetBridge() = default;

    virtual void requestConnect(int instance_id,
                                const std::string& host,
                                int port,
                                int mode,
                                const ConnectOptions& options) = 0;
    virtual void requestSend(int instance_id,
                             const Datum& recipients,
                             const std::string& subject,
                             const Datum& content) = 0;
    virtual void requestDisconnect(int instance_id) = 0;
    [[nodiscard]] virtual bool isConnected(int instance_id) const = 0;
    [[nodiscard]] virtual std::vector<NetMessage> pollMessages(int instance_id) = 0;
    virtual void destroyInstance(int instance_id) = 0;
};

} // namespace libreshockwave::lingo::xtra
