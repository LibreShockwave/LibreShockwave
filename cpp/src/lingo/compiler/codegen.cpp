#include "libreshockwave/lingo/compiler/codegen.hpp"

#include <bit>
#include <cmath>

#include "libreshockwave/format/script_format_utils.hpp"

namespace libreshockwave::lingo::compiler {
namespace {

// Whether an argument fits in a 1-byte / 2-byte field. PUSH_INT8/INT16 are
// signed; everything else is unsigned.
bool fitsOne(Opcode op, int value) {
    if (op == Opcode::PUSH_INT8) {
        return value >= -128 && value <= 127;
    }
    return value >= 0 && value <= 255;
}

bool fitsTwo(Opcode op, int value) {
    if (op == Opcode::PUSH_INT16) {
        return value >= -32768 && value <= 32767;
    }
    return value >= 0 && value <= 65535;
}

// A fixed `the <prop>` (not a name-table entry) is encoded as PUSH_INT8 <id>
// then GET <type>. This inverts the decompiler's readV4Property.
bool fixedPropCode(const std::string& name, int& type, int& id) {
    // property_type 0x00: moviePropertyName.
    static const std::pair<const char*, int> kMovie[] = {
        {"floatPrecision", 0x00}, {"mouseDownScript", 0x01},
        {"mouseUpScript", 0x02}, {"keyDownScript", 0x03},
        {"keyUpScript", 0x04}, {"timeoutScript", 0x05},
        {"short time", 0x06}, {"abbr time", 0x07}, {"long time", 0x08},
        {"short date", 0x09}, {"abbr date", 0x0a}, {"long date", 0x0b},
    };
    for (const auto& e : kMovie) {
        if (name == e.first) { type = 0x00; id = e.second; return true; }
    }
    // property_type 0x07: animationPropertyName.
    static const std::pair<const char*, int> kAnim[] = {
        {"beepOn", 0x01}, {"buttonStyle", 0x02}, {"centerStage", 0x03},
        {"checkBoxAccess", 0x04}, {"checkboxType", 0x05}, {"colorDepth", 0x06},
        {"colorQD", 0x07}, {"exitLock", 0x08}, {"fixStageSize", 0x09},
        {"fullColorPermit", 0x0a}, {"imageDirect", 0x0b},
        {"lastClick", 0x0e}, {"lastEvent", 0x0f},
        {"lastRoll", 0x12},
        {"timeoutLapsed", 0x13}, {"multiSound", 0x14}, {"pauseState", 0x15},
        {"quickTimePresent", 0x16}, {"selEnd", 0x17}, {"selStart", 0x18},
        {"soundEnabled", 0x19}, {"soundLevel", 0x1a}, {"stageColor", 0x1b},
        {"switchColorDepth", 0x1d}, {"timeoutKeyDown", 0x1e},
        {"timeoutLength", 0x1f}, {"timeoutMouse", 0x20}, {"timeoutPlay", 0x21},
        {"timer", 0x22}, {"preLoadRAM", 0x23},
        {"videoForWindowsPresent", 0x24}, {"netPresent", 0x25},
        {"safePlayer", 0x26}, {"soundKeepDevice", 0x27},
        {"soundMixMedia", 0x28},
    };
    for (const auto& e : kAnim) {
        if (name == e.first) { type = 0x07; id = e.second; return true; }
    }
    // property_type 0x08: animation2PropertyName.
    static const std::pair<const char*, int> kAnim2[] = {
        {"perFrameHook", 0x01}, {"number of castMembers", 0x02},
        {"number of menus", 0x03}, {"number of castLibs", 0x04},
        {"number of xtras", 0x05},
    };
    for (const auto& e : kAnim2) {
        if (name == e.first) { type = 0x08; id = e.second; return true; }
    }
    return false;
}

// A `the <prop>` that is a live builtin (not a fixed prop, not a name-table
// movie property) is encoded as PUSH_ARG_LIST 0 then THE_BUILTIN <name_id>.
// This inverts the decompiler's THE_BUILTIN case (which pops the arg list).
bool builtinProp(const std::string& name) {
    static const char* const kBuiltin[] = {
        "clickOn", "commandDown", "date", "doubleClick", "frame", "key",
        "keyCode", "lastKey", "maxinteger", "mouseDown", "mouseH", "mouseV",
        "optionDown", "paramCount", "result", "rollover", "shiftDown",
        "stageBottom", "stageLeft", "stageRight", "stageTop", "time",
    };
    for (const auto* b : kBuiltin) {
        if (name == b) { return true; }
    }
    return false;
}

} // namespace

CodeGenerator::CodeGenerator(std::unique_ptr<Script> script,
                             const std::vector<std::string>& name_table,
                             int version,
                             bool capital_x)
    : script_(std::move(script)),
      names_(name_table),
      version_(version),
      mult_(capital_x ? 1 : (version >= 500 ? 8 : 6)) {}

int CodeGenerator::nameId(const std::string& name) {
    for (std::size_t i = 0; i < names_.size(); ++i) {
        if (names_[i] == name) {
            return static_cast<int>(i);
        }
    }
    throw CompileError("name '" + name + "' is not in the name table", 0, 0);
}

void CodeGenerator::emit(Opcode op, int argument) {
    const int base = lingo::argBytes(op);
    int width = base;
    if (base == 1 && !fitsOne(op, argument)) {
        width = fitsTwo(op, argument) ? 2 : 4;
    }
    // JMP_IF_Z and JMP always use a 2-byte argument (to allow negative
    // offsets). END_REPEAT uses the narrowest width that fits (1-byte when the
    // offset is a signed byte, 2-byte otherwise).
    if (op == Opcode::JMP_IF_Z || op == Opcode::JMP) {
        width = 2;
    }
    const int n = static_cast<int>(op);
    int raw;
    if (width == 0) {
        raw = n;
    } else if (width == 1) {
        raw = n;
    } else if (width == 2) {
        raw = n + 0x40;
    } else {
        raw = n + 0x80;
    }
    handler_->instructions.push_back(
        moviesource::ScriptAsset::Instruction{offset_, op, raw, argument});
    offset_ += 1 + width;
}

void CodeGenerator::emitNoArg(Opcode op) {
    emit(op, 0);
}

void CodeGenerator::emitInt(long long value) {
    if (value == 0) {
        emitNoArg(Opcode::PUSH_ZERO);
    } else if (value >= -128 && value <= 127) {
        emit(Opcode::PUSH_INT8, static_cast<int>(value));
    } else if (value >= -32768 && value <= 32767) {
        emit(Opcode::PUSH_INT16, static_cast<int>(value));
    } else {
        emit(Opcode::PUSH_INT32, static_cast<int>(value));
    }
}

int CodeGenerator::internString(const std::string& value) {
    const auto found = literal_index_.find(value);
    if (found != literal_index_.end()) {
        return found->second;
    }
    const int index = static_cast<int>(literals_.size());
    moviesource::ScriptAsset::LiteralEntry entry;
    entry.type = 1; // string
    entry.value = value;
    literals_.push_back(std::move(entry));
    literal_index_[value] = index;
    return index;
}

int CodeGenerator::internInt(long long value) {
    // Ints are pushed directly (PUSH_INT8/16/32), not via the literal table.
    (void)value;
    return -1;
}

int CodeGenerator::internFloat(double value) {
    // Floats that are not exactly representable as float32 live in the literal
    // table as type 9 entries.
    for (std::size_t i = 0; i < literals_.size(); ++i) {
        if (literals_[i].type == 9 && literals_[i].numeric_value == value) {
            return static_cast<int>(i);
        }
    }
    const int index = static_cast<int>(literals_.size());
    moviesource::ScriptAsset::LiteralEntry entry;
    entry.type = 9;
    entry.value = format::floatingToString(value);
    entry.numeric_value = value;
    literals_.push_back(std::move(entry));
    return index;
}

int CodeGenerator::internSymbol(const std::string& name) {
    return nameId(name);
}

// ---------------------------------------------------------------------------
// Script compilation.
// ---------------------------------------------------------------------------

moviesource::ScriptAsset CodeGenerator::compile() {
    moviesource::ScriptAsset asset;
    asset.names = names_;
    for (const auto& prop : script_->property_names) {
        asset.property_name_ids.push_back(nameId(prop));
    }
    for (const auto& global : script_->global_names) {
        asset.global_name_ids.push_back(nameId(global));
    }
    asset.handlers.reserve(script_->handlers.size());
    for (auto& handler_ast : script_->handlers) {
        compileHandler(*handler_ast, asset);
    }
    asset.literals = std::move(literals_);
    return asset;
}

void CodeGenerator::compileHandler(const Handler& handler_ast, moviesource::ScriptAsset& asset) {
    // Reset per-handler state. The literal table is per-script (a single table
    // the PUSH_CONS index references), so it is not cleared here.
    arg_slot_.clear();
    local_slot_.clear();
    local_names_.clear();
    declared_names_.clear();
    global_names_.clear();
    offset_ = 0;

    // Names declared as globals or properties are never locals.
    for (const auto& prop : script_->property_names) {
        declared_names_.insert(prop);
    }
    for (const auto& global : script_->global_names) {
        declared_names_.insert(global);
        global_names_.insert(global);
    }
    for (const auto& global : handler_ast.global_names) {
        declared_names_.insert(global);
        global_names_.insert(global);
    }
    // Arguments are parameters, never locals. `tMethod = tEvent` inside a
    // handler must emit SET_PARAM and must not grow the local table.
    for (const auto& arg : handler_ast.arg_names) {
        declared_names_.insert(arg);
    }
    // First pass: collect the locals (first-assignment order).
    collectLocals(handler_ast);

    moviesource::ScriptAsset::Handler handler;
    handler.name_id = nameId(handler_ast.name);
    handler.arg_count = static_cast<int>(handler_ast.arg_names.size());
    for (std::size_t i = 0; i < handler_ast.arg_names.size(); ++i) {
        handler.arg_name_ids.push_back(nameId(handler_ast.arg_names[i]));
        arg_slot_[handler_ast.arg_names[i]] = static_cast<int>(i);
    }
    handler.globals_count = static_cast<int>(handler_ast.global_names.size());
    for (const auto& global : handler_ast.global_names) {
        handler.global_name_ids.push_back(nameId(global));
    }
    for (std::size_t i = 0; i < local_names_.size(); ++i) {
        local_slot_[local_names_[i]] = static_cast<int>(i);
    }
    handler_ = &handler;
    emitBlock(handler_ast.statements);
    emitNoArg(Opcode::RET);
    handler_ = nullptr;
    handler.local_count = static_cast<int>(local_names_.size());
    for (const auto& local : local_names_) {
        handler.local_name_ids.push_back(nameId(local));
    }
    handler.bytecode_length = offset_;
    asset.handlers.push_back(std::move(handler));
}

void CodeGenerator::collectLocals(const Handler& handler_ast) {
    for (const auto& stmt : handler_ast.statements) {
        collectLocalsStmt(*stmt);
    }
}

// Mark a simple name as a local (first-assignment order, skipping declared names).
void CodeGenerator::markLocal(const std::string& name) {
    if (declared_names_.count(name)) {
        return;
    }
    for (const auto& existing : local_names_) {
        if (existing == name) {
            return;
        }
    }
    local_names_.push_back(name);
}

void CodeGenerator::collectLocalsStmt(const Stmt& stmt) {
    switch (stmt.kind) {
        case Stmt::Kind::If:
            if (stmt.condition) collectLocalsExpr(*stmt.condition);
            for (const auto& s : stmt.then_block) collectLocalsStmt(*s);
            for (const auto& s : stmt.else_block) collectLocalsStmt(*s);
            break;
        case Stmt::Kind::RepeatWhile:
            if (stmt.condition) collectLocalsExpr(*stmt.condition);
            for (const auto& s : stmt.block) collectLocalsStmt(*s);
            break;
        case Stmt::Kind::RepeatWithIn:
            if (stmt.value) collectLocalsExpr(*stmt.value);
            if (stmt.loop_var && stmt.loop_var->kind == Expr::Kind::Var) {
                markLocal(stmt.loop_var->name);
            }
            for (const auto& s : stmt.block) collectLocalsStmt(*s);
            break;
        case Stmt::Kind::RepeatWithTo:
            if (stmt.start) collectLocalsExpr(*stmt.start);
            if (stmt.end) collectLocalsExpr(*stmt.end);
            if (stmt.loop_var && stmt.loop_var->kind == Expr::Kind::Var) {
                markLocal(stmt.loop_var->name);
            }
            for (const auto& s : stmt.block) collectLocalsStmt(*s);
            break;
        case Stmt::Kind::Return:
            if (stmt.value) collectLocalsExpr(*stmt.value);
            for (const auto& arg : stmt.args) {
                if (arg) collectLocalsExpr(*arg);
            }
            break;
        case Stmt::Kind::Assignment:
            if (stmt.variable) collectLocalsExpr(*stmt.variable);
            if (stmt.variable && stmt.variable->kind == Expr::Kind::Var) {
                markLocal(stmt.variable->name);
            }
            if (stmt.value) collectLocalsExpr(*stmt.value);
            break;
        case Stmt::Kind::Put:
            if (stmt.value) collectLocalsExpr(*stmt.value);
            if (stmt.variable) collectLocalsExpr(*stmt.variable);
            if (stmt.variable && stmt.variable->kind == Expr::Kind::Var) {
                markLocal(stmt.variable->name);
            }
            break;
        case Stmt::Kind::Hilite:
        case Stmt::Kind::Delete:
            if (stmt.value) collectLocalsExpr(*stmt.value);
            break;
        case Stmt::Kind::Case:
            if (stmt.condition) collectLocalsExpr(*stmt.condition);
            for (const auto& branch : stmt.case_branches) {
                for (const auto& label : branch.labels) collectLocalsExpr(*label);
                for (const auto& s : branch.body) collectLocalsStmt(*s);
            }
            break;
        case Stmt::Kind::Tell:
            if (stmt.window) collectLocalsExpr(*stmt.window);
            for (const auto& s : stmt.block) collectLocalsStmt(*s);
            break;
        case Stmt::Kind::Call:
            for (const auto& arg : stmt.args) collectLocalsExpr(*arg);
            break;
        default:
            break;
    }
}

void CodeGenerator::collectLocalsExpr(const Expr& expr) {
    switch (expr.kind) {
        case Expr::Kind::Var:
            break;
        case Expr::Kind::ThePropExpr:
            if (expr.object) collectLocalsExpr(*expr.object);
            break;
        case Expr::Kind::ObjPropExpr:
        case Expr::Kind::MemberExpr:
            if (expr.index) collectLocalsExpr(*expr.index);
            if (expr.index2) collectLocalsExpr(*expr.index2);
            break;
        case Expr::Kind::ObjBracketExpr:
            if (expr.object) collectLocalsExpr(*expr.object);
            if (expr.index) collectLocalsExpr(*expr.index);
            break;
        case Expr::Kind::ObjPropIndexExpr:
            if (expr.object) collectLocalsExpr(*expr.object);
            if (expr.index) collectLocalsExpr(*expr.index);
            if (expr.index2) collectLocalsExpr(*expr.index2);
            break;
        case Expr::Kind::ChunkExpr:
            if (expr.source) collectLocalsExpr(*expr.source);
            if (expr.first) collectLocalsExpr(*expr.first);
            if (expr.last) collectLocalsExpr(*expr.last);
            break;
        case Expr::Kind::LastChunkExpr:
            if (expr.source) collectLocalsExpr(*expr.source);
            break;
        case Expr::Kind::ChunkCountExpr:
            if (expr.source) collectLocalsExpr(*expr.source);
            break;
        case Expr::Kind::SpriteIntersects:
        case Expr::Kind::SpriteWithin:
            if (expr.left) collectLocalsExpr(*expr.left);
            if (expr.right) collectLocalsExpr(*expr.right);
            break;
        case Expr::Kind::MenuProp:
        case Expr::Kind::SoundProp:
        case Expr::Kind::SpriteProp:
        case Expr::Kind::MenuItemProp:
            if (expr.index) collectLocalsExpr(*expr.index);
            if (expr.index2) collectLocalsExpr(*expr.index2);
            break;
        case Expr::Kind::NewObj:
        case Expr::Kind::Call:
        case Expr::Kind::ObjCall:
        case Expr::Kind::ObjCallV4:
            for (const auto& arg : expr.args) {
                if (arg) collectLocalsExpr(*arg);
            }
            if ((expr.kind == Expr::Kind::ObjCall || expr.kind == Expr::Kind::ObjCallV4) && expr.object) {
                collectLocalsExpr(*expr.object);
            }
            break;
        case Expr::Kind::List:
            for (const auto& item : expr.list->items) collectLocalsExpr(*item);
            break;
        case Expr::Kind::PropList:
            for (const auto& value : expr.prop_list->values) {
                if (value) collectLocalsExpr(*value);
            }
            break;
        case Expr::Kind::BinaryOp:
        case Expr::Kind::NotOp:
        case Expr::Kind::InverseOp:
            if (expr.left) collectLocalsExpr(*expr.left);
            if (expr.kind == Expr::Kind::BinaryOp && expr.right) {
                collectLocalsExpr(*expr.right);
            }
            break;
        default:
            break;
    }
}

void CodeGenerator::emitBlock(const std::vector<std::unique_ptr<Stmt>>& block) {
    for (const auto& stmt : block) {
        emitStmt(*stmt);
    }
}

void CodeGenerator::emitStmt(const Stmt& stmt) {
    switch (stmt.kind) {
        case Stmt::Kind::If: {
            emitExpr(*stmt.condition);
            const int cond_offset = offset_;
            emit(Opcode::JMP_IF_Z, 0); // patched to else (or end)
            emitBlock(stmt.then_block);
            int else_start = -1;
            int then_jump_offset = -1;
            if (!stmt.else_block.empty() || stmt.has_else) {
                then_jump_offset = offset_;
                emit(Opcode::JMP, 0); // patched to end
                else_start = offset_;
                emitBlock(stmt.else_block);
            }
            const int end_offset = offset_;
            patchJump(cond_offset,
                      (else_start != -1 ? else_start : end_offset) - cond_offset);
            if (then_jump_offset != -1) {
                patchJump(then_jump_offset, end_offset - then_jump_offset);
            }
            break;
        }
        case Stmt::Kind::RepeatWhile:
            emitRepeatWhile(stmt);
            break;
        case Stmt::Kind::RepeatWithIn:
            emitRepeatWithIn(stmt);
            break;
        case Stmt::Kind::RepeatWithTo:
            emitRepeatWithTo(stmt);
            break;
        case Stmt::Kind::Return: {
            // A return inside a case must remove the pending case values before
            // the return value, or the arg list captures them.
            if (case_depth_ > 0) {
                emit(Opcode::POP, case_depth_);
            }
            if (!stmt.args.empty()) {
                emitArgs(stmt.args, /*no_ret=*/true);
                emit(Opcode::EXT_CALL, nameId("return") * mult_);
            } else if (stmt.value) {
                emitExpr(*stmt.value);
                emit(Opcode::PUSH_ARG_LIST_NO_RET, 1);
                emit(Opcode::EXT_CALL, nameId("return") * mult_);
            } else {
                // A bare return is a no-argument call to the return handler.
                emit(Opcode::PUSH_ARG_LIST_NO_RET, 0);
                emit(Opcode::EXT_CALL, nameId("return") * mult_);
            }
            break;
        }
        case Stmt::Kind::Exit:
            // The Lingo exit statement is a bare RET. A bare return is a
            // no-argument call to the return handler.
            emitNoArg(Opcode::RET);
            break;
        case Stmt::Kind::Assignment: {
            // A simple var: value first, then the SET. A dot/bracket lvalue:
            // the object first, then the value, then the SET.
            if (stmt.variable->kind == Expr::Kind::Var) {
                emitExpr(*stmt.value);
                emitSetForVar(stmt.variable->name);
            } else if (stmt.variable->kind == Expr::Kind::ObjPropExpr) {
                emitReceiver(*stmt.variable->object);
                emitExpr(*stmt.value);
                emit(Opcode::SET_OBJ_PROP, nameId(stmt.variable->name) * mult_);
            } else if (stmt.variable->kind == Expr::Kind::ObjBracketExpr) {
                // object[index] = value: push object, index, value, then
                // PUSH_ARG_LIST_NO_RET(3) and OBJ_CALL(setAt).
                emitReceiver(*stmt.variable->object);
                emitExpr(*stmt.variable->index);
                emitExpr(*stmt.value);
                emit(Opcode::PUSH_ARG_LIST_NO_RET, 3);
                emit(Opcode::OBJ_CALL, nameId("setAt") * mult_);
            } else if (stmt.variable->kind == Expr::Kind::ObjPropIndexExpr) {
                // object.prop[index] = value: push object, #prop, index,
                // value, then OBJ_CALL(setProp).
                emitReceiver(*stmt.variable->object);
                emit(Opcode::PUSH_SYMB, nameId(stmt.variable->name) * mult_);
                emitExpr(*stmt.variable->index);
                const bool has_second = stmt.variable->index2 != nullptr;
                if (has_second) {
                    emitExpr(*stmt.variable->index2);
                }
                emitExpr(*stmt.value);
                emit(Opcode::PUSH_ARG_LIST_NO_RET, has_second ? 5 : 4);
                emit(Opcode::OBJ_CALL, nameId("setProp") * mult_);
            } else if (stmt.variable->kind == Expr::Kind::ChunkExpr) {
                // `char 1 of x = value` is a PUT_CHUNK with put_type into.
                emitExpr(*stmt.value);
                emitChunkBounds(*stmt.variable);
                const int var_type = emitVarRefForPut(*stmt.variable->source);
                emit(Opcode::PUT_CHUNK, (1 << 4) | var_type);
            } else {
                // Fallback: value first, then the lvalue (which emits the SET).
                emitExpr(*stmt.value);
                emitLvalue(*stmt.variable);
            }
            break;
        }
        case Stmt::Kind::Put: {
            emitExpr(*stmt.value);
            const int put_type = stmt.put_dir == PutDir::Into ? 1
                                 : stmt.put_dir == PutDir::After ? 2 : 3;
            if (stmt.variable->kind == Expr::Kind::ChunkExpr) {
                emitChunkBounds(*stmt.variable);
                const int var_type = emitVarRefForPut(*stmt.variable->source);
                emit(Opcode::PUT_CHUNK, (put_type << 4) | var_type);
            } else {
                const int var_type = emitVarRefForPut(*stmt.variable);
                emit(Opcode::PUT, (put_type << 4) | var_type);
            }
            break;
        }
        case Stmt::Kind::Hilite:
            emitExpr(*stmt.value);
            emitNoArg(Opcode::HILITE_CHUNK);
            break;
        case Stmt::Kind::Delete:
            // `delete char 1 to n of x`: the eight bounds, then the source
            // var-ref, then DELETE_CHUNK with the var_type nibble.
            if (stmt.value->kind == Expr::Kind::ChunkExpr && stmt.value->source) {
                emitChunkBounds(*stmt.value);
                emit(Opcode::DELETE_CHUNK, emitVarRefForPut(*stmt.value->source));
            } else {
                // `delete <object>` is the object's delete method call.
                emitReceiver(*stmt.value);
                emit(Opcode::PUSH_ARG_LIST_NO_RET, 1);
                emit(Opcode::OBJ_CALL, nameId("delete") * mult_);
            }
            break;
        case Stmt::Kind::Case:
            emitCase(stmt);
            break;
        case Stmt::Kind::ExitRepeat:
            // `exit repeat` jumps past the loop's END_REPEAT.
            if (!loop_stack_.empty()) {
                loop_stack_.back().break_jumps.push_back(offset_);
                emit(Opcode::JMP, 0);
            }
            break;
        case Stmt::Kind::NextRepeat:
            // `next repeat` jumps to the loop's increment or END_REPEAT.
            if (!loop_stack_.empty()) {
                loop_stack_.back().continue_jumps.push_back(offset_);
                emit(Opcode::JMP, 0);
            }
            break;
        case Stmt::Kind::Tell:
            emitExpr(*stmt.window);
            emitNoArg(Opcode::START_TELL);
            emitBlock(stmt.block);
            emitNoArg(Opcode::END_TELL);
            break;
        case Stmt::Kind::When:
            // `when` is a handler-level directive; it is not inlined into the
            // body bytecode. Skip (the decompiler emits it as a comment).
            break;
        case Stmt::Kind::Call: {
            // A statement call: push the args (NO_RET), then EXT_CALL (or
            // LOCAL_CALL). An object method call pushes the receiver first.
            if (stmt.window) {
                emitReceiver(*stmt.window);
                for (const auto& arg : stmt.args) {
                    emitExpr(*arg);
                }
                emit(Opcode::PUSH_ARG_LIST_NO_RET, static_cast<int>(stmt.args.size()) + 1);
                emit(Opcode::OBJ_CALL, nameId(stmt.name) * mult_);
                break;
            }
            emitCall(stmt.name, stmt.args, /*is_local=*/false, /*no_ret=*/true);
            break;
        }
    }
}

void CodeGenerator::emitExpr(const Expr& expr) {
    switch (expr.kind) {
        case Expr::Kind::Int: {
            emitInt(expr.int_value);
            break;
        }
        case Expr::Kind::Float: {
            const float f = static_cast<float>(expr.float_value);
            if (static_cast<double>(f) == expr.float_value) {
                emit(Opcode::PUSH_FLOAT32, static_cast<int>(std::bit_cast<std::uint32_t>(f)));
            } else {
                emit(Opcode::PUSH_CONS, internFloat(expr.float_value) * mult_);
            }
            break;
        }
        case Expr::Kind::String: {
            const int index = internString(expr.string.value);
            emit(Opcode::PUSH_CONS, index * mult_);
            break;
        }
        case Expr::Kind::Symbol: {
            emit(Opcode::PUSH_SYMB, nameId(expr.name) * mult_);
            break;
        }
        case Expr::Kind::Symb: {
            emit(Opcode::PUSH_SYMB, expr.int_value * mult_);
            break;
        }
        case Expr::Kind::Var: {
            emitVarGet(expr.name);
            break;
        }
        case Expr::Kind::TheExpr: {
            int type = -1, id = -1;
            if (fixedPropCode(expr.name, type, id)) {
                // A fixed prop: PUSH_INT8 <id> then GET <type>.
                emit(Opcode::PUSH_INT8, id);
                emit(Opcode::GET, type);
            } else if (builtinProp(expr.name)) {
                // A live builtin: PUSH_ARG_LIST 0 then THE_BUILTIN <name_id>.
                emit(Opcode::PUSH_ARG_LIST, 0);
                emit(Opcode::THE_BUILTIN, nameId(expr.name) * mult_);
            } else {
                // A name-table movie property: GET_MOVIE_PROP.
                emit(Opcode::GET_MOVIE_PROP, nameId(expr.name) * mult_);
            }
            break;
        }
        case Expr::Kind::ThePropExpr: {
            // The decompiler prints the count(base, #prop) object call as
            // `the count of the <prop> of <base>` in non-dot mode.
            if (expr.name == "count" && expr.object &&
                expr.object->kind == Expr::Kind::ThePropExpr) {
                emitReceiver(*expr.object->object);
                emit(Opcode::PUSH_SYMB, nameId(expr.object->name) * mult_);
                emit(Opcode::PUSH_ARG_LIST, 2);
                emit(Opcode::OBJ_CALL, nameId("count") * mult_);
                break;
            }
            // `the <prop> of <object>`: push the object, then access the prop.
            // A fixed prop is PUSH_INT8 <id>; GET <type>. A name-table prop
            // is GET_OBJ_PROP.
            emitReceiver(*expr.object);
            int type = -1, id = -1;
            if (fixedPropCode(expr.name, type, id)) {
                emit(Opcode::PUSH_INT8, id);
                emit(Opcode::GET, type);
            } else {
                emit(Opcode::GET_OBJ_PROP, nameId(expr.name) * mult_);
            }
            break;
        }
        case Expr::Kind::ObjPropExpr: {
            // `.count` on a nested property is encoded as count(base, prop_sym):
            // push the base, push the property name as a symbol, wrap in an
            // arg list, then OBJ_CALL count. This inverts the decompiler's
            // count pattern (nargs == 2, 2nd arg a symbol).
            if (expr.name == "count" && expr.object &&
                expr.object->kind == Expr::Kind::ObjPropExpr) {
                emitReceiver(*expr.object->object);
                emit(Opcode::PUSH_SYMB, nameId(expr.object->name) * mult_);
                emit(Opcode::PUSH_ARG_LIST, 2);
                emit(Opcode::OBJ_CALL, nameId("count") * mult_);
                break;
            }
            emitReceiver(*expr.object);
            emit(Opcode::GET_OBJ_PROP, nameId(expr.name) * mult_);
            break;
        }
        case Expr::Kind::ObjBracketExpr: {
            // `object[index]` is the getAt object call in Director bytecode. The
            // decompiler's translateObjCall prints OBJ_CALL getAt as a bracket.
            emitReceiver(*expr.object);
            emitExpr(*expr.index);
            emit(Opcode::PUSH_ARG_LIST, 2);
            emit(Opcode::OBJ_CALL, nameId("getAt") * mult_);
            break;
        }
        case Expr::Kind::ObjPropIndexExpr: {
            emitObjPropIndex(expr, /*by_ref=*/false);
            break;
        }
        case Expr::Kind::MemberExpr: {
            // `field(x)` is a field reference (GET_FIELD). `castLib(x)` is a
            // bare cast library reference used by the animation2 properties.
            // `member(x)` and `script(x)` in expression position are ordinary
            // calls: the decompiler prints the same text for the call and for
            // the legacy member entity, and the call form is the standalone one.
            if (expr.member_type == "field") {
                emitExpr(*expr.index);
                if (expr.index2) {
                    emitExpr(*expr.index2);
                } else if (version_ >= 500) {
                    emitNoArg(Opcode::PUSH_ZERO);
                }
                emitNoArg(Opcode::GET_FIELD);
                break;
            }
            if (expr.member_type == "castLib") {
                emitExpr(*expr.index);
                break;
            }
            emitExpr(*expr.index);
            if (expr.index2) {
                emitExpr(*expr.index2);
                emit(Opcode::PUSH_ARG_LIST, 2);
            } else {
                emit(Opcode::PUSH_ARG_LIST, 1);
            }
            emit(Opcode::EXT_CALL, nameId(expr.member_type) * mult_);
            break;
        }
        case Expr::Kind::ChunkExpr: {
            // The eight bounds, then the source, then GET_CHUNK. The
            // decompiler pops the source first and the bounds in reverse.
            emitChunkBounds(expr);
            emitExpr(*expr.source);
            emitNoArg(Opcode::GET_CHUNK);
            break;
        }
        case Expr::Kind::LastChunkExpr: {
            // `the last <chunk> in <string>` is a legacy GET with the chunk
            // property id (chunk type plus 0x0b) and property type 0x00.
            emitExpr(*expr.source);
            emit(Opcode::PUSH_INT8, expr.chunk_type + 0x0b);
            emit(Opcode::GET, 0x00);
            break;
        }
        case Expr::Kind::ChunkCountExpr: {
            // `the number of <chunks> in <string>` is a legacy GET with the
            // chunk type as the property id and property type 0x01.
            emitExpr(*expr.source);
            emit(Opcode::PUSH_INT8, expr.chunk_type);
            emit(Opcode::GET, 0x01);
            break;
        }
        case Expr::Kind::SpriteIntersects: {
            emitExpr(*expr.left);
            emitExpr(*expr.right);
            emitNoArg(Opcode::ONTO_SPR);
            break;
        }
        case Expr::Kind::SpriteWithin: {
            emitExpr(*expr.left);
            emitExpr(*expr.right);
            emitNoArg(Opcode::INTO_SPR);
            break;
        }
        case Expr::Kind::MenuProp:
        case Expr::Kind::SoundProp:
        case Expr::Kind::SpriteProp:
        case Expr::Kind::MenuItemProp: {
            // `the <prop> of menu/sound/sprite <id>`: push the id, then the
            // property opcode.
            if (expr.index) {
                emitExpr(*expr.index);
            }
            if (expr.index2) {
                emitExpr(*expr.index2);
            }
            emit(Opcode::GET_OBJ_PROP, nameId(expr.name) * mult_);
            break;
        }
        case Expr::Kind::NewObj: {
            emitArgs(expr.args, /*no_ret=*/false);
            emit(Opcode::NEW_OBJ, nameId(expr.name) * mult_);
            break;
        }
        case Expr::Kind::Call: {
            emitCall(expr.name, expr.args, /*is_local=*/false, /*no_ret=*/false);
            break;
        }
        case Expr::Kind::ObjCall: {
            // `object.method(args)`: push the object, push the args, then
            // PUSH_ARG_LIST (object + args), then OBJ_CALL.
            emitReceiver(*expr.object);
            for (const auto& arg : expr.args) {
                emitExpr(*arg);
            }
            emit(Opcode::PUSH_ARG_LIST, static_cast<int>(expr.args.size()) + 1);
            emit(Opcode::OBJ_CALL, nameId(expr.name) * mult_);
            break;
        }
        case Expr::Kind::ObjCallV4: {
            emitReceiver(*expr.object);
            emitArgs(expr.args, /*no_ret=*/false);
            emit(Opcode::OBJ_CALL_V4, 0);
            break;
        }
        case Expr::Kind::List: {
            // Build the list on the stack: push the items, combine them with
            // PUSH_ARG_LIST, then set the value type with PUSH_LIST.
            const auto& items = expr.list->items;
            for (const auto& item : items) {
                emitExpr(*item);
            }
            emit(Opcode::PUSH_ARG_LIST, static_cast<int>(items.size()));
            emitNoArg(Opcode::PUSH_LIST);
            break;
        }
        case Expr::Kind::PropList: {
            const auto& pl = expr.prop_list;
            for (std::size_t i = 0; i < pl->keys.size(); ++i) {
                if (pl->key_types[i] == PropListLit::KeyType::Symbol) {
                    emit(Opcode::PUSH_SYMB, nameId(pl->keys[i]) * mult_);
                } else if (pl->key_types[i] == PropListLit::KeyType::String) {
                    emit(Opcode::PUSH_CONS, internString(pl->keys[i]) * mult_);
                } else {
                    emitInt(std::stoll(pl->keys[i]));
                }
                if (pl->values[i]) {
                    emitExpr(*pl->values[i]);
                } else {
                    emitNoArg(Opcode::PUSH_ZERO);
                }
            }
            emit(Opcode::PUSH_ARG_LIST, static_cast<int>(pl->keys.size()) * 2);
            emitNoArg(Opcode::PUSH_PROP_LIST);
            break;
        }
        case Expr::Kind::BinaryOp: {
            emitExpr(*expr.left);
            emitExpr(*expr.right);
            emitNoArg(binOpToOpcode(expr.bin_op));
            break;
        }
        case Expr::Kind::NotOp: {
            emitExpr(*expr.left);
            emitNoArg(Opcode::NOT);
            break;
        }
        case Expr::Kind::InverseOp: {
            emitExpr(*expr.left);
            emitNoArg(Opcode::INV);
            break;
        }
    }
}

void CodeGenerator::emitObjPropIndex(const Expr& expr, bool by_ref) {
    // `base.prop[idx]` (and `base.prop[idx1, idx2]`) is encoded as the
    // getProp(base, prop_sym, idx[, idx2]) call. A receiver-position read uses
    // getPropRef instead; the decompiler prints both forms identically. The
    // base is itself in receiver position, so a nested access is by-ref too.
    emitReceiver(*expr.object);
    emit(Opcode::PUSH_SYMB, nameId(expr.name) * mult_);
    emitExpr(*expr.index);
    if (expr.index2) {
        emitExpr(*expr.index2);
        emit(Opcode::PUSH_ARG_LIST, 4);
    } else {
        emit(Opcode::PUSH_ARG_LIST, 3);
    }
    emit(Opcode::OBJ_CALL, nameId(by_ref ? "getPropRef" : "getProp") * mult_);
}

void CodeGenerator::emitReceiver(const Expr& expr) {
    if (expr.kind == Expr::Kind::ObjPropExpr) {
        // `a.b.c`: the inner access is a chained read and the outermost access
        // is a plain property read. Director uses GET_CHAINED_PROP for the
        // inner access because its result is immediately read again.
        emitReceiver(*expr.object);
        emit(Opcode::GET_CHAINED_PROP, nameId(expr.name) * mult_);
        return;
    }
    if (expr.kind == Expr::Kind::ThePropExpr) {
        emitReceiver(*expr.object);
        int type = -1;
        int id = -1;
        if (fixedPropCode(expr.name, type, id)) {
            emit(Opcode::PUSH_INT8, id);
            emit(Opcode::GET, type);
        } else {
            emit(Opcode::GET_CHAINED_PROP, nameId(expr.name) * mult_);
        }
        return;
    }
    if (expr.kind == Expr::Kind::ObjPropIndexExpr) {
        emitObjPropIndex(expr, /*by_ref=*/true);
        return;
    }
    emitExpr(expr);
}

void CodeGenerator::emitVarGet(const std::string& name) {
    // Choose the GET opcode by the variable's declaration kind.
    if (arg_slot_.count(name)) {
        emit(Opcode::GET_PARAM, arg_slot_.at(name) * mult_);
        return;
    }
    if (local_slot_.count(name)) {
        emit(Opcode::GET_LOCAL, local_slot_.at(name) * mult_);
        return;
    }
    // A global, a script property, or a movie-level (top-level) property.
    // Director's built-in top-level references (_player, _movie, _sound) are
    // always accessed via GET_TOP_LEVEL_PROP, even when declared global.
    if (isTopLevelBuiltin(name)) {
        emit(Opcode::GET_TOP_LEVEL_PROP, nameId(name) * mult_);
    } else if (global_names_.count(name)) {
        emit(Opcode::GET_GLOBAL, nameId(name) * mult_);
    } else if (isScriptProperty(name)) {
        emit(Opcode::GET_PROP, nameId(name) * mult_);
    } else {
        emit(Opcode::GET_TOP_LEVEL_PROP, nameId(name) * mult_);
    }
}

// Director's built-in top-level references. They are accessed via
// GET_TOP_LEVEL_PROP regardless of their global declaration.
bool CodeGenerator::isTopLevelBuiltin(const std::string& name) {
    return name == "_player" || name == "_movie" || name == "_sound";
}

bool CodeGenerator::isScriptProperty(const std::string& name) {
    for (const auto& prop : script_->property_names) {
        if (prop == name) {
            return true;
        }
    }
    return false;
}

void CodeGenerator::emitLvalue(const Expr& expr) {
    // An lvalue leaves a var-ref on the stack; the following SET opcode is
    // chosen by the var kind. For a simple var, emit the GET (which the SET
    // pairs with). For a bracket/dot index, emit the object + index.
    switch (expr.kind) {
        case Expr::Kind::Var: {
            // A simple var: the SET opcode is emitted by the caller via the
            // var kind. Here we emit a marker by re-emitting the GET; the SET
            // is appended by emitStmt. To keep the pattern exact, emit the GET
            // then the matching SET.
            emitSetForVar(expr.name);
            break;
        }
        case Expr::Kind::ObjBracketExpr: {
            emitExpr(*expr.object);
            emitExpr(*expr.index);
            emitNoArg(Opcode::SET);
            break;
        }
        case Expr::Kind::TheExpr: {
            // `set the <prop> to <value>`: the value is already on the stack.
            // A fixed prop is PUSH_INT8 <id>; SET <type>. A name-table prop is
            // SET_MOVIE_PROP <name_id * mult>.
            int type = -1, id = -1;
            if (fixedPropCode(expr.name, type, id)) {
                emit(Opcode::PUSH_INT8, id);
                emit(Opcode::SET, type);
            } else {
                emit(Opcode::SET_MOVIE_PROP, nameId(expr.name) * mult_);
            }
            break;
        }
        case Expr::Kind::ObjPropExpr: {
            emitReceiver(*expr.object);
            emit(Opcode::SET_OBJ_PROP, nameId(expr.name) * mult_);
            break;
        }
        case Expr::Kind::ObjPropIndexExpr: {
            emitExpr(*expr.object);
            emit(Opcode::GET_OBJ_PROP, nameId(expr.name) * mult_);
            emitExpr(*expr.index);
            emitNoArg(Opcode::SET);
            break;
        }
        case Expr::Kind::ChunkExpr: {
            // A chunk target: the eight bounds, then the source variable ref.
            emitChunkBounds(expr);
            (void)emitVarRefForPut(*expr.source);
            break;
        }
        default: {
            // Fallback: treat as a var get + set.
            emitSetForVar(expr.name);
            break;
        }
    }
}

void CodeGenerator::emitChunkBounds(const Expr& chunk) {
    // first_char, last_char, first_word, last_word, first_item, last_item,
    // first_line, last_line. Missing bounds become PUSH_ZERO. Director folds a
    // negated integer bound into the constant itself.
    const auto emit_bound = [this](const Expr& expr) {
        if (expr.kind == Expr::Kind::InverseOp && expr.left &&
            expr.left->kind == Expr::Kind::Int) {
            emitInt(-expr.left->int_value);
            return;
        }
        emitExpr(expr);
    };
    for (const int chunk_id : {1, 2, 3, 4, 5, 6, 7, 8}) {
        const int type = (chunk_id + 1) / 2; // 1->1, 2->1, 3->2, ...
        if (chunk.chunk_type == type && chunk.first) {
            if (chunk_id % 2 == 1) {
                emit_bound(*chunk.first);
            } else if (chunk.last) {
                emit_bound(*chunk.last);
            } else {
                emitNoArg(Opcode::PUSH_ZERO);
            }
        } else {
            emitNoArg(Opcode::PUSH_ZERO);
        }
    }
}

int CodeGenerator::emitVarRefForPut(const Expr& variable) {
    if (variable.kind == Expr::Kind::Var) {
        const std::string& name = variable.name;
        if (arg_slot_.count(name)) {
            emitInt(arg_slot_.at(name));
            return 4;
        }
        if (local_slot_.count(name)) {
            emitInt(local_slot_.at(name));
            return 5;
        }
        emit(Opcode::PUSH_VAR_REF, nameId(name) * mult_);
        return isScriptProperty(name) ? 3 : 1;
    }
    // Any other target keeps its name in a var-ref so the decompiler can
    // recover it.
    emit(Opcode::PUSH_VAR_REF, nameId(variable.name) * mult_);
    return 3;
}

void CodeGenerator::emitSetForVar(const std::string& name) {
    if (arg_slot_.count(name)) {
        emit(Opcode::SET_PARAM, arg_slot_.at(name) * mult_);
        return;
    }
    if (local_slot_.count(name)) {
        emit(Opcode::SET_LOCAL, local_slot_.at(name) * mult_);
        return;
    }
    if (global_names_.count(name)) {
        emit(Opcode::SET_GLOBAL, nameId(name) * mult_);
    } else if (isScriptProperty(name)) {
        emit(Opcode::SET_PROP, nameId(name) * mult_);
    } else {
        emit(Opcode::SET_MOVIE_PROP, nameId(name) * mult_);
    }
}

void CodeGenerator::emitArgs(const std::vector<std::unique_ptr<Expr>>& args,
                             bool no_ret) {
    for (const auto& arg : args) {
        emitExpr(*arg);
    }
    // A statement call wraps its args in PUSH_ARG_LIST_NO_RET; an expression
    // call (and NEW_OBJ) wraps them in PUSH_ARG_LIST.
    emit(no_ret ? Opcode::PUSH_ARG_LIST_NO_RET : Opcode::PUSH_ARG_LIST,
         static_cast<int>(args.size()));
}

void CodeGenerator::emitCall(const std::string& name,
                            const std::vector<std::unique_ptr<Expr>>& args,
                            bool is_local,
                            bool no_ret) {
    emitArgs(args, no_ret);
    // A call to a handler in the same script is a LOCAL_CALL (arg = handler index).
    for (std::size_t i = 0; i < script_->handlers.size(); ++i) {
        if (script_->handlers[i]->name == name) {
            emit(Opcode::LOCAL_CALL, static_cast<int>(i));
            return;
        }
    }
    if (is_local) {
        emit(Opcode::LOCAL_CALL, 0);
    } else {
        emit(Opcode::EXT_CALL, nameId(name) * mult_);
    }
}

void CodeGenerator::emitRepeatWhile(const Stmt& stmt) {
    loop_stack_.emplace_back();
    const int loop_start = offset_;
    emitExpr(*stmt.condition);
    const int cond_offset = offset_;
    emit(Opcode::JMP_IF_Z, 0); // patched to loop_end
    emitBlock(stmt.block);
    const int continue_target = offset_;
    for (const int jump_offset : loop_stack_.back().continue_jumps) {
        patchJump(jump_offset, continue_target - jump_offset);
    }
    emit(Opcode::END_REPEAT, offset_ - loop_start);
    const int loop_end = offset_;
    patchJump(cond_offset, loop_end - cond_offset);
    for (const int jump_offset : loop_stack_.back().break_jumps) {
        patchJump(jump_offset, loop_end - jump_offset);
    }
    loop_stack_.pop_back();
}

void CodeGenerator::emitRepeatWithIn(const Stmt& stmt) {
    // The exact pattern the decompiler's isRepeatWithInLoop recognizes:
    //   PEEK 0; PUSH_ARG_LIST 1; EXT_CALL count; PUSH_INT8 1; PEEK 0;
    //   PEEK 2; LT_EQ; JMP_IF_Z -> exit; PEEK 2; PEEK 1; PUSH_ARG_LIST 2;
    //   EXT_CALL getAt; SET var; <body>; PUSH_INT8 1; ADD; END_REPEAT -> top;
    //   POP 3
    emitExpr(*stmt.value); // the list
    emitNoArg(Opcode::PEEK); // 0 (top of stack)
    emit(Opcode::PUSH_ARG_LIST, 1);
    emit(Opcode::EXT_CALL, nameId("count") * mult_);
    emit(Opcode::PUSH_INT8, 1);
    const int top = offset_;
    emitNoArg(Opcode::PEEK); // 0 (the index)
    emit(Opcode::PEEK, 2); // the count
    emitNoArg(Opcode::LT_EQ);
    const int cond_offset = offset_;
    emit(Opcode::JMP_IF_Z, 0); // patched to exit
    emit(Opcode::PEEK, 2); // the list
    emit(Opcode::PEEK, 1); // the index
    emit(Opcode::PUSH_ARG_LIST, 2);
    emit(Opcode::EXT_CALL, nameId("getAt") * mult_);
    emitSetForVar(stmt.loop_var->name);
    loop_stack_.emplace_back();
    emitBlock(stmt.block);
    const int continue_target = offset_;
    for (const int jump_offset : loop_stack_.back().continue_jumps) {
        patchJump(jump_offset, continue_target - jump_offset);
    }
    emit(Opcode::PUSH_INT8, 1);
    emitNoArg(Opcode::ADD);
    emit(Opcode::END_REPEAT, offset_ - top); // back to top (positive distance)
    const int exit = offset_;
    emit(Opcode::POP, 3);
    patchJump(cond_offset, exit - cond_offset);
    for (const int jump_offset : loop_stack_.back().break_jumps) {
        patchJump(jump_offset, exit - jump_offset);
    }
    loop_stack_.pop_back();
}

void CodeGenerator::emitRepeatWithTo(const Stmt& stmt) {
    // The pattern the decompiler's isRepeatWithToLoop recognizes:
    //   <push start>; SET var; loop_top: GET var; <push end>; LT_EQ (or GT_EQ
    //   for down); JMP_IF_Z -> exit; <body>; PUSH_INT8 +-1; GET var; ADD;
    //   SET var; END_REPEAT -> loop_top
    emitExpr(*stmt.start);
    emitSetForVar(stmt.loop_var->name);
    const int loop_top = offset_;
    emitVarGet(stmt.loop_var->name);
    emitExpr(*stmt.end);
    emitNoArg(stmt.up ? Opcode::LT_EQ : Opcode::GT_EQ);
    const int cond_offset = offset_;
    emit(Opcode::JMP_IF_Z, 0); // patched to exit
    loop_stack_.emplace_back();
    emitBlock(stmt.block);
    const int continue_target = offset_;
    for (const int jump_offset : loop_stack_.back().continue_jumps) {
        patchJump(jump_offset, continue_target - jump_offset);
    }
    emit(Opcode::PUSH_INT8, stmt.up ? 1 : -1);
    emitVarGet(stmt.loop_var->name);
    emitNoArg(Opcode::ADD);
    emitSetForVar(stmt.loop_var->name);
    emit(Opcode::END_REPEAT, offset_ - loop_top); // back to loop_top (positive distance)
    const int exit = offset_;
    patchJump(cond_offset, exit - cond_offset);
    for (const int jump_offset : loop_stack_.back().break_jumps) {
        patchJump(jump_offset, exit - jump_offset);
    }
    loop_stack_.pop_back();
}

void CodeGenerator::emitCase(const Stmt& stmt) {
    // Director's case lowering: the case value stays on the stack. Each branch
    // starts with PEEK plus a label compare. The last label of a branch uses EQ
    // and jumps over the body to the next branch; earlier labels of an
    // or-group use NT_EQ and jump into the body. The last branch falls through
    // to POP, which removes the case value.
    emitExpr(*stmt.condition);
    ++case_depth_;

    struct PendingJump {
        int offset;
        int branch;
    };
    std::vector<int> branch_start;
    std::vector<PendingJump> or_jumps;   // NT_EQ checks: target is the body
    std::vector<PendingJump> next_jumps; // EQ checks: target is the next branch
    std::vector<int> end_jumps;          // branch-end JMPs: target is the case end
    int otherwise_skip = -1;

    for (std::size_t branch_index = 0; branch_index < stmt.case_branches.size(); ++branch_index) {
        const auto& branch = stmt.case_branches[branch_index];
        branch_start.push_back(offset_);
        if (!branch.is_otherwise) {
            for (std::size_t label_index = 0; label_index < branch.labels.size(); ++label_index) {
                emitNoArg(Opcode::PEEK);
                emitExpr(*branch.labels[label_index]);
                const bool is_last_label = label_index + 1 == branch.labels.size();
                emitNoArg(is_last_label ? Opcode::EQ : Opcode::NT_EQ);
                const int jump_offset = offset_;
                if (is_last_label) {
                    next_jumps.push_back(PendingJump{jump_offset, static_cast<int>(branch_index)});
                } else {
                    or_jumps.push_back(PendingJump{jump_offset, static_cast<int>(branch_index)});
                }
                emit(Opcode::JMP_IF_Z, 0);
            }
        }
        const int body_start = offset_;
        for (const auto& jump : or_jumps) {
            if (jump.branch == static_cast<int>(branch_index)) {
                patchJump(jump.offset, body_start - jump.offset);
            }
        }
        // A case with only an otherwise branch jumps over the otherwise body.
        // Director emits this even though the body is then unreachable.
        if (branch.is_otherwise && branch_index == 0) {
            otherwise_skip = offset_;
            emit(Opcode::JMP, 0);
        }
        emitBlock(branch.body);
        if (branch_index + 1 < stmt.case_branches.size()) {
            end_jumps.push_back(offset_);
            emit(Opcode::JMP, 0);
        }
    }

    const int case_end = offset_;
    for (const auto& jump : next_jumps) {
        const std::size_t next_branch = static_cast<std::size_t>(jump.branch) + 1;
        const int target = next_branch < branch_start.size() ? branch_start[next_branch] : case_end;
        patchJump(jump.offset, target - jump.offset);
    }
    for (const int jump_offset : end_jumps) {
        patchJump(jump_offset, case_end - jump_offset);
    }
    if (otherwise_skip >= 0) {
        patchJump(otherwise_skip, case_end - otherwise_skip);
    }
    emit(Opcode::POP, 1);
    --case_depth_;
}

// ---------------------------------------------------------------------------
// Helpers.
// ---------------------------------------------------------------------------

void CodeGenerator::patchJump(int from_offset, int target) {
    // Find the instruction at `from_offset` and set its argument to `target`.
    for (auto& instr : handler_->instructions) {
        if (instr.offset == from_offset) {
            instr.argument = target;
            return;
        }
    }
}

Opcode CodeGenerator::binOpToOpcode(BinOp op) {
    switch (op) {
        case BinOp::Mul: return Opcode::MUL;
        case BinOp::Add: return Opcode::ADD;
        case BinOp::Sub: return Opcode::SUB;
        case BinOp::Div: return Opcode::DIV;
        case BinOp::Mod: return Opcode::MOD;
        case BinOp::JoinStr: return Opcode::JOIN_STR;
        case BinOp::JoinPadStr: return Opcode::JOIN_PAD_STR;
        case BinOp::Lt: return Opcode::LT;
        case BinOp::LtEq: return Opcode::LT_EQ;
        case BinOp::NotEq: return Opcode::NT_EQ;
        case BinOp::Eq: return Opcode::EQ;
        case BinOp::Gt: return Opcode::GT;
        case BinOp::GtEq: return Opcode::GT_EQ;
        case BinOp::And: return Opcode::AND;
        case BinOp::Or: return Opcode::OR;
        case BinOp::ContainsStr: return Opcode::CONTAINS_STR;
        case BinOp::Contains0Str: return Opcode::CONTAINS_0_STR;
    }
    return Opcode::INVALID;
}

} // namespace libreshockwave::lingo::compiler
