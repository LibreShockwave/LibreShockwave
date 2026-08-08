#include "libreshockwave/player/xtra/SocketMultiuserBridge.hpp"

#include <array>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include "libreshockwave/player/xtra/QueuedMultiuserBridge.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace libreshockwave::player::xtra {

#ifdef _WIN32
// Winsock handles are unsigned opaque values rather than small POSIX ints,
// and send()/recv() return int rather than ssize_t.
using SocketHandle = SOCKET;
constexpr SocketHandle INVALID_SOCKET_HANDLE = INVALID_SOCKET;
using SocketSignedSize = int;
#else
using SocketHandle = int;
constexpr SocketHandle INVALID_SOCKET_HANDLE = -1;
using SocketSignedSize = ssize_t;
#endif

namespace {

constexpr std::size_t READ_BUFFER_SIZE = 8192;
constexpr int SMUS_MODE = 0;
constexpr std::size_t SMUS_HEADER_SIZE = 6;
// A server burst can deliver hundreds of messages within milliseconds.  The
// movie consumes a bounded batch per frame, so a single poll must not drain
// and parse the whole flood: excess stays in the kernel socket buffer (TCP
// backpressure slows the server) or is held back for the next poll.
/// Maximum bytes read from the socket in one poll.
constexpr std::size_t MAX_READ_BYTES_PER_POLL = 32768;
/// Stop draining the kernel once this much raw data is already held, so a
/// movie that cannot keep up throttles the server via TCP flow control.
constexpr std::size_t MAX_HELD_BYTES = 65536;
/// Maximum frames/messages handed to the movie per poll; the rest carry over.
constexpr std::size_t MAX_FRAMES_PER_POLL = 128;
/// Raw encrypted bytes are flushed to the movie once a flood has piled up a
/// chunk of at least this size (continuous traffic never reaches the
/// quiet-poll condition below, so a size threshold is the flood fallback).
constexpr std::size_t RAW_FLUSH_THRESHOLD = 4096;

void closeSocket(SocketHandle& socketFd) {
    if (socketFd != INVALID_SOCKET_HANDLE) {
#ifdef _WIN32
        ::closesocket(socketFd);
#else
        ::close(socketFd);
#endif
        socketFd = INVALID_SOCKET_HANDLE;
    }
}

#ifdef _WIN32
// Winsock must be initialised once per process before any socket call.  The
// process lives and dies with the player and connect threads are detached, so
// WSACleanup is deliberately never called.
void ensureWinsockStarted() {
    static const bool started = [] {
        WSADATA data;
        return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    (void)started;
}
#endif

SocketHandle connectSocket(const std::string& host, int port) {
#ifdef _WIN32
    ensureWinsockStarted();
#endif
    if (host.empty() || port <= 0) {
        return INVALID_SOCKET_HANDLE;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* rawResult = nullptr;
    const std::string service = std::to_string(port);
    if (::getaddrinfo(host.c_str(), service.c_str(), &hints, &rawResult) != 0) {
        return INVALID_SOCKET_HANDLE;
    }

    std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> results(rawResult, ::freeaddrinfo);
    for (addrinfo* entry = results.get(); entry != nullptr; entry = entry->ai_next) {
        SocketHandle socketFd = ::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
        if (socketFd == INVALID_SOCKET_HANDLE) {
            continue;
        }

        if (::connect(socketFd, entry->ai_addr, static_cast<int>(entry->ai_addrlen)) == 0) {
            return socketFd;
        }
        closeSocket(socketFd);
    }

    return INVALID_SOCKET_HANDLE;
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

bool sendAll(SocketHandle socketFd, const std::vector<std::uint8_t>& bytes) {
    if (socketFd == INVALID_SOCKET_HANDLE || bytes.empty()) {
        return true;
    }

    const auto* cursor = bytes.data();
    std::size_t remaining = bytes.size();
    while (remaining > 0) {
        // Winsock send() takes a char pointer and int length; the same
        // arguments convert cleanly to POSIX's void*/size_t signature.
        const SocketSignedSize sent = ::send(socketFd,
                                             reinterpret_cast<const char*>(cursor),
                                             static_cast<int>(remaining),
                                             sendFlags());
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

std::optional<std::vector<std::uint8_t>> takePlaintextMessage(std::vector<std::uint8_t>& buffer) {
    const auto terminator = std::find(buffer.begin(), buffer.end(), static_cast<std::uint8_t>(1));
    if (terminator == buffer.end()) {
        return std::nullopt;
    }

    const auto end = terminator + 1;
    std::vector<std::uint8_t> message(buffer.begin(), end);
    buffer.erase(buffer.begin(), end);
    return message;
}

} // namespace

struct SocketMultiuserBridge::Connection {
    mutable std::mutex mutex;
    SocketHandle socketFd = INVALID_SOCKET_HANDLE;
    int instanceId = 0;
    bool connected = false;
    bool connecting = false;
    bool closeRequested = false;
    int mode = 0;
    std::array<char, READ_BUFFER_SIZE> readBuf{};
    std::vector<std::uint8_t> inboundBuffer;
    // Raw Multiuser traffic is a TCP byte stream.  The encrypted Director
    // framing is owned by the movie, so a recv() chunk is not a message
    // boundary.  Hold encrypted bytes briefly so a split frame header is not
    // handed to the movie as if it were a complete header.
    // The handshake responses are delimiter-terminated plaintext messages.
    // Once the server-secret-key response arrives, subsequent traffic uses
    // encrypted length-framed packets and must remain opaque to this bridge.
    bool rawEncrypted = false;
    int rawInboundQuietPolls = 0;
    std::vector<NetMessage> queuedMessages;
    // Polled messages beyond the per-poll cap, held for the next poll so the
    // movie consumes a bounded batch per frame without any message loss.
    std::vector<NetMessage> returnBuffer;
    QueuedMultiuserBridge protocol;

    ~Connection() {
        closeSocket(socketFd);
    }
};

SocketMultiuserBridge::~SocketMultiuserBridge() {
    closeAll();
}

void SocketMultiuserBridge::setIncomingDataReadyPredicate(IncomingDataReadyPredicate predicate) {
    std::lock_guard lock(connectionsMutex_);
    incomingDataReadyPredicate_ = std::move(predicate);
}

bool SocketMultiuserBridge::hasCompletedHandshake() const {
    std::lock_guard lock(connectionsMutex_);
    if (!textConnectionRequested_) {
        return false;
    }
    bool activeConnection = false;
    bool textConnectionActive = false;
    bool allReady = true;
    for (const auto& [_, connection] : connections_) {
        if (!connection) {
            continue;
        }
        std::lock_guard connectionLock(connection->mutex);
        if (!connection->connecting && !connection->connected) {
            continue;
        }
        activeConnection = true;
        if (connection->mode != SMUS_MODE) {
            textConnectionActive = true;
            if (!connection->rawEncrypted) {
                allReady = false;
            }
        }
    }
    const bool ready = allReady && activeConnection &&
                       (!textConnectionRequested_ || textConnectionActive);
    return ready;
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
        if (mode != SMUS_MODE) {
            textConnectionRequested_ = true;
        }
        connections_[instanceId] = connection;
    }

    std::thread([connection, host, port] {
        SocketHandle socketFd = connectSocket(host, port);
        std::lock_guard lock(connection->mutex);
        connection->connecting = false;
        if (connection->closeRequested) {
            closeSocket(socketFd);
            return;
        }
        if (socketFd == INVALID_SOCKET_HANDLE) {
            connection->protocol.notifyError(connection->instanceId, -3);
            auto messages = connection->protocol.pollMessages(connection->instanceId);
            connection->queuedMessages.insert(connection->queuedMessages.end(), messages.begin(), messages.end());
            return;
        }
        connection->socketFd = socketFd;
#ifdef _WIN32
        // Winsock has no MSG_DONTWAIT, so switch the socket to non-blocking
        // mode to keep pollMessages() from stalling on a quiet server.
        u_long nonBlockingMode = 1;
        ::ioctlsocket(socketFd, FIONBIO, &nonBlockingMode);
#endif
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
    if (!connection->connected || connection->socketFd == INVALID_SOCKET_HANDLE) {
        return;
    }

    connection->protocol.requestSend(instanceId, recipients, subject, content);
    for (const auto& request : connection->protocol.pendingRequests()) {
        if (request.type != QueuedMultiuserBridge::REQ_SEND) {
            continue;
        }
        const auto bytes = request.wireBytes();
        if (!sendAll(connection->socketFd, bytes)) {
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

    if (connection->connected && connection->socketFd != INVALID_SOCKET_HANDLE) {
        bool readRawBytes = false;
        std::size_t bytesRead = 0;
        std::size_t framesDelivered = 0;
        for (int attempt = 0; attempt < 16; ++attempt) {
            // Bound this poll's work: stop draining the socket once a burst
            // has been partially consumed.  Excess stays in the kernel
            // buffer (TCP backpressure slows the server) or in
            // inboundBuffer for the next poll.
            if (bytesRead >= MAX_READ_BYTES_PER_POLL ||
                connection->inboundBuffer.size() >= MAX_HELD_BYTES) {
                break;
            }
            const SocketSignedSize read = ::recv(connection->socketFd,
                                                 connection->readBuf.data(),
                                                 static_cast<int>(connection->readBuf.size()),
                                                 recvFlags());
            if (read > 0) {
                bytesRead += static_cast<std::size_t>(read);
                const auto* begin = reinterpret_cast<const std::uint8_t*>(connection->readBuf.data());
                if (connection->mode == SMUS_MODE) {
                    connection->inboundBuffer.insert(connection->inboundBuffer.end(),
                                                     begin,
                                                     begin + read);
                    while (framesDelivered < MAX_FRAMES_PER_POLL) {
                        auto frame = takeSmusFrame(connection->inboundBuffer);
                        if (!frame.has_value()) {
                            break;
                        }
                        ++framesDelivered;
                        connection->protocol.deliverMessageBytes(instanceId, *frame);
                    }
                } else {
                    connection->inboundBuffer.insert(connection->inboundBuffer.end(),
                                                     begin,
                                                     begin + read);
                    if (!connection->rawEncrypted) {
                        while (framesDelivered < MAX_FRAMES_PER_POLL) {
                            auto message = takePlaintextMessage(connection->inboundBuffer);
                            if (!message.has_value()) {
                                break;
                            }
                            ++framesDelivered;
                            const bool isServerSecretKey = message->size() >= 2 &&
                                QueuedMultiuserBridge::decodeShockwaveCommand(
                                    static_cast<char>((*message)[0]),
                                    static_cast<char>((*message)[1])) == 1;
                            connection->protocol.deliverMessageBytes(instanceId, *message);
                            if (isServerSecretKey) {
                                connection->rawEncrypted = true;
                                break;
                            }
                        }
                    }
                    if (connection->rawEncrypted) {
                        readRawBytes = true;
                        connection->rawInboundQuietPolls = 0;
                    }
                }
                continue;
            }
            if (read == 0) {
                closeSocket(connection->socketFd);
                connection->connected = false;
                connection->protocol.notifyDisconnected(instanceId);
                break;
            }
#ifdef _WIN32
            if (::WSAGetLastError() == WSAEWOULDBLOCK) {
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
#endif
                break;
            }

            closeSocket(connection->socketFd);
            connection->connected = false;
            connection->protocol.notifyError(instanceId, -2);
            break;
        }

        if (connection->mode != SMUS_MODE && connection->rawEncrypted &&
            !connection->inboundBuffer.empty()) {
            // Director's decoder already retains an incomplete encrypted body,
            // but it cannot recover if it receives only an encrypted header.  A
            // few quiet polls let TCP deliver the rest of a small frame while
            // retaining a complete stream chunk for Director's parser.
            if (readRawBytes) {
                connection->rawInboundQuietPolls = 0;
            } else {
                ++connection->rawInboundQuietPolls;
            }
            // Under continuous traffic there are no quiet polls, so a flood
            // would otherwise pile up bytes forever and starve the movie:
            // flush as soon as a full chunk has accumulated.
            const bool incomingDataReady = !incomingDataReadyPredicate_ || incomingDataReadyPredicate_();
            if (incomingDataReady &&
                (connection->inboundBuffer.size() >= RAW_FLUSH_THRESHOLD ||
                 (connection->inboundBuffer.size() >= 7 &&
                  connection->rawInboundQuietPolls >= 3))) {
                connection->protocol.deliverMessageBytes(instanceId, connection->inboundBuffer);
                connection->inboundBuffer.clear();
                connection->rawInboundQuietPolls = 0;
            } else if (!incomingDataReady) {
                // Do not count quiet polls while the runtime is applying
                // external casts. The buffered stream is still one TCP
                // sequence and must be handed to Director as a whole after
                // the dependency boundary has passed.
                connection->rawInboundQuietPolls = 0;
            }
        }
    } else if (connection->mode != SMUS_MODE && !connection->inboundBuffer.empty()) {
        // Disconnected: flush any remaining plaintext/raw bytes so the movie
        // sees the tail before the close/error state.
        connection->protocol.deliverMessageBytes(instanceId, connection->inboundBuffer);
        connection->inboundBuffer.clear();
        connection->rawInboundQuietPolls = 0;
    }

    auto messages = connection->protocol.pollMessages(instanceId);
    std::vector<NetMessage> all;
    all.reserve(connection->returnBuffer.size() + messages.size());
    all.insert(all.end(), connection->returnBuffer.begin(), connection->returnBuffer.end());
    connection->returnBuffer.clear();
    all.insert(all.end(), messages.begin(), messages.end());

    std::vector<NetMessage> delivered;
    if (connection->connected && connection->socketFd != INVALID_SOCKET_HANDLE &&
        all.size() > MAX_FRAMES_PER_POLL) {
        // Hold back the excess for the next poll: the movie consumes a
        // bounded batch per frame, and dropping would lose protocol messages.
        delivered.assign(all.begin(),
                         all.begin() + static_cast<std::ptrdiff_t>(MAX_FRAMES_PER_POLL));
        connection->returnBuffer.assign(
            all.begin() + static_cast<std::ptrdiff_t>(MAX_FRAMES_PER_POLL),
            all.end());
    } else {
        delivered = std::move(all);
    }
    result.insert(result.end(), delivered.begin(), delivered.end());
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
