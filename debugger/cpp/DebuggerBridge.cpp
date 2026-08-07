#include "DebuggerBridge.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/chunks/ScriptChunk.hpp"
#include "libreshockwave/format/ScriptFormatUtils.hpp"
#include "libreshockwave/lingo/Datum.hpp"
#include "libreshockwave/lingo/decompiler/LingoDecompiler.hpp"
#include "libreshockwave/lingo/vm/LingoVM.hpp"
#include "libreshockwave/lingo/vm/datum/DatumFormatter.hpp"
#include "libreshockwave/lingo/vm/trace/InstructionAnnotator.hpp"
#include "libreshockwave/player/Player.hpp"
#include "libreshockwave/player/cast/CastLib.hpp"
#include "libreshockwave/player/cast/CastLibManager.hpp"
#include "libreshockwave/player/debug/BreakpointManager.hpp"
#include "libreshockwave/player/debug/DebugController.hpp"
#include "libreshockwave/player/debug/DebugSnapshot.hpp"

namespace {

using libreshockwave::lingo::Datum;
using libreshockwave::lingo::decompiler::LingoDecompiler;
using libreshockwave::lingo::vm::datum::formatBrief;
using libreshockwave::lingo::vm::trace::InstructionAnnotator;
using libreshockwave::player::debug::DebugController;
using libreshockwave::player::debug::DebugSnapshot;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string jsonEscape(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const unsigned char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    constexpr char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(ch >> 4U) & 0xFU]);
                    out.push_back(hex[ch & 0xFU]);
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return out;
}

void appendJsonString(std::ostringstream& out, std::string_view value) {
    out << '"' << jsonEscape(value) << '"';
}

std::string datumToJson(const Datum& value) {
    if (value.isVoid()) {
        return "null";
    }
    if (value.isInt()) {
        return std::to_string(value.intValue());
    }
    if (value.isFloat()) {
        return std::to_string(value.floatValue());
    }
    if (value.isString()) {
        return "\"" + jsonEscape(value.stringValue()) + "\"";
    }
    if (value.isList()) {
        std::ostringstream out;
        out << '[';
        bool first = true;
        for (const auto& item : value.listValue().items()) {
            if (!first) out << ',';
            first = false;
            out << datumToJson(item);
        }
        out << ']';
        return out.str();
    }
    if (value.isPropList()) {
        std::ostringstream out;
        out << '{';
        bool first = true;
        for (const auto& [key, val] : value.propListValue().properties()) {
            if (!first) out << ',';
            first = false;
            appendJsonString(out, key.stringValue());
            out << ':' << datumToJson(val);
        }
        out << '}';
        return out.str();
    }
    // Fallback: use DatumFormatter
    return "\"" + jsonEscape(formatBrief(value)) + "\"";
}

std::string scriptTypeName(const libreshockwave::chunks::ScriptChunk& script) {
    return std::string(libreshockwave::format::getScriptTypeName(script.resolvedScriptType()));
}

// ---------------------------------------------------------------------------
// Debug context management
// ---------------------------------------------------------------------------

struct DebugContext {
    std::shared_ptr<DebugController> controller;
};

std::unordered_map<int, std::unique_ptr<DebugContext>> debugContexts;

DebugContext* getDebugContext(int handle) {
    auto* player = lsw_internal_get_player(handle);
    if (player == nullptr) {
        return nullptr;
    }
    auto it = debugContexts.find(handle);
    if (it != debugContexts.end()) {
        return it->second.get();
    }
    // Lazy-init: create a DebugController and attach it to the player.
    auto ctx = std::make_unique<DebugContext>();
    ctx->controller = std::make_shared<DebugController>();
    ctx->controller->setNonBlockingMode(true);  // WASM: cooperative yield, no threads
    player->setDebugController(ctx->controller);
    player->setDebugEnabled(true);
    auto* result = ctx.get();
    debugContexts.emplace(handle, std::move(ctx));
    return result;
}

const char* scratch(int handle, std::string value) {
    return lsw_internal_json_scratch(handle, std::move(value));
}

// ---------------------------------------------------------------------------
// JSON builders
// ---------------------------------------------------------------------------

std::string buildMoviesJson(libreshockwave::player::Player& player) {
    std::ostringstream out;
    out << '[';
    bool first = true;
    for (const auto& [number, castLib] : player.castLibManager().castLibs()) {
        if (castLib == nullptr) continue;
        if (!first) out << ',';
        first = false;
        out << "{\"number\":" << number
            << ",\"name\":";
        appendJsonString(out, castLib->name());
        out << ",\"fileName\":";
        appendJsonString(out, castLib->fileName());
        out << ",\"scriptCount\":" << castLib->allScripts().size()
            << ",\"memberCount\":" << castLib->memberCount()
            << ",\"isExternal\":" << (castLib->isExternal() ? "true" : "false")
            << ",\"isLoaded\":" << (castLib->isLoaded() ? "true" : "false")
            << '}';
    }
    out << ']';
    return out.str();
}

std::string buildScriptsJson(libreshockwave::player::Player& player, int castLibNumber) {
    auto castLib = player.castLibManager().getCastLib(castLibNumber);
    std::ostringstream out;
    out << '[';
    if (castLib != nullptr) {
        bool first = true;
        for (const auto& script : castLib->allScripts()) {
            if (script == nullptr) continue;
            if (!first) out << ',';
            first = false;
            out << "{\"scriptId\":" << script->id().value()
                << ",\"name\":";
            appendJsonString(out, script->displayName());
            out << ",\"type\":";
            appendJsonString(out, scriptTypeName(*script));
            out << ",\"handlerCount\":" << script->handlers().size()
                << '}';
        }
    }
    out << ']';
    return out.str();
}

std::string buildHandlerCodeJson(libreshockwave::player::Player& player,
                                  int scriptId,
                                  const std::string& handlerName) {
    // Find the script chunk by scanning all cast libs.
    std::shared_ptr<libreshockwave::chunks::ScriptChunk> foundScript;
    const libreshockwave::chunks::ScriptChunk::Handler* foundHandler = nullptr;
    const libreshockwave::chunks::ScriptNamesChunk* foundNames = nullptr;

    for (const auto& [number, castLib] : player.castLibManager().castLibs()) {
        if (castLib == nullptr) continue;
        for (const auto& script : castLib->allScripts()) {
            if (script == nullptr) continue;
            if (script->id().value() != scriptId) continue;
            foundScript = script;
            foundNames = castLib->scriptNames().get();
            foundHandler = script->findHandlerPtr(handlerName, foundNames);
            if (foundHandler != nullptr) break;
        }
        if (foundHandler != nullptr) break;
    }

    // Also try the player's main DirectorFile scripts
    if (foundHandler == nullptr) {
        auto file = player.file();
        if (file != nullptr) {
            for (const auto& script : file->scripts()) {
                if (script == nullptr) continue;
                if (script->id().value() != scriptId) continue;
                foundScript = script;
                foundHandler = script->findHandlerPtr(handlerName);
                if (foundHandler != nullptr) break;
            }
        }
    }

    std::ostringstream out;
    out << '{';

    // When no handler specified, return script overview with handler list
    if (foundScript != nullptr && handlerName.empty()) {
        out << "\"scriptName\":";
        appendJsonString(out, foundScript->displayName());
        out << ",\"handlerName\":\"\"";
        out << ",\"argCount\":0,\"localCount\":0,\"globalsCount\":0,\"bytecodeLength\":0";
        // Handler names — always use the script's own file for name resolution
        out << ",\"handlers\":[";
        bool firstH = true;
        for (const auto& handler : foundScript->handlers()) {
            if (!firstH) out << ',';
            firstH = false;
            appendJsonString(out, foundScript->resolveName(handler.nameId));
        }
        out << ']';
        // Globals
        out << ",\"globals\":[";
        bool firstGlob = true;
        for (const auto& g : foundScript->globals()) {
            if (!firstGlob) out << ',';
            firstGlob = false;
            appendJsonString(out, foundScript->resolveName(g.nameId));
        }
        out << ']';
        // Literals
        out << ",\"literals\":[";
        bool firstLit = true;
        for (const auto& lit : foundScript->literals()) {
            if (!firstLit) out << ',';
            firstLit = false;
            out << "{\"type\":" << lit.type
                << ",\"offset\":" << lit.offset
                << ",\"value\":";
            if (lit.value.index() == 1) {
                appendJsonString(out, std::get<std::string>(lit.value));
            } else if (lit.value.index() == 2) {
                out << std::get<int>(lit.value);
            } else {
                out << "null";
            }
            out << '}';
        }
        out << ']';
        // Bytecode + decompiled empty
        out << ",\"bytecode\":[],\"decompiled\":[]";
    } else if (foundScript != nullptr && foundHandler != nullptr) {
        out << "\"scriptName\":";
        appendJsonString(out, foundScript->displayName());
        out << ",\"handlerName\":";
        appendJsonString(out, handlerName);
        out << ",\"argCount\":" << foundHandler->argCount
            << ",\"localCount\":" << foundHandler->localCount
            << ",\"globalsCount\":" << foundHandler->globalsCount
            << ",\"bytecodeLength\":" << foundHandler->bytecodeLength;

        // Build bytecode array
        out << ",\"bytecode\":[";
        bool firstInstr = true;
        for (const auto& instr : foundHandler->instructions) {
            if (!firstInstr) out << ',';
            firstInstr = false;
            out << "{\"offset\":" << instr.offset
                << ",\"opcode\":";
            appendJsonString(out, std::string(libreshockwave::lingo::mnemonic(instr.opcode)));
            out << ",\"argument\":" << instr.argument
                << ",\"annotation\":";
            appendJsonString(out, InstructionAnnotator::annotate(
                *foundScript, foundHandler, instr, foundNames, true));
            out << '}';
        }
        out << ']';

        // Build decompiled lines array
        LingoDecompiler decompiler;
        auto decompiled = decompiler.decompileHandlerWithMapping(*foundHandler, *foundScript, foundNames);
        out << ",\"decompiled\":[";
        bool firstLine = true;
        for (const auto& line : decompiled.lines) {
            if (!firstLine) out << ',';
            firstLine = false;
            out << "{\"text\":";
            appendJsonString(out, line.text);
            out << ",\"bytecodeOffset\":" << line.bytecodeOffset
                << '}';
        }
        out << ']';

        // Literals
        out << ",\"literals\":[";
        bool firstLit = true;
        for (const auto& lit : foundScript->literals()) {
            if (!firstLit) out << ',';
            firstLit = false;
            out << "{\"type\":" << lit.type
                << ",\"offset\":" << lit.offset
                << ",\"value\":";
            if (lit.value.index() == 1) {  // string
                appendJsonString(out, std::get<std::string>(lit.value));
            } else if (lit.value.index() == 2) {  // int
                out << std::get<int>(lit.value);
            } else {
                out << "null";
            }
            out << '}';
        }
        out << ']';

        // Global names
        out << ",\"globals\":[";
        bool firstGlob = true;
        for (const auto& g : foundScript->globals()) {
            if (!firstGlob) out << ',';
            firstGlob = false;
            appendJsonString(out, foundScript->resolveName(g.nameId, foundNames));
        }
        out << ']';
    } else {
        out << "\"error\":\"Handler not found\"";
    }

    out << '}';
    return out.str();
}

std::string buildSnapshotJson(const DebugSnapshot& snap) {
    std::ostringstream out;
    out << '{'
        << "\"scriptId\":" << snap.scriptId
        << ",\"scriptName\":";
    appendJsonString(out, snap.scriptName);
    out << ",\"handlerName\":";
    appendJsonString(out, snap.handlerName);
    out << ",\"instructionOffset\":" << snap.instructionOffset
        << ",\"instructionIndex\":" << snap.instructionIndex
        << ",\"opcode\":";
    appendJsonString(out, snap.opcode);
    out << ",\"argument\":" << snap.argument
        << ",\"annotation\":";
    appendJsonString(out, snap.annotation);

    // All instructions with breakpoint flags
    out << ",\"allInstructions\":[";
    bool firstInstr = true;
    for (const auto& instr : snap.allInstructions) {
        if (!firstInstr) out << ',';
        firstInstr = false;
        out << "{\"offset\":" << instr.offset
            << ",\"index\":" << instr.index
            << ",\"opcode\":";
        appendJsonString(out, instr.opcode);
        out << ",\"argument\":" << instr.argument
            << ",\"annotation\":";
        appendJsonString(out, instr.annotation);
        out << ",\"hasBreakpoint\":" << (instr.hasBreakpoint ? "true" : "false")
            << '}';
    }
    out << ']';

    // Stack
    out << ",\"stack\":[";
    bool firstStack = true;
    for (const auto& val : snap.stack) {
        if (!firstStack) out << ',';
        firstStack = false;
        out << datumToJson(val);
    }
    out << ']';

    // Locals
    out << ",\"locals\":{";
    bool firstLocal = true;
    for (const auto& [name, val] : snap.locals) {
        if (!firstLocal) out << ',';
        firstLocal = false;
        appendJsonString(out, name);
        out << ':' << datumToJson(val);
    }
    out << '}';

    // Globals
    out << ",\"globals\":{";
    bool firstGlob = true;
    for (const auto& [name, val] : snap.globals) {
        if (!firstGlob) out << ',';
        firstGlob = false;
        appendJsonString(out, name);
        out << ':' << datumToJson(val);
    }
    out << '}';

    // Arguments
    out << ",\"arguments\":[";
    bool firstArg = true;
    for (const auto& val : snap.arguments) {
        if (!firstArg) out << ',';
        firstArg = false;
        out << datumToJson(val);
    }
    out << ']';

    // Receiver
    out << ",\"receiver\":";
    if (snap.receiver.has_value()) {
        out << datumToJson(*snap.receiver);
    } else {
        out << "null";
    }

    // Instance properties (from the script instance / "me" receiver)
    out << ",\"properties\":{";
    if (snap.receiver.has_value() && snap.receiver->type() == libreshockwave::lingo::DatumType::ScriptInstanceRef) {
        const auto& props = snap.receiver->scriptInstanceValue().properties();
        bool firstProp = true;
        for (const auto& [name, val] : props) {
            if (!firstProp) out << ',';
            firstProp = false;
            appendJsonString(out, name);
            out << ':' << datumToJson(val);
        }
    }
    out << '}';

    // Call stack
    out << ",\"callStack\":[";
    bool firstCs = true;
    for (const auto& frame : snap.callStack) {
        if (!firstCs) out << ',';
        firstCs = false;
        out << "{\"scriptId\":" << frame.scriptId
            << ",\"scriptName\":";
        appendJsonString(out, frame.scriptName);
        out << ",\"handlerName\":";
        appendJsonString(out, frame.handlerName);
        out << '}';
    }
    out << ']';

    // Watch results
    out << ",\"watchResults\":[";
    bool firstWr = true;
    for (const auto& wr : snap.watchResults) {
        if (!firstWr) out << ',';
        firstWr = false;
        out << "{\"id\":";
        appendJsonString(out, wr.id);
        out << ",\"expression\":";
        appendJsonString(out, wr.expression);
        out << ",\"value\":";
        if (wr.lastValue.has_value()) {
            out << datumToJson(*wr.lastValue);
        } else {
            out << "null";
        }
        out << ",\"error\":";
        if (wr.lastError.has_value()) {
            appendJsonString(out, *wr.lastError);
        } else {
            out << "null";
        }
        out << '}';
    }
    out << ']';

    out << '}';
    return out.str();
}

std::string buildBreakpointsJson(DebugController& controller) {
    std::ostringstream out;
    out << '[';
    bool first = true;
    for (const auto& bp : controller.breakpointManager().getAllBreakpoints()) {
        if (!first) out << ',';
        first = false;
        out << "{\"scriptId\":" << bp.scriptId
            << ",\"handlerName\":";
        appendJsonString(out, bp.handlerName);
        out << ",\"offset\":" << bp.offset
            << ",\"enabled\":" << (bp.enabled ? "true" : "false")
            << '}';
    }
    out << ']';
    return out.str();
}

}  // namespace

// ---------------------------------------------------------------------------
// Public C ABI
// ---------------------------------------------------------------------------

extern "C" {

// -- Enumeration ------------------------------------------------------------

EMSCRIPTEN_KEEPALIVE const char* lsw_debug_list_movies_json(int handle) {
    auto* player = lsw_internal_get_player(handle);
    if (player == nullptr) {
        return "[]";
    }
    return scratch(handle, buildMoviesJson(*player));
}

EMSCRIPTEN_KEEPALIVE const char* lsw_debug_list_scripts_json(int handle, int castLibNumber) {
    auto* player = lsw_internal_get_player(handle);
    if (player == nullptr) {
        return "[]";
    }
    return scratch(handle, buildScriptsJson(*player, castLibNumber));
}

EMSCRIPTEN_KEEPALIVE const char* lsw_debug_get_handler_code_json(int handle,
                                                                  int scriptId,
                                                                  const char* handlerName) {
    auto* player = lsw_internal_get_player(handle);
    if (player == nullptr || handlerName == nullptr) {
        return "{\"error\":\"Invalid player or handler name\"}";
    }
    return scratch(handle, buildHandlerCodeJson(*player, scriptId, std::string(handlerName)));
}

// -- Execution control ------------------------------------------------------

EMSCRIPTEN_KEEPALIVE void lsw_debug_pause(int handle) {
    auto* dbg = getDebugContext(handle);
    if (dbg != nullptr) {
        dbg->controller->pause();
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_debug_continue(int handle) {
    auto* dbg = getDebugContext(handle);
    if (dbg != nullptr) {
        dbg->controller->continueExecution();
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_debug_step_into(int handle) {
    auto* dbg = getDebugContext(handle);
    if (dbg != nullptr) {
        dbg->controller->stepInto();
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_debug_step_over(int handle) {
    auto* dbg = getDebugContext(handle);
    if (dbg != nullptr) {
        dbg->controller->stepOver();
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_debug_step_out(int handle) {
    auto* dbg = getDebugContext(handle);
    if (dbg != nullptr) {
        dbg->controller->stepOut();
    }
}

EMSCRIPTEN_KEEPALIVE int lsw_debug_is_paused(int handle) {
    auto* dbg = getDebugContext(handle);
    if (dbg == nullptr) {
        return 0;
    }
    return dbg->controller->isPaused() ? 1 : 0;
}

// -- Breakpoints ------------------------------------------------------------

EMSCRIPTEN_KEEPALIVE const char* lsw_debug_toggle_breakpoint(int handle,
                                                               int scriptId,
                                                               const char* handlerName,
                                                               int offset) {
    auto* dbg = getDebugContext(handle);
    if (dbg == nullptr || handlerName == nullptr) {
        return "{\"active\":false}";
    }
    bool active = dbg->controller->toggleBreakpoint(scriptId, std::string(handlerName), offset);
    return scratch(handle, std::string("{\"active\":") + (active ? "true" : "false") + "}");
}

EMSCRIPTEN_KEEPALIVE void lsw_debug_clear_breakpoints(int handle) {
    auto* dbg = getDebugContext(handle);
    if (dbg != nullptr) {
        dbg->controller->clearAllBreakpoints();
    }
}

EMSCRIPTEN_KEEPALIVE const char* lsw_debug_list_breakpoints_json(int handle) {
    auto* dbg = getDebugContext(handle);
    if (dbg == nullptr) {
        return "[]";
    }
    return scratch(handle, buildBreakpointsJson(*dbg->controller));
}

// -- State ------------------------------------------------------------------

EMSCRIPTEN_KEEPALIVE const char* lsw_debug_get_snapshot_json(int handle) {
    auto* dbg = getDebugContext(handle);
    if (dbg == nullptr) {
        return "{\"scriptId\":0,\"scriptName\":\"\",\"handlerName\":\"\",\"instructionOffset\":-1}";
    }
    auto snap = dbg->controller->currentSnapshot();
    if (!snap.has_value()) {
        return "{\"scriptId\":0,\"scriptName\":\"\",\"handlerName\":\"\",\"instructionOffset\":-1}";
    }
    return scratch(handle, buildSnapshotJson(*snap));
}

// -- Watch expressions ------------------------------------------------------

EMSCRIPTEN_KEEPALIVE const char* lsw_debug_add_watch(int handle, const char* expression) {
    auto* dbg = getDebugContext(handle);
    if (dbg == nullptr || expression == nullptr) {
        return "{\"error\":\"Invalid state\"}";
    }
    auto watch = dbg->controller->addWatchExpression(std::string(expression));
    std::ostringstream out;
    out << "{\"id\":";
    appendJsonString(out, watch.id);
    out << ",\"expression\":";
    appendJsonString(out, watch.expression);
    out << '}';
    return scratch(handle, out.str());
}

EMSCRIPTEN_KEEPALIVE int lsw_debug_remove_watch(int handle, const char* watchId) {
    auto* dbg = getDebugContext(handle);
    if (dbg == nullptr || watchId == nullptr) {
        return 0;
    }
    return dbg->controller->removeWatchExpression(std::string(watchId)) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE const char* lsw_debug_get_watches_json(int handle) {
    auto* dbg = getDebugContext(handle);
    if (dbg == nullptr) {
        return "[]";
    }
    auto watches = dbg->controller->evaluateWatchExpressions();
    std::ostringstream out;
    out << '[';
    bool first = true;
    for (const auto& wr : watches) {
        if (!first) out << ',';
        first = false;
        out << "{\"id\":";
        appendJsonString(out, wr.id);
        out << ",\"expression\":";
        appendJsonString(out, wr.expression);
        out << ",\"value\":";
        if (wr.lastValue.has_value()) {
            out << datumToJson(*wr.lastValue);
        } else {
            out << "null";
        }
        out << ",\"error\":";
        if (wr.lastError.has_value()) {
            appendJsonString(out, *wr.lastError);
        } else {
            out << "null";
        }
        out << '}';
    }
    out << ']';
    return scratch(handle, out.str());
}

}  // extern "C"
