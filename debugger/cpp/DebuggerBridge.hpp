#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace libreshockwave::player {
class Player;
}

namespace libreshockwave {
class DirectorFile;
}

// Internal accessors implemented in WasmBridge.cpp — the two translation
// units are compiled into the same .wasm module so these resolve at link
// time.  They return the Player / DirectorFile for the given context handle
// so the debugger bridge can reach the debug controller, VM, cast libs, etc.

libreshockwave::player::Player* lsw_internal_get_player(int handle);
std::shared_ptr<libreshockwave::DirectorFile> lsw_internal_get_file(int handle);
const char* lsw_internal_json_scratch(int handle, std::string value);
