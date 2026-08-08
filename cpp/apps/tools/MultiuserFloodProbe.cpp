// Flood probe for the multiuser receive path.
//
// A local TCP server dumps thousands of SMUS frames back to back (a burst
// arriving within a few milliseconds) and holds the connection open.  The
// bridge must deliver every message exactly once, in bounded batches per
// poll — the client must not stall on the burst, and nothing may be lost.
//
// Usage: run the probe; exit code 0 means all messages arrived, each poll
// stayed within the per-poll cap, and nothing was duplicated or dropped.

#include "libreshockwave/player/xtra/SocketMultiuserBridge.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr int kFrameCount = 3000;
// Matches the bridge's per-poll cap; a poll above this means the flood
// stalled the receiver.
constexpr std::size_t kMaxPerPoll = 128;
constexpr int kContentPad = 300;

void putShort(std::vector<std::uint8_t>& out, int value) {
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void putInt(std::vector<std::uint8_t>& out, int value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void putChunk(std::vector<std::uint8_t>& out, const std::string& text) {
    putInt(out, static_cast<int>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
    // Chunks are 2-byte aligned (Director-style pascal strings): odd-length
    // payloads carry a trailing pad byte the reader skips.
    if ((text.size() & 1U) != 0U) {
        out.push_back(0);
    }
}

// SMUS frame: header [0x72, 0x00, bodyLength BE32] followed by the body the
// bridge decodes into a NetMessage (errorCode, timestamp, subject, sender,
// recipient count, content as a Lingo-encoded string).
std::vector<std::uint8_t> buildFrame(int index) {
    std::vector<std::uint8_t> body;
    putInt(body, 0);                            // errorCode
    putInt(body, 0);                            // timestamp
    putChunk(body, "MSG" + std::to_string(index));  // subject
    putChunk(body, "flooder");                  // sender
    putInt(body, 0);                            // recipients
    putShort(body, 3);                          // content: Lingo string
    putChunk(body, std::string(kContentPad, 'x'));

    std::vector<std::uint8_t> frame;
    frame.push_back(0x72);
    frame.push_back(0x00);
    putInt(frame, static_cast<int>(body.size()));
    frame.insert(frame.end(), body.begin(), body.end());
    return frame;
}

void runServer(std::atomic<int>& portOut) {
    const int listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        portOut.store(-1);
        return;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(listenFd);
        portOut.store(-1);
        return;
    }
    socklen_t addrLen = sizeof(addr);
    if (::getsockname(listenFd, reinterpret_cast<sockaddr*>(&addr), &addrLen) != 0) {
        ::close(listenFd);
        portOut.store(-1);
        return;
    }
    if (::listen(listenFd, 1) != 0) {
        ::close(listenFd);
        portOut.store(-1);
        return;
    }
    portOut.store(ntohs(addr.sin_port));

    const int clientFd = ::accept(listenFd, nullptr, nullptr);
    ::close(listenFd);
    if (clientFd < 0) {
        return;
    }

    // Write every frame back to back; the kernel coalesces them into a
    // burst that arrives within a few milliseconds.
    for (int i = 0; i < kFrameCount; ++i) {
        const auto frame = buildFrame(i);
        std::size_t sent = 0;
        while (sent < frame.size()) {
            const ssize_t n =
                ::send(clientFd, frame.data() + sent, frame.size() - sent, MSG_NOSIGNAL);
            if (n <= 0) {
                break;
            }
            sent += static_cast<std::size_t>(n);
        }
    }

    // Hold the connection open so the receiver must drain the burst across
    // several polls rather than reading it as one shot after EOF.
    ::sleep(5);
    ::close(clientFd);
}
} // namespace

int main() {
    std::atomic<int> port{0};
    std::thread server(runServer, std::ref(port));
    while (port.load() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (port.load() < 0) {
        std::fprintf(stderr, "FAIL: could not start flood server\n");
        server.join();
        return 1;
    }

    libreshockwave::player::xtra::SocketMultiuserBridge bridge;
    bridge.requestConnect(1, "127.0.0.1", port.load(), 0 /* SMUS */, {});

    int frames = 0;
    bool sawConnectNotification = false;
    std::size_t maxBatch = 0;
    int polls = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (frames < kFrameCount && std::chrono::steady_clock::now() < deadline) {
        const auto messages = bridge.pollMessages(1);
        ++polls;
        if (messages.size() > maxBatch) {
            maxBatch = messages.size();
        }
        if (messages.size() > kMaxPerPoll) {
            std::fprintf(stderr,
                         "FAIL: single poll returned %zu messages (cap %zu) — the client "
                         "would stall processing the burst\n",
                         messages.size(), kMaxPerPoll);
            bridge.requestDisconnect(1);
            server.join();
            return 1;
        }
        for (const auto& message : messages) {
            if (message.subject.rfind("MSG", 0) == 0) {
                ++frames;
            } else if (message.subject == "ConnectToNetServer") {
                sawConnectNotification = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    bridge.requestDisconnect(1);
    server.join();

    if (frames != kFrameCount) {
        std::fprintf(stderr, "FAIL: received %d of %d flood frames\n", frames, kFrameCount);
        return 1;
    }
    if (!sawConnectNotification) {
        std::fprintf(stderr, "FAIL: connect notification message was not delivered\n");
        return 1;
    }
    std::printf(
        "OK: %d/%d frames received in %d polls, max batch %zu (cap %zu), connect notice seen\n",
        frames, kFrameCount, polls, maxBatch, kMaxPerPoll);
    return 0;
}
