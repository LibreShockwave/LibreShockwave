#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/player/xtra/queued_multiuser_bridge.hpp"

extern "C" {
int lswCreate();
void lswDestroy(int handle);
int lswLoadMovie(int handle, const char* source, const std::uint8_t* bytes, int byte_count, const char* params_text);
void lswSetExternalParams(int handle, const char* params_text);
void lswSetPreloadCasts(int handle, int preload_casts);
void lswSetTempoOverride(int handle, int tempo);
int lswTempo(int handle);
int lswBaseTempo(int handle);
void lswPlay(int handle);
void lswPause(int handle);
void lswStop(int handle);
int lswTick(int handle);
int lswRenderFrame(int handle);
int lswFrameWidth(int handle);
int lswFrameHeight(int handle);
int lswFrameByteLength(int handle);
int lswDirectorKeyFromBrowser(int browser_key_code);
const char* lswFrameInfoJson(int handle);
const char* lswLastError(int handle);
const char* lswPollFetchRequests(int handle);
void lswDrainFetchRequests(int handle);
const char* lswPollMultiuserRequests(int handle);
void lswDrainMultiuserRequests(int handle);
const char* lswPollAudioCommands(int handle);
void lswDrainAudioCommands(int handle);
void lswAudioStopped(int handle, int channel_num);
void lswMultiuserConnected(int handle, int instance_id);
void lswMultiuserDisconnected(int handle, int instance_id);
void lswMultiuserError(int handle, int instance_id, int error_code);
void lswMultiuserMessageBytes(int handle, int instance_id, const std::uint8_t* bytes, int byte_count);
void lswMouseMove(int handle, int stage_x, int stage_y);
void lswMouseDown(int handle, int stage_x, int stage_y, int right_button);
void lswMouseUp(int handle, int stage_x, int stage_y, int right_button);
void lswBlur(int handle);
void lswKeyDown(int handle, int browser_key_code, const char* key_text, int shift, int ctrl, int alt);
void lswKeyUp(int handle, int browser_key_code, const char* key_text, int shift, int ctrl, int alt);
void lswPasteText(int handle, const char* text);
const char* lswSelectedText(int handle);
void lswSelectAll(int handle);
const char* lswCutSelectedText(int handle);
}

namespace {

namespace fs = std::filesystem;
using libreshockwave::lingo::Datum;
using libreshockwave::lingo::xtra::MultiuserNetBridge;
using libreshockwave::player::xtra::QueuedMultiuserBridge;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool hasDirectorExtension(const fs::path& path) {
    const auto ext = path.extension().string();
    return ext == ".dcr" || ext == ".dir" || ext == ".dxr" || ext == ".cct" || ext == ".cst";
}

std::vector<std::uint8_t> readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open " + path.string());
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

fs::path findFirstMovie(const fs::path& root) {
    if (root.empty()) {
        return {};
    }
    if (fs::is_regular_file(root) && hasDirectorExtension(root)) {
        return root;
    }
    if (!fs::is_directory(root)) {
        return {};
    }
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && hasDirectorExtension(entry.path())) {
            return entry.path();
        }
    }
    return {};
}

void probeQueuedMultiuserBridge() {
    QueuedMultiuserBridge bridge;
    require(QueuedMultiuserBridge::serializeWireContent("", "body") == "body", "multiuser body serialization failed");
    require(QueuedMultiuserBridge::serializeWireContent("subject", "body") == "subject body",
            "multiuser subject serialization failed");
    require(QueuedMultiuserBridge::decodeShockwaveCommand('C', 'D') == 196,
            "multiuser command decoding failed");

    MultiuserNetBridge::ConnectOptions chat_options;
    chat_options.user_name = "me";
    bridge.requestConnect(7, "chat.example", 1234, 1, chat_options);
    require(bridge.pendingRequests().size() == 1, "multiuser connect was not queued");
    const auto* connect = bridge.getRequest(0);
    require(connect != nullptr, "multiuser queued connect missing");
    require(connect->type == QueuedMultiuserBridge::REQ_CONNECT, "multiuser connect request type mismatch");
    require(connect->host == "chat.example" && connect->port == 1234, "multiuser connect target mismatch");

    bridge.notifyConnected(7);
    require(bridge.isConnected(7), "multiuser connected state was not recorded");
    auto messages = bridge.pollMessages(7);
    require(messages.size() == 1 && messages[0].subject == "ConnectToNetServer",
            "multiuser connect callback message missing");
    require(bridge.pendingRequests().size() == 1, "multiuser connect should not queue non-SMUS data");

    bridge.requestSend(7, Datum::of(std::string("room")), "CHAT", Datum::of(std::string("hello")));
    require(bridge.pendingRequests().size() == 2, "multiuser send was not queued");
    const auto* send = bridge.getRequest(1);
    require(send != nullptr && send->type == QueuedMultiuserBridge::REQ_SEND, "multiuser send request missing");
    require(send->wireContent() == "CHAT hello", "multiuser send wire content mismatch");
    require((send->wireBytes() == std::vector<std::uint8_t>{'C', 'H', 'A', 'T', ' ', 'h', 'e', 'l', 'l', 'o'}),
            "multiuser send wire bytes mismatch");

    bridge.deliverMessageBytes(7, {'A', 0x00, 0xFF});
    messages = bridge.pollMessages(7);
    require(messages.size() == 1, "multiuser delivered byte message missing");
    require(messages[0].sender_id.empty() && messages[0].subject.empty(),
            "multiuser delivered byte message metadata mismatch");
    require(messages[0].content.stringValue() == std::string({'A', '\0', static_cast<char>(0xFF)}),
            "multiuser delivered byte content mismatch");

    bridge.requestDisconnect(7);
    require(!bridge.isConnected(7), "multiuser disconnect did not clear connected state");
    require(bridge.pendingRequests().back().type == QueuedMultiuserBridge::REQ_DISCONNECT,
            "multiuser disconnect was not queued");

    QueuedMultiuserBridge smus;
    MultiuserNetBridge::ConnectOptions smus_options;
    smus_options.user_name = "alice";
    smus.requestConnect(3, "smus.example", 1235, 0, smus_options);
    smus.drainPendingRequests();
    smus.notifyConnected(3);
    require(smus.pendingRequests().size() == 1, "SMUS logon was not queued after connect");
    const auto* logon = smus.getRequest(0);
    require(logon != nullptr && logon->subject == "Logon", "SMUS logon request missing");
    const auto logon_bytes = logon->wireBytes();
    require(logon_bytes.size() == 80 && logon_bytes[0] == 114 && logon_bytes[1] == 0,
            "SMUS logon frame format mismatch");
}

void probeLoadedMovieBridge(int handle) {
    lswSetExternalParams(handle, "sw1=one=1;two=2\nsw2=value");
    lswSetPreloadCasts(handle, 1);
    const int base_tempo = lswBaseTempo(handle);
    require(base_tempo > 0, "base tempo must be positive");
    lswSetTempoOverride(handle, 5);
    require(lswTempo(handle) == 5, "tempo override was not applied through C ABI");
    lswSetTempoOverride(handle, 0);
    require(lswTempo(handle) > 0, "tempo reset produced invalid tempo");

    lswPause(handle);
    lswPlay(handle);
    (void)lswTick(handle);
    require(lswRenderFrame(handle) != 0, "render after tick failed");

    lswMouseMove(handle, 8, 9);
    lswMouseDown(handle, 8, 9, 0);
    lswMouseUp(handle, 8, 9, 0);
    lswKeyDown(handle, 13, "\r", 0, 0, 0);
    lswKeyUp(handle, 13, "\r", 0, 0, 0);
    lswPasteText(handle, "probe");
    lswSelectAll(handle);
    (void)lswSelectedText(handle);
    (void)lswCutSelectedText(handle);
    lswBlur(handle);

    require(lswPollFetchRequests(handle) != nullptr, "fetch request poll returned null");
    lswDrainFetchRequests(handle);
    require(lswPollMultiuserRequests(handle) != nullptr, "multiuser request poll returned null");
    lswDrainMultiuserRequests(handle);
    require(lswPollAudioCommands(handle) != nullptr, "audio command poll returned null");
    lswDrainAudioCommands(handle);
    lswAudioStopped(handle, 1);
    const std::uint8_t bytes[] = {'P', 'I', 'N', 'G'};
    lswMultiuserConnected(handle, 99);
    lswMultiuserMessageBytes(handle, 99, bytes, static_cast<int>(std::size(bytes)));
    lswMultiuserError(handle, 99, -2);
    lswMultiuserDisconnected(handle, 99);
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(lswDirectorKeyFromBrowser(13) == 36, "Enter key mapping failed");
        probeQueuedMultiuserBridge();

        const int handle = lswCreate();
        require(handle > 0, "create failed");
        require(lswPollAudioCommands(handle) != nullptr, "empty audio command poll returned null");
        lswDrainAudioCommands(handle);
        lswAudioStopped(handle, 1);

        if (argc > 1) {
            const fs::path movie = findFirstMovie(argv[1]);
            if (movie.empty()) {
                std::cout << "WASM bridge probe: no Director movie found under " << argv[1] << '\n';
                lswDestroy(handle);
                return 0;
            }
            const auto bytes = readFile(movie);
            if (lswLoadMovie(handle, movie.string().c_str(), bytes.data(), static_cast<int>(bytes.size()), "") == 0) {
                std::cerr << "load failed: " << lswLastError(handle) << '\n';
                lswDestroy(handle);
                return 1;
            }
            probeLoadedMovieBridge(handle);
            if (lswRenderFrame(handle) == 0 || lswFrameByteLength(handle) <= 0) {
                std::cerr << "render failed: " << lswLastError(handle) << '\n';
                lswDestroy(handle);
                return 1;
            }
            std::cout << "WASM bridge probe: rendered " << movie
                      << " at " << lswFrameWidth(handle) << 'x' << lswFrameHeight(handle)
                      << " info=" << lswFrameInfoJson(handle) << '\n';
        } else {
            std::cout << "WASM bridge probe: C ABI exports available\n";
        }

        lswDestroy(handle);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WASM bridge probe failed: " << error.what() << '\n';
        return 1;
    }
}
