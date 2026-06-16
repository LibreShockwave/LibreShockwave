#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "libreshockwave/lingo/Datum.hpp"
#include "libreshockwave/player/xtra/QueuedMultiuserBridge.hpp"

extern "C" {
int lsw_create();
void lsw_destroy(int handle);
int lsw_load_movie(int handle, const char* source, const std::uint8_t* bytes, int byteCount, const char* paramsText);
void lsw_set_external_params(int handle, const char* paramsText);
void lsw_set_preload_casts(int handle, int preloadCasts);
void lsw_set_tempo_override(int handle, int tempo);
int lsw_tempo(int handle);
int lsw_base_tempo(int handle);
void lsw_play(int handle);
void lsw_pause(int handle);
void lsw_stop(int handle);
int lsw_tick(int handle);
int lsw_render_frame(int handle);
int lsw_frame_width(int handle);
int lsw_frame_height(int handle);
int lsw_frame_byte_length(int handle);
int lsw_director_key_from_browser(int browserKeyCode);
const char* lsw_frame_info_json(int handle);
const char* lsw_last_error(int handle);
const char* lsw_poll_fetch_requests(int handle);
void lsw_drain_fetch_requests(int handle);
const char* lsw_poll_multiuser_requests(int handle);
void lsw_drain_multiuser_requests(int handle);
void lsw_multiuser_connected(int handle, int instanceId);
void lsw_multiuser_disconnected(int handle, int instanceId);
void lsw_multiuser_error(int handle, int instanceId, int errorCode);
void lsw_multiuser_message_bytes(int handle, int instanceId, const std::uint8_t* bytes, int byteCount);
void lsw_mouse_move(int handle, int stageX, int stageY);
void lsw_mouse_down(int handle, int stageX, int stageY, int rightButton);
void lsw_mouse_up(int handle, int stageX, int stageY, int rightButton);
void lsw_blur(int handle);
void lsw_key_down(int handle, int browserKeyCode, const char* keyText, int shift, int ctrl, int alt);
void lsw_key_up(int handle, int browserKeyCode, const char* keyText, int shift, int ctrl, int alt);
void lsw_paste_text(int handle, const char* text);
const char* lsw_selected_text(int handle);
void lsw_select_all(int handle);
const char* lsw_cut_selected_text(int handle);
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

    MultiuserNetBridge::ConnectOptions chatOptions;
    chatOptions.userName = "me";
    bridge.requestConnect(7, "chat.example", 1234, 1, chatOptions);
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
    require(messages[0].senderID.empty() && messages[0].subject.empty(),
            "multiuser delivered byte message metadata mismatch");
    require(messages[0].content.stringValue() == std::string({'A', '\0', static_cast<char>(0xFF)}),
            "multiuser delivered byte content mismatch");

    bridge.requestDisconnect(7);
    require(!bridge.isConnected(7), "multiuser disconnect did not clear connected state");
    require(bridge.pendingRequests().back().type == QueuedMultiuserBridge::REQ_DISCONNECT,
            "multiuser disconnect was not queued");

    QueuedMultiuserBridge smus;
    MultiuserNetBridge::ConnectOptions smusOptions;
    smusOptions.userName = "alice";
    smus.requestConnect(3, "smus.example", 1235, 0, smusOptions);
    smus.drainPendingRequests();
    smus.notifyConnected(3);
    require(smus.pendingRequests().size() == 1, "SMUS logon was not queued after connect");
    const auto* logon = smus.getRequest(0);
    require(logon != nullptr && logon->subject == "Logon", "SMUS logon request missing");
    const auto logonBytes = logon->wireBytes();
    require(logonBytes.size() == 80 && logonBytes[0] == 114 && logonBytes[1] == 0,
            "SMUS logon frame format mismatch");
}

void probeLoadedMovieBridge(int handle) {
    lsw_set_external_params(handle, "sw1=one=1;two=2\nsw2=value");
    lsw_set_preload_casts(handle, 1);
    const int baseTempo = lsw_base_tempo(handle);
    require(baseTempo > 0, "base tempo must be positive");
    lsw_set_tempo_override(handle, 5);
    require(lsw_tempo(handle) == 5, "tempo override was not applied through C ABI");
    lsw_set_tempo_override(handle, 0);
    require(lsw_tempo(handle) > 0, "tempo reset produced invalid tempo");

    lsw_pause(handle);
    lsw_play(handle);
    (void)lsw_tick(handle);
    require(lsw_render_frame(handle) != 0, "render after tick failed");

    lsw_mouse_move(handle, 8, 9);
    lsw_mouse_down(handle, 8, 9, 0);
    lsw_mouse_up(handle, 8, 9, 0);
    lsw_key_down(handle, 13, "\r", 0, 0, 0);
    lsw_key_up(handle, 13, "\r", 0, 0, 0);
    lsw_paste_text(handle, "probe");
    lsw_select_all(handle);
    (void)lsw_selected_text(handle);
    (void)lsw_cut_selected_text(handle);
    lsw_blur(handle);

    require(lsw_poll_fetch_requests(handle) != nullptr, "fetch request poll returned null");
    lsw_drain_fetch_requests(handle);
    require(lsw_poll_multiuser_requests(handle) != nullptr, "multiuser request poll returned null");
    lsw_drain_multiuser_requests(handle);
    const std::uint8_t bytes[] = {'P', 'I', 'N', 'G'};
    lsw_multiuser_connected(handle, 99);
    lsw_multiuser_message_bytes(handle, 99, bytes, static_cast<int>(std::size(bytes)));
    lsw_multiuser_error(handle, 99, -2);
    lsw_multiuser_disconnected(handle, 99);
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(lsw_director_key_from_browser(13) == 36, "Enter key mapping failed");
        probeQueuedMultiuserBridge();

        const int handle = lsw_create();
        require(handle > 0, "create failed");

        if (argc > 1) {
            const fs::path movie = findFirstMovie(argv[1]);
            if (movie.empty()) {
                std::cout << "WASM bridge probe: no Director movie found under " << argv[1] << '\n';
                lsw_destroy(handle);
                return 0;
            }
            const auto bytes = readFile(movie);
            if (lsw_load_movie(handle, movie.string().c_str(), bytes.data(), static_cast<int>(bytes.size()), "") == 0) {
                std::cerr << "load failed: " << lsw_last_error(handle) << '\n';
                lsw_destroy(handle);
                return 1;
            }
            probeLoadedMovieBridge(handle);
            if (lsw_render_frame(handle) == 0 || lsw_frame_byte_length(handle) <= 0) {
                std::cerr << "render failed: " << lsw_last_error(handle) << '\n';
                lsw_destroy(handle);
                return 1;
            }
            std::cout << "WASM bridge probe: rendered " << movie
                      << " at " << lsw_frame_width(handle) << 'x' << lsw_frame_height(handle)
                      << " info=" << lsw_frame_info_json(handle) << '\n';
        } else {
            std::cout << "WASM bridge probe: C ABI exports available\n";
        }

        lsw_destroy(handle);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WASM bridge probe failed: " << error.what() << '\n';
        return 1;
    }
}
