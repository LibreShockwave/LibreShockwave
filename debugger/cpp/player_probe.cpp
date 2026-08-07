// Temporary diagnostic probe — replicates DebuggerContext's playback path
// (play() on main thread, tick loop) against a movie file. Prints per-tick
// state so we can see whether the movie actually progresses.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/player/Player.hpp"
#include "libreshockwave/player/debug/DebugController.hpp"
#include "libreshockwave/player/render/pipeline/FrameSnapshot.hpp"

static std::vector<std::uint8_t> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    in.seekg(0, std::ios::end);
    const auto n = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(n));
    if (!data.empty()) {
        in.read(reinterpret_cast<char*>(data.data()), n);
    }
    return data;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: player_probe <movie> [ticks]\n");
        return 2;
    }
    const int ticks = argc > 2 ? std::atoi(argv[2]) : 60;

    auto file = libreshockwave::DirectorFile::load(readFile(argv[1]));
    if (!file) {
        std::printf("LOAD FAILED\n");
        return 1;
    }
    file->setBasePath(argv[1]);
    std::printf("movie: scripts=%zu tempo=%d\n", file->scripts().size(), file->tempo());

    libreshockwave::player::Player player(file);
    player.setDebugEnabled(true);
    auto dc = std::make_shared<libreshockwave::player::debug::DebugController>();
    player.setDebugController(dc);

    player.play();
    std::printf("after play: state=%d frame=%d\n", (int)player.state(), player.currentFrame());

    for (int i = 0; i < ticks; ++i) {
        (void)player.tick();
        auto snap = player.frameSnapshot();
        const auto* img = snap.stageImage.get();
        std::printf("tick %3d: state=%d frame=%d/%d paused=%d stage=%dx%d\n",
                    i, (int)player.state(), player.currentFrame(), player.frameCount(),
                    dc->isPaused() ? 1 : 0,
                    img ? img->width() : -1, img ? img->height() : -1);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    player.shutdown();
    return 0;
}
