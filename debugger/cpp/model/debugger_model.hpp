#pragma once

#include <QMetaType>
#include <string>
#include <vector>

namespace libreshockwave::debugger {

/// Movie metadata for the tree panel.
struct MovieInfo {
    int number{0};
    std::string name;
    std::string file_name;
    int script_count{0};
    int member_count{0};
    bool is_external{false};
    bool is_loaded{false};
};

/// Script metadata for the tree panel.
struct ScriptInfo {
    int script_id{0};
    std::string name;
    std::string type;
    int handler_count{0};
};

/// Single bytecode instruction for the code view.
struct InstructionData {
    int offset{0};
    int index{0};
    std::string opcode;
    int argument{0};
    std::string annotation;
    bool has_breakpoint{false};
};

/// Single decompiled source line.
struct DecompiledLineData {
    std::string text;
    int bytecode_offset{0};
};

/// The kind of declaration a right-click "Go to declaration" resolves to.
enum class DeclarationKind {
    Method,    // a handler in a script
    Property,  // a behavior property declared by a script
    Global     // a global variable listed by a script
};

/// One resolvable declaration site in the movie, produced by SymbolIndex.
/// `handlerName` is only set for Method targets; `castLibNumber` disambiguates
/// scripts that share an ID across cast libraries.
struct DeclarationTarget {
    int cast_lib_number{0};
    int script_id{0};
    std::string script_name;
    std::string handler_name;
    DeclarationKind kind{DeclarationKind::Method};
};

/// Handler code data (bytecode + decompiled).
struct HandlerCodeData {
    std::string script_name;
    std::string handler_name;
    int arg_count{0};
    int local_count{0};
    int globals_count{0};
    int bytecode_length{0};
    std::vector<std::string> handler_names;
    std::vector<std::string> global_names;
    std::vector<InstructionData> instructions;
    std::vector<DecompiledLineData> decompiled_lines;
};

/// Value-type snapshot data safe for Qt signal/slot crossing threads.
struct SnapshotData {
    int script_id{0};
    std::string script_name;
    std::string handler_name;
    int instruction_offset{-1};
    int instruction_index{0};
    std::string opcode;
    int argument{0};
    std::string annotation;
    std::vector<InstructionData> all_instructions;
    // Call stack
    struct CallFrameEntry {
        int script_id{0};
        std::string script_name;
        std::string handler_name;
    };
    std::vector<CallFrameEntry> call_stack;
    // Variables encoded as display strings (avoids Datum in signals)
    struct VariableEntry {
        std::string name;
        std::string value;
        std::string type;
        bool expandable{false};
    };
    std::vector<VariableEntry> locals;
    std::vector<VariableEntry> globals;
    std::vector<VariableEntry> properties;
    std::vector<VariableEntry> stack;
    // Watch results
    struct WatchEntry {
        std::string id;
        std::string expression;
        std::string value;
        std::string error;
    };
    std::vector<WatchEntry> watch_results;
};

/// Breakpoint info for UI display.
struct BreakpointData {
    int script_id{0};
    std::string handler_name;
    int offset{0};
    bool enabled{true};
};

/// Movie tree snapshot for runtime cast-load updates.  Built on the VM
/// (worker) thread when an external cast (CCT/CST) finishes loading and
/// handed to the UI through a queued signal, so the tree never reads live
/// cast state concurrently with the tick loop.
struct MovieTreeSnapshot {
    struct HandlerEntry {
        std::string name;

        friend bool operator==(const HandlerEntry&, const HandlerEntry&) = default;
    };
    struct ScriptEntry {
        int script_id{0};
        int cast_lib_number{0};
        std::string display_name;
        std::string type_name;
        std::vector<HandlerEntry> handlers;
        // Declared behavior properties and globals of the script, resolved
        // on the VM thread so "Go to declaration" can index them without
        // reading live cast state from the UI thread.
        std::vector<std::string> property_names;
        std::vector<std::string> global_names;

        friend bool operator==(const ScriptEntry&, const ScriptEntry&) = default;
    };
    struct MovieEntry {
        int cast_lib_number{0};
        std::string name;
        std::string file_name;
        std::vector<ScriptEntry> scripts;

        friend bool operator==(const MovieEntry&, const MovieEntry&) = default;
    };
    std::vector<MovieEntry> movies;

    friend bool operator==(const MovieTreeSnapshot&, const MovieTreeSnapshot&) = default;
};

} // namespace libreshockwave::debugger

// Required so SnapshotData can cross threads through queued Qt signal
// connections (DebugStateBridge emits from the worker/VM thread). Without
// this, Qt silently drops the paused() signal and the pause/step/breakpoint
// UI never updates.
Q_DECLARE_METATYPE(libreshockwave::debugger::SnapshotData)

// Same for the movie tree snapshot: DebuggerContext emits castLoaded() from
// the worker/VM thread when a runtime CCT/CST load completes.
Q_DECLARE_METATYPE(libreshockwave::debugger::MovieTreeSnapshot)

// DeclarationTarget crosses from CodeViewPanel to the window as a queued-safe
// value when "Go to declaration" targets another script.
Q_DECLARE_METATYPE(libreshockwave::debugger::DeclarationTarget)
