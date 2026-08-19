#pragma once

#include <QMetaType>
#include <string>
#include <vector>

namespace libreshockwave::debugger {

/// Movie metadata for the tree panel.
struct MovieInfo {
    int number{0};
    std::string name;
    std::string fileName;
    int scriptCount{0};
    int memberCount{0};
    bool isExternal{false};
    bool isLoaded{false};
};

/// Script metadata for the tree panel.
struct ScriptInfo {
    int scriptId{0};
    std::string name;
    std::string type;
    int handlerCount{0};
};

/// Single bytecode instruction for the code view.
struct InstructionData {
    int offset{0};
    int index{0};
    std::string opcode;
    int argument{0};
    std::string annotation;
    bool hasBreakpoint{false};
};

/// Single decompiled source line.
struct DecompiledLineData {
    std::string text;
    int bytecodeOffset{0};
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
    int castLibNumber{0};
    int scriptId{0};
    std::string scriptName;
    std::string handlerName;
    DeclarationKind kind{DeclarationKind::Method};
};

/// Handler code data (bytecode + decompiled).
struct HandlerCodeData {
    std::string scriptName;
    std::string handlerName;
    int argCount{0};
    int localCount{0};
    int globalsCount{0};
    int bytecodeLength{0};
    std::vector<std::string> handlerNames;
    std::vector<std::string> globalNames;
    std::vector<InstructionData> instructions;
    std::vector<DecompiledLineData> decompiledLines;
};

/// Value-type snapshot data safe for Qt signal/slot crossing threads.
struct SnapshotData {
    int scriptId{0};
    std::string scriptName;
    std::string handlerName;
    int instructionOffset{-1};
    int instructionIndex{0};
    std::string opcode;
    int argument{0};
    std::string annotation;
    std::vector<InstructionData> allInstructions;
    // Call stack
    struct CallFrameEntry {
        int scriptId{0};
        std::string scriptName;
        std::string handlerName;
    };
    std::vector<CallFrameEntry> callStack;
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
    std::vector<WatchEntry> watchResults;
};

/// Breakpoint info for UI display.
struct BreakpointData {
    int scriptId{0};
    std::string handlerName;
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
        int scriptId{0};
        int castLibNumber{0};
        std::string displayName;
        std::string typeName;
        std::vector<HandlerEntry> handlers;
        // Declared behavior properties and globals of the script, resolved
        // on the VM thread so "Go to declaration" can index them without
        // reading live cast state from the UI thread.
        std::vector<std::string> propertyNames;
        std::vector<std::string> globalNames;

        friend bool operator==(const ScriptEntry&, const ScriptEntry&) = default;
    };
    struct MovieEntry {
        int castLibNumber{0};
        std::string name;
        std::string fileName;
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
