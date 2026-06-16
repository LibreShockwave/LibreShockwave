#include "libreshockwave/player/xtra/SocketMultiuserBridge.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include "libreshockwave/player/xtra/QueuedMultiuserBridge.hpp"

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace libreshockwave::player::xtra {
namespace {

constexpr std::size_t READ_BUFFER_SIZE = 8192;
constexpr int SMUS_MODE = 0;
constexpr std::size_t SMUS_HEADER_SIZE = 6;

void closeSocket(int& socketFd) {
    if (socketFd >= 0) {
        ::close(socketFd);
        socketFd = -1;
    }
}

int connectSocket(const std::string& host, int port) {
    if (host.empty() || port <= 0) {
        return -1;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* rawResult = nullptr;
    const std::string service = std::to_string(port);
    if (::getaddrinfo(host.c_str(), service.c_str(), &hints, &rawResult) != 0) {
        return -1;
    }

    std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> results(rawResult, ::freeaddrinfo);
    for (addrinfo* entry = results.get(); entry != nullptr; entry = entry->ai_next) {
        int socketFd = ::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
        if (socketFd < 0) {
            continue;
        }

        if (::connect(socketFd, entry->ai_addr, entry->ai_addrlen) == 0) {
            return socketFd;
        }
        ::close(socketFd);
    }

    return -1;
}

int sendFlags() {
#ifdef MSG_NOSIGNAL
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
}

int recvFlags() {
#ifdef MSG_DONTWAIT
    return MSG_DONTWAIT;
#else
    return 0;
#endif
}

bool sendAll(int socketFd, const std::vector<std::uint8_t>& bytes) {
    if (socketFd < 0 || bytes.empty()) {
        return true;
    }

    const auto* cursor = bytes.data();
    std::size_t remaining = bytes.size();
    while (remaining > 0) {
        const ssize_t sent = ::send(socketFd, cursor, remaining, sendFlags());
        if (sent <= 0) {
            return false;
        }
        cursor += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}

std::optional<std::vector<std::uint8_t>> takeSmusFrame(std::vector<std::uint8_t>& buffer) {
    if (buffer.size() < SMUS_HEADER_SIZE) {
        return std::nullopt;
    }

    const int bodyLength = (static_cast<int>(buffer[2]) << 24) |
                           (static_cast<int>(buffer[3]) << 16) |
                           (static_cast<int>(buffer[4]) << 8) |
                           static_cast<int>(buffer[5]);
    if (buffer[0] != 114 || buffer[1] != 0 || bodyLength < 0) {
        auto invalid = std::move(buffer);
        buffer.clear();
        return invalid;
    }

    const std::size_t frameLength = SMUS_HEADER_SIZE + static_cast<std::size_t>(bodyLength);
    if (buffer.size() < frameLength) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> frame(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(frameLength));
    buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(frameLength));
    return frame;
}

} // namespace

struct SocketMultiuserBridge::Connection {
    mutable std::mutex mutex;
    int socketFd = -1;
    int instanceId = 0;
    bool connected = false;
    bool connecting = false;
    bool closeRequested = false;
    int mode = 0;
    std::array<char, READ_BUFFER_SIZE> readBuf{};
    std::vector<std::uint8_t> inboundBuffer;
    std::vector<NetMessage> queuedMessages;
    QueuedMultiuserBridge protocol;

    ~Connection() {
        closeSocket(socketFd);
    }
};

SocketMultiuserBridge::~SocketMultiuserBridge() {
    closeAll();
}

void SocketMultiuserBridge::requestConnect(int instanceId,
                                           const std::string& host,
                                           int port,
                                           int mode,
                                           const ConnectOptions& options) {
    auto connection = std::make_shared<Connection>();
    {
        std::lock_guard lock(connection->mutex);
        connection->instanceId = instanceId;
        connection->connecting = true;
        connection->mode = mode;
        connection->protocol.requestConnect(instanceId, host, port, mode, options);
        connection->protocol.drainPendingRequests();
    }

    {
        std::lock_guard lock(connectionsMutex_);
        connections_[instanceId] = connection;
    }

    std::thread([connection, host, port] {
        int socketFd = connectSocket(host, port);
        std::lock_guard lock(connection->mutex);
        connection->connecting = false;
        if (connection->closeRequested) {
            closeSocket(socketFd);
            return;
        }
        if (socketFd < 0) {
            connection->protocol.notifyError(connection->instanceId, -3);
            auto messages = connection->protocol.pollMessages(connection->instanceId);
            connection->queuedMessages.insert(connection->queuedMessages.end(), messages.begin(), messages.end());
            return;
        }
        connection->socketFd = socketFd;
        connection->connected = true;
        connection->protocol.notifyConnected(connection->instanceId);
        for (const auto& request : connection->protocol.pendingRequests()) {
            if (request.type == QueuedMultiuserBridge::REQ_SEND &&
                !sendAll(connection->socketFd, request.wireBytes())) {
                closeSocket(connection->socketFd);
                connection->connected = false;
                connection->protocol.notifyError(connection->instanceId, -2);
                break;
            }
        }
        connection->protocol.drainPendingRequests();
        auto messages = connection->protocol.pollMessages(connection->instanceId);
        connection->queuedMessages.insert(connection->queuedMessages.end(), messages.begin(), messages.end());
    }).detach();
}

void SocketMultiuserBridge::requestSend(int instanceId,
                                        const lingo::Datum& recipients,
                                        const std::string& subject,
                                        const lingo::Datum& content) {
    auto connection = connectionFor(instanceId);
    if (connection == nullptr) {
        return;
    }

    std::lock_guard lock(connection->mutex);
    if (!connection->connected || connection->socketFd < 0) {
        return;
    }

    connection->protocol.requestSend(instanceId, recipients, subject, content);
    for (const auto& request : connection->protocol.pendingRequests()) {
        if (request.type != QueuedMultiuserBridge::REQ_SEND) {
            continue;
        }
        if (!sendAll(connection->socketFd, request.wireBytes())) {
            closeSocket(connection->socketFd);
            connection->connected = false;
            connection->protocol.notifyError(instanceId, -2);
            auto messages = connection->protocol.pollMessages(instanceId);
            connection->queuedMessages.insert(connection->queuedMessages.end(), messages.begin(), messages.end());
            connection->protocol.drainPendingRequests();
            return;
        }
    }
    connection->protocol.drainPendingRequests();
}

void SocketMultiuserBridge::requestDisconnect(int instanceId) {
    std::shared_ptr<Connection> connection;
    {
        std::lock_guard lock(connectionsMutex_);
        const auto found = connections_.find(instanceId);
        if (found == connections_.end()) {
            return;
        }
        connection = std::move(found->second);
        connections_.erase(found);
    }

    std::lock_guard lock(connection->mutex);
    connection->closeRequested = true;
    connection->connecting = false;
    connection->connected = false;
    connection->protocol.notifyDisconnected(instanceId);
    closeSocket(connection->socketFd);
}

bool SocketMultiuserBridge::isConnected(int instanceId) const {
    auto connection = connectionFor(instanceId);
    if (connection == nullptr) {
        return false;
    }
    std::lock_guard lock(connection->mutex);
    return connection->connected;
}

std::vector<SocketMultiuserBridge::NetMessage> SocketMultiuserBridge::pollMessages(int instanceId) {
    auto connection = connectionFor(instanceId);
    if (connection == nullptr) {
        return {};
    }

    std::lock_guard lock(connection->mutex);
    std::vector<NetMessage> result = std::move(connection->queuedMessages);
    connection->queuedMessages.clear();

    if (!connection->connected || connection->socketFd < 0) {
        return result;
    }

    for (int attempt = 0; attempt < 16; ++attempt) {
        const ssize_t read = ::recv(connection->socketFd,
                                   connection->readBuf.data(),
                                   connection->readBuf.size(),
                                   recvFlags());
        if (read > 0) {
            const auto* begin = reinterpret_cast<const std::uint8_t*>(connection->readBuf.data());
            if (connection->mode == SMUS_MODE) {
                connection->inboundBuffer.insert(connection->inboundBuffer.end(),
                                                 begin,
                                                 begin + read);
                while (auto frame = takeSmusFrame(connection->inboundBuffer)) {
                    connection->protocol.deliverMessageBytes(instanceId, *frame);
                }
            } else {
                connection->protocol.deliverMessageBytes(
                    instanceId,
                    std::vector<std::uint8_t>(begin, begin + read));
            }
            continue;
        }
        if (read == 0) {
            closeSocket(connection->socketFd);
            connection->connected = false;
            connection->protocol.notifyDisconnected(instanceId);
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        closeSocket(connection->socketFd);
        connection->connected = false;
        connection->protocol.notifyError(instanceId, -2);
        break;
    }

    auto messages = connection->protocol.pollMessages(instanceId);
    result.insert(result.end(), messages.begin(), messages.end());
    return result;
}

void SocketMultiuserBridge::destroyInstance(int instanceId) {
    requestDisconnect(instanceId);
}

std::shared_ptr<SocketMultiuserBridge::Connection> SocketMultiuserBridge::connectionFor(int instanceId) const {
    std::lock_guard lock(connectionsMutex_);
    const auto found = connections_.find(instanceId);
    return found != connections_.end() ? found->second : nullptr;
}

void SocketMultiuserBridge::closeAll() {
    std::vector<std::shared_ptr<Connection>> connections;
    {
        std::lock_guard lock(connectionsMutex_);
        connections.reserve(connections_.size());
        for (auto& entry : connections_) {
            connections.push_back(std::move(entry.second));
        }
        connections_.clear();
    }

    for (const auto& connection : connections) {
        std::lock_guard lock(connection->mutex);
        connection->closeRequested = true;
        connection->connecting = false;
        connection->connected = false;
        closeSocket(connection->socketFd);
    }
}

} // namespace libreshockwave::player::xtra
