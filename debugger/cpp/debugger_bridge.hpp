#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace libreshockwave::player {
class Player;
}

namespace libreshockwave {
namespace moviesource {
class MovieSource;
}
}

// Internal accessors implemented in wasm_bridge.cpp — the two translation
// units are compiled into the same .wasm module so these resolve at link
// time.  They return the Player / MovieSource for the given context handle
// so the debugger bridge can reach the debug controller, VM, cast libs, etc.

libreshockwave::player::Player* lswInternalGetPlayer(int handle);
std::shared_ptr<libreshockwave::moviesource::MovieSource> lswInternalGetFile(int handle);
const char* lswInternalJsonScratch(int handle, std::string value);
