#include "lsw_script_loader.hpp"

#include <algorithm>
#include <unordered_set>

#include "libreshockwave/lingo/compiler/codegen.hpp"
#include "libreshockwave/lingo/compiler/compile_error.hpp"
#include "libreshockwave/lingo/compiler/lexer.hpp"
#include "libreshockwave/lingo/compiler/parser.hpp"

namespace libreshockwave::moviesource {
namespace {

// Build a name table from the token order. Identifiers and symbols are the
// names the code generator can look up; keywords are harmless extras. This is
// only used for hand-authored scripts without a manifest name table.
std::vector<std::string> buildNameTable(const std::vector<lingo::compiler::Token>& tokens) {
    std::vector<std::string> names;
    std::unordered_set<std::string> seen;
    for (const auto& token : tokens) {
        if (token.type != lingo::compiler::TokenType::Identifier &&
            token.type != lingo::compiler::TokenType::Symbol &&
            token.type != lingo::compiler::TokenType::Keyword) {
            continue;
        }
        if (token.text.empty() || seen.count(token.text) != 0) {
            continue;
        }
        seen.insert(token.text);
        names.push_back(token.text);
    }
    return names;
}

} // namespace

std::shared_ptr<ScriptAsset> compileLswScript(const std::string& script_name,
                                              const std::string& text,
                                              const std::vector<std::string>& names,
                                              ScriptType type,
                                              int behavior_flags,
                                              int version,
                                              bool capital_x,
                                              std::string& error) {
    using lingo::compiler::CodeGenerator;
    using lingo::compiler::CompileError;
    using lingo::compiler::Lexer;
    using lingo::compiler::Parser;

    try {
        Lexer lexer(text);
        auto tokens = lexer.lex();
        std::vector<std::string> name_table = names;
        if (name_table.empty()) {
            name_table = buildNameTable(tokens);
        }
        Parser parser(std::move(tokens));
        auto ast = parser.parseScript();
        CodeGenerator generator(std::move(ast), name_table, version, capital_x);
        auto asset = std::make_shared<ScriptAsset>(generator.compile());
        asset->name = script_name;
        asset->type = type;
        asset->behavior_flags = behavior_flags;
        return asset;
    } catch (const CompileError& compile_error) {
        error = "script '" + script_name + "': " + compile_error.what();
        return nullptr;
    }
}

} // namespace libreshockwave::moviesource
