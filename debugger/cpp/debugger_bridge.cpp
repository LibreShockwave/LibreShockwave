#include "debugger_bridge.hpp"

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
#ifdef __EMSCRIPTEN__
#define LSW_EXPORT(name) __attribute__((export_name(name)))
#else
#define LSW_EXPORT(name)
#endif

#include "libreshockwave/moviesource/movie_source.hpp"
#include "libreshockwave/moviesource/director_movie_source.hpp"
#include "libreshockwave/chunks/script_chunk.hpp"
#include "libreshockwave/format/script_format_utils.hpp"
#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/lingo/decompiler/lingo_decompiler.hpp"
#include "libreshockwave/lingo/vm/lingo_vm.hpp"
#include "libreshockwave/lingo/vm/datum/datum_formatter.hpp"
#include "libreshockwave/lingo/vm/trace/instruction_annotator.hpp"
#include "libreshockwave/player/player.hpp"
#include "libreshockwave/player/cast/cast_lib.hpp"
#include "libreshockwave/player/cast/cast_lib_manager.hpp"
#include "libreshockwave/player/debug/breakpoint_manager.hpp"
#include "libreshockwave/player/debug/debug_controller.hpp"
#include "libreshockwave/player/debug/debug_snapshot.hpp"

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

std::string scriptDisplayName(const libreshockwave::chunks::ScriptChunk& script) {
    const auto type_name = libreshockwave::format::getScriptTypeName(script.resolvedScriptType());
    return type_name + " #" + std::to_string(script.id().value());
}

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

std::unordered_map<int, std::unique_ptr<DebugContext>> debug_contexts;

DebugContext* getDebugContext(int handle) {
    auto* player = lswInternalGetPlayer(handle);
    if (player == nullptr) {
        return nullptr;
    }
    auto it = debug_contexts.find(handle);
    if (it != debug_contexts.end()) {
        return it->second.get();
    }
    // Lazy-init: create a DebugController and attach it to the player.
    auto ctx = std::make_unique<DebugContext>();
    ctx->controller = std::make_shared<DebugController>();
    ctx->controller->setNonBlockingMode(true);  // WASM: cooperative yield, no threads
    player->setDebugController(ctx->controller);
    player->setDebugEnabled(true);
    auto* result = ctx.get();
    debug_contexts.emplace(handle, std::move(ctx));
    return result;
}

const char* scratch(int handle, std::string value) {
    return lswInternalJsonScratch(handle, std::move(value));
}

// ---------------------------------------------------------------------------
// JSON builders
// ---------------------------------------------------------------------------

std::string buildMoviesJson(libreshockwave::player::Player& player) {
    std::ostringstream out;
    out << '[';
    bool first = true;
    for (const auto& [number, cast_lib] : player.castLibManager().castLibs()) {
        if (cast_lib == nullptr) continue;
        if (!first) out << ',';
        first = false;
        out << "{\"number\":" << number
            << ",\"name\":";
        appendJsonString(out, cast_lib->name());
        out << ",\"fileName\":";
        appendJsonString(out, cast_lib->fileName());
        out << ",\"scriptCount\":" << cast_lib->allScripts().size()
            << ",\"memberCount\":" << cast_lib->memberCount()
            << ",\"isExternal\":" << (cast_lib->isExternal() ? "true" : "false")
            << ",\"isLoaded\":" << (cast_lib->isLoaded() ? "true" : "false")
            << '}';
    }
    out << ']';
    return out.str();
}

std::string buildScriptsJson(libreshockwave::player::Player& player, int cast_lib_number) {
    auto cast_lib = player.castLibManager().getCastLib(cast_lib_number);
    std::ostringstream out;
    out << '[';
    if (cast_lib != nullptr) {
        bool first = true;
        for (const auto& script : cast_lib->allScripts()) {
            if (script == nullptr) continue;
            if (!first) out << ',';
            first = false;
            out << "{\"scriptId\":" << script->id().value()
                << ",\"name\":";
            appendJsonString(out, scriptDisplayName(*script));
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
                                  int script_id,
                                  const std::string& handler_name) {
    // Find the script chunk by scanning all cast libs.
    std::shared_ptr<libreshockwave::chunks::ScriptChunk> found_script;
    const libreshockwave::chunks::ScriptChunk::Handler* found_handler = nullptr;
    const libreshockwave::chunks::ScriptNamesChunk* found_names = nullptr;

    for (const auto& [number, cast_lib] : player.castLibManager().castLibs()) {
        if (cast_lib == nullptr) continue;
        for (const auto& script : cast_lib->allScripts()) {
            if (script == nullptr) continue;
            if (script->id().value() != script_id) continue;
            found_script = script;
            found_names = cast_lib->scriptNames().get();
            found_handler = script->findHandlerPtr(handler_name, found_names);
            if (found_handler != nullptr) break;
        }
        if (found_handler != nullptr) break;
    }

    // Also try the player's main MovieSource scripts
    if (found_handler == nullptr) {
        auto file = player.file();
        if (file != nullptr) {
            for (const auto& script : file->scriptChunks()) {
                if (script == nullptr) continue;
                if (script->id().value() != script_id) continue;
                found_script = script;
                found_names = file->scriptNamesForScript(*script).get();
                found_handler = script->findHandlerPtr(handler_name, found_names);
                if (found_handler != nullptr) break;
            }
        }
    }

    std::ostringstream out;
    out << '{';

    // When no handler specified, return script overview with handler list
    if (found_script != nullptr && handler_name.empty()) {
        out << "\"scriptName\":";
        appendJsonString(out, scriptDisplayName(*found_script));
        out << ",\"handlerName\":\"\"";
        out << ",\"argCount\":0,\"localCount\":0,\"globalsCount\":0,\"bytecodeLength\":0";
        // Handler names — always use the script's own file for name resolution
        out << ",\"handlers\":[";
        bool first_h = true;
        for (const auto& handler : found_script->handlers()) {
            if (!first_h) out << ',';
            first_h = false;
            appendJsonString(out, found_script->resolveName(handler.name_id, found_names));
        }
        out << ']';
        // Globals
        out << ",\"globals\":[";
        bool first_glob = true;
        for (const auto& g : found_script->globals()) {
            if (!first_glob) out << ',';
            first_glob = false;
            appendJsonString(out, found_script->resolveName(g.name_id, found_names));
        }
        out << ']';
        // Literals
        out << ",\"literals\":[";
        bool first_lit = true;
        for (const auto& lit : found_script->literals()) {
            if (!first_lit) out << ',';
            first_lit = false;
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
    } else if (found_script != nullptr && found_handler != nullptr) {
        // Convert the ScriptChunk to a ScriptAsset for the annotator.
        auto asset_source = found_script->file();
        auto asset_script = asset_source != nullptr ? asset_source->scriptByChunkId(found_script->id().value()) : nullptr;
        const libreshockwave::moviesource::ScriptAsset::Handler* asset_handler = nullptr;
        if (asset_script != nullptr) {
            for (const auto& h : asset_script->handlers) {
                if (h.name_id == found_handler->name_id) {
                    asset_handler = &h;
                    break;
                }
            }
        }
        out << "\"scriptName\":";
        appendJsonString(out, scriptDisplayName(*found_script));
        out << ",\"handlerName\":";
        appendJsonString(out, handler_name);
        out << ",\"argCount\":" << found_handler->arg_count
            << ",\"localCount\":" << found_handler->local_count
            << ",\"globalsCount\":" << found_handler->globals_count
            << ",\"bytecodeLength\":" << found_handler->bytecode_length;

        // Build bytecode array
        out << ",\"bytecode\":[";
        bool first_instr = true;
        for (const auto& instr : found_handler->instructions) {
            if (!first_instr) out << ',';
            first_instr = false;
            out << "{\"offset\":" << instr.offset
                << ",\"opcode\":";
            appendJsonString(out, std::string(libreshockwave::lingo::mnemonic(instr.opcode)));
            out << ",\"argument\":" << instr.argument
                << ",\"annotation\":";
            if (asset_script != nullptr && asset_handler != nullptr) {
                // Find the matching instruction in the asset handler.
                const libreshockwave::moviesource::ScriptAsset::Instruction* asset_instr = nullptr;
                for (const auto& ai : asset_handler->instructions) {
                    if (ai.offset == instr.offset) {
                        asset_instr = &ai;
                        break;
                    }
                }
                if (asset_instr != nullptr) {
                    appendJsonString(out, InstructionAnnotator::annotate(
                        *asset_script, asset_handler, *asset_instr, true));
                } else {
                    out << "\"\"";
                }
            } else {
                out << "\"\"";
            }
            out << '}';
        }
        out << ']';

        // Build decompiled lines array
        LingoDecompiler decompiler;
        auto decompiled = decompiler.decompileHandlerWithMapping(*found_handler, *found_script, found_names);
        out << ",\"decompiled\":[";
        bool first_line = true;
        for (const auto& line : decompiled.lines) {
            if (!first_line) out << ',';
            first_line = false;
            out << "{\"text\":";
            appendJsonString(out, line.text);
            out << ",\"bytecodeOffset\":" << line.bytecode_offset
                << '}';
        }
        out << ']';

        // Literals
        out << ",\"literals\":[";
        bool first_lit = true;
        for (const auto& lit : found_script->literals()) {
            if (!first_lit) out << ',';
            first_lit = false;
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
        bool first_glob = true;
        for (const auto& g : found_script->globals()) {
            if (!first_glob) out << ',';
            first_glob = false;
            appendJsonString(out, found_script->resolveName(g.name_id, found_names));
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
        << "\"scriptId\":" << snap.script_id
        << ",\"scriptName\":";
    appendJsonString(out, snap.script_name);
    out << ",\"handlerName\":";
    appendJsonString(out, snap.handler_name);
    out << ",\"instructionOffset\":" << snap.instruction_offset
        << ",\"instructionIndex\":" << snap.instruction_index
        << ",\"opcode\":";
    appendJsonString(out, snap.opcode);
    out << ",\"argument\":" << snap.argument
        << ",\"annotation\":";
    appendJsonString(out, snap.annotation);

    // All instructions with breakpoint flags
    out << ",\"allInstructions\":[";
    bool first_instr = true;
    for (const auto& instr : snap.all_instructions) {
        if (!first_instr) out << ',';
        first_instr = false;
        out << "{\"offset\":" << instr.offset
            << ",\"index\":" << instr.index
            << ",\"opcode\":";
        appendJsonString(out, instr.opcode);
        out << ",\"argument\":" << instr.argument
            << ",\"annotation\":";
        appendJsonString(out, instr.annotation);
        out << ",\"hasBreakpoint\":" << (instr.has_breakpoint ? "true" : "false")
            << '}';
    }
    out << ']';

    // Stack
    out << ",\"stack\":[";
    bool first_stack = true;
    for (const auto& val : snap.stack) {
        if (!first_stack) out << ',';
        first_stack = false;
        out << datumToJson(val);
    }
    out << ']';

    // Locals
    out << ",\"locals\":{";
    bool first_local = true;
    for (const auto& [name, val] : snap.locals) {
        if (!first_local) out << ',';
        first_local = false;
        appendJsonString(out, name);
        out << ':' << datumToJson(val);
    }
    out << '}';

    // Globals
    out << ",\"globals\":{";
    bool first_glob = true;
    for (const auto& [name, val] : snap.globals) {
        if (!first_glob) out << ',';
        first_glob = false;
        appendJsonString(out, name);
        out << ':' << datumToJson(val);
    }
    out << '}';

    // Arguments
    out << ",\"arguments\":[";
    bool first_arg = true;
    for (const auto& val : snap.arguments) {
        if (!first_arg) out << ',';
        first_arg = false;
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
        bool first_prop = true;
        for (const auto& [name, val] : props) {
            if (!first_prop) out << ',';
            first_prop = false;
            appendJsonString(out, name);
            out << ':' << datumToJson(val);
        }
    }
    out << '}';

    // Call stack
    out << ",\"callStack\":[";
    bool first_cs = true;
    for (const auto& frame : snap.call_stack) {
        if (!first_cs) out << ',';
        first_cs = false;
        out << "{\"scriptId\":" << frame.script_id
            << ",\"scriptName\":";
        appendJsonString(out, frame.script_name);
        out << ",\"handlerName\":";
        appendJsonString(out, frame.handler_name);
        out << '}';
    }
    out << ']';

    // Watch results
    out << ",\"watchResults\":[";
    bool first_wr = true;
    for (const auto& wr : snap.watch_results) {
        if (!first_wr) out << ',';
        first_wr = false;
        out << "{\"id\":";
        appendJsonString(out, wr.id);
        out << ",\"expression\":";
        appendJsonString(out, wr.expression);
        out << ",\"value\":";
        if (wr.last_value.has_value()) {
            out << datumToJson(*wr.last_value);
        } else {
            out << "null";
        }
        out << ",\"error\":";
        if (wr.last_error.has_value()) {
            appendJsonString(out, *wr.last_error);
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
        out << "{\"scriptId\":" << bp.script_id
            << ",\"handlerName\":";
        appendJsonString(out, bp.handler_name);
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

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_debug_list_movies_json") const char* lswDebugListMoviesJson(int handle) {
    auto* player = lswInternalGetPlayer(handle);
    if (player == nullptr) {
        return "[]";
    }
    return scratch(handle, buildMoviesJson(*player));
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_debug_list_scripts_json") const char* lswDebugListScriptsJson(int handle, int cast_lib_number) {
    auto* player = lswInternalGetPlayer(handle);
    if (player == nullptr) {
        return "[]";
    }
    return scratch(handle, buildScriptsJson(*player, cast_lib_number));
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_debug_get_handler_code_json") const char* lswDebugGetHandlerCodeJson(int handle,
                                                                  int script_id,
                                                                  const char* handler_name) {
    auto* player = lswInternalGetPlayer(handle);
    if (player == nullptr || handler_name == nullptr) {
        return "{\"error\":\"Invalid player or handler name\"}";
    }
    return scratch(handle, buildHandlerCodeJson(*player, script_id, std::string(handler_name)));
}

// -- Execution control ------------------------------------------------------

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_debug_pause") void lswDebugPause(int handle) {
    auto* dbg = getDebugContext(handle);
    if (dbg != nullptr) {
        dbg->controller->pause();
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_debug_continue") void lswDebugContinue(int handle) {
    auto* dbg = getDebugContext(handle);
    if (dbg != nullptr) {
        dbg->controller->continueExecution();
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_debug_step_into") void lswDebugStepInto(int handle) {
    auto* dbg = getDebugContext(handle);
    if (dbg != nullptr) {
        dbg->controller->stepInto();
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_debug_step_over") void lswDebugStepOver(int handle) {
    auto* dbg = getDebugContext(handle);
    if (dbg != nullptr) {
        dbg->controller->stepOver();
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_debug_step_out") void lswDebugStepOut(int handle) {
    auto* dbg = getDebugContext(handle);
    if (dbg != nullptr) {
        dbg->controller->stepOut();
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_debug_is_paused") int lswDebugIsPaused(int handle) {
    auto* dbg = getDebugContext(handle);
    if (dbg == nullptr) {
        return 0;
    }
    return dbg->controller->isPaused() ? 1 : 0;
}

// -- Breakpoints ------------------------------------------------------------

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_debug_toggle_breakpoint") const char* lswDebugToggleBreakpoint(int handle,
                                                               int script_id,
                                                               const char* handler_name,
                                                               int offset) {
    auto* dbg = getDebugContext(handle);
    if (dbg == nullptr || handler_name == nullptr) {
        return "{\"active\":false}";
    }
    bool active = dbg->controller->toggleBreakpoint(script_id, std::string(handler_name), offset);
    return scratch(handle, std::string("{\"active\":") + (active ? "true" : "false") + "}");
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_debug_clear_breakpoints") void lswDebugClearBreakpoints(int handle) {
    auto* dbg = getDebugContext(handle);
    if (dbg != nullptr) {
        dbg->controller->clearAllBreakpoints();
    }
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_debug_list_breakpoints_json") const char* lswDebugListBreakpointsJson(int handle) {
    auto* dbg = getDebugContext(handle);
    if (dbg == nullptr) {
        return "[]";
    }
    return scratch(handle, buildBreakpointsJson(*dbg->controller));
}

// -- State ------------------------------------------------------------------

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_debug_get_snapshot_json") const char* lswDebugGetSnapshotJson(int handle) {
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

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_debug_add_watch") const char* lswDebugAddWatch(int handle, const char* expression) {
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

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_debug_remove_watch") int lswDebugRemoveWatch(int handle, const char* watch_id) {
    auto* dbg = getDebugContext(handle);
    if (dbg == nullptr || watch_id == nullptr) {
        return 0;
    }
    return dbg->controller->removeWatchExpression(std::string(watch_id)) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE LSW_EXPORT("lsw_debug_get_watches_json") const char* lswDebugGetWatchesJson(int handle) {
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
        if (wr.last_value.has_value()) {
            out << datumToJson(*wr.last_value);
        } else {
            out << "null";
        }
        out << ",\"error\":";
        if (wr.last_error.has_value()) {
            appendJsonString(out, *wr.last_error);
        } else {
            out << "null";
        }
        out << '}';
    }
    out << ']';
    return scratch(handle, out.str());
}

}  // extern "C"
