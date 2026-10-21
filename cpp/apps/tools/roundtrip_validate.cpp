// Standalone round-trip validation driver (not a ctest target).
//
// Usage:
//   roundtrip_validate <movie-file> [--script <name>] [--dump <prefix>]
//
// Loads a Director movie, decompiles each script with the LingoDecompiler,
// recompiles the text with the lingo/compiler pipeline, and compares the
// resulting ScriptAsset against the original, field by field. It reports every
// difference (handler, instruction, literal, name table) and a summary.
//
// This is the validation path for the byte-identical round-trip guarantee:
// load -> decompile -> save -> load must reproduce the original bytecode.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "libreshockwave/chunks/script_chunk.hpp"
#include "libreshockwave/lingo/compiler/codegen.hpp"
#include "libreshockwave/lingo/compiler/compile_error.hpp"
#include "libreshockwave/lingo/compiler/lexer.hpp"
#include "libreshockwave/lingo/compiler/parser.hpp"
#include "libreshockwave/lingo/decompiler/lingo_decompiler.hpp"
#include "libreshockwave/moviesource/movie_source.hpp"

namespace {

using libreshockwave::chunks::ScriptChunk;
using libreshockwave::lingo::compiler::CodeGenerator;
using libreshockwave::lingo::compiler::CompileError;
using libreshockwave::lingo::compiler::Lexer;
using libreshockwave::lingo::compiler::Parser;
using libreshockwave::lingo::decompiler::LingoDecompiler;
using libreshockwave::moviesource::ScriptAsset;

struct Diff {
    std::string section;
    std::string detail;
};

std::string literalValueText(const ScriptAsset::LiteralEntry& entry) {
    std::ostringstream out;
    out << "[" << entry.type << "] ";
    if (std::holds_alternative<std::monostate>(entry.value)) {
        out << "void";
    } else if (const auto* s = std::get_if<std::string>(&entry.value)) {
        out << "\"" << *s << "\"";
    } else if (const auto* i = std::get_if<int>(&entry.value)) {
        out << *i;
    } else if (const auto* b = std::get_if<std::vector<std::uint8_t>>(&entry.value)) {
        out << "bytes(" << b->size() << ")";
    }
    return out.str();
}

// Compare one handler's execution data. Returns the differences found.
std::vector<Diff> compareHandler(const ScriptAsset::Handler& a,
                                const ScriptAsset::Handler& b,
                                const std::string& label) {
    std::vector<Diff> diffs;
    auto add = [&](const std::string& section, const std::string& detail) {
        diffs.push_back(Diff{label + " " + section, detail});
    };
    if (a.name_id != b.name_id) {
        add("name_id", std::to_string(a.name_id) + " != " + std::to_string(b.name_id));
    }
    if (a.arg_count != b.arg_count) {
        add("arg_count", std::to_string(a.arg_count) + " != " + std::to_string(b.arg_count));
    }
    if (a.local_count != b.local_count) {
        add("local_count", std::to_string(a.local_count) + " != " + std::to_string(b.local_count));
    }
    if (a.globals_count != b.globals_count) {
        add("globals_count", std::to_string(a.globals_count) + " != " + std::to_string(b.globals_count));
    }
    if (a.line_count != b.line_count) {
        add("line_count", std::to_string(a.line_count) + " != " + std::to_string(b.line_count));
    }
    if (a.arg_name_ids != b.arg_name_ids) {
        add("arg_name_ids", "mismatch");
    }
    if (a.local_name_ids != b.local_name_ids) {
        add("local_name_ids", "mismatch");
    }
    if (a.instructions.size() != b.instructions.size()) {
        add("instruction_count",
            std::to_string(a.instructions.size()) + " != " + std::to_string(b.instructions.size()));
        return diffs;
    }
    for (std::size_t i = 0; i < a.instructions.size(); ++i) {
        const auto& ia = a.instructions[i];
        const auto& ib = b.instructions[i];
        if (ia.offset != ib.offset || ia.raw_opcode != ib.raw_opcode ||
            ia.argument != ib.argument) {
            std::ostringstream detail;
            detail << "instr " << i << " @" << ia.offset << " raw=" << ia.raw_opcode
                   << " arg=" << ia.argument << " -> raw=" << ib.raw_opcode
                   << " arg=" << ib.argument;
            add("instruction", detail.str());
        }
    }
    return diffs;
}

// Compare two ScriptAssets field by field.
std::vector<Diff> compareAssets(const ScriptAsset& a, const ScriptAsset& b) {
    std::vector<Diff> diffs;
    if (a.names != b.names) {
        std::ostringstream detail;
        detail << "name table size " << a.names.size() << " != " << b.names.size();
        diffs.push_back(Diff{"names", detail.str()});
    }
    if (a.property_name_ids != b.property_name_ids) {
        diffs.push_back(Diff{"property_name_ids", "mismatch"});
    }
    if (a.global_name_ids != b.global_name_ids) {
        diffs.push_back(Diff{"global_name_ids", "mismatch"});
    }
    if (a.literals.size() != b.literals.size()) {
        std::ostringstream detail;
        detail << "literal count " << a.literals.size() << " != " << b.literals.size();
        diffs.push_back(Diff{"literals", detail.str()});
        return diffs;
    }
    for (std::size_t i = 0; i < a.literals.size(); ++i) {
        const auto& la = a.literals[i];
        const auto& lb = b.literals[i];
        bool same = la.type == lb.type;
        if (same && std::holds_alternative<std::monostate>(la.value)) {
            same = std::holds_alternative<std::monostate>(lb.value);
        } else if (same) {
            same = std::visit(
                [](const auto& x, const auto& y) {
                    using X = std::decay_t<decltype(x)>;
                    using Y = std::decay_t<decltype(y)>;
                    if constexpr (std::is_same_v<X, Y>) {
                        return x == y;
                    } else {
                        return false;
                    }
                },
                la.value, lb.value);
        }
        if (!same) {
            diffs.push_back(Diff{"literal " + std::to_string(i),
                                literalValueText(la) + " -> " + literalValueText(lb)});
        }
    }
    if (a.handlers.size() != b.handlers.size()) {
        std::ostringstream detail;
        detail << "handler count " << a.handlers.size() << " != " << b.handlers.size();
        diffs.push_back(Diff{"handlers", detail.str()});
        return diffs;
    }
    for (std::size_t i = 0; i < a.handlers.size(); ++i) {
        auto handler_diffs = compareHandler(a.handlers[i], b.handlers[i],
                                          "handler " + a.getHandlerName(a.handlers[i]));
        diffs.insert(diffs.end(), handler_diffs.begin(), handler_diffs.end());
    }
    return diffs;
}

// Recompile decompiled text back to a ScriptAsset.
std::shared_ptr<ScriptAsset> recompile(const std::string& text,
                                      const std::vector<std::string>& name_table,
                                      int version,
                                      bool capital_x) {
    Lexer lexer(text);
    const auto tokens = lexer.lex();
    Parser parser(tokens);
    auto ast = parser.parseScript();
    CodeGenerator generator(std::move(ast), name_table, version, capital_x);
    return std::make_shared<ScriptAsset>(generator.compile());
}

struct Options {
    std::string movie_path;
    std::string script_filter;
    std::string dump_prefix;
    bool dump_asm = false;
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--script" && i + 1 < argc) {
            options.script_filter = argv[++i];
        } else if (arg == "--dump" && i + 1 < argc) {
            options.dump_prefix = argv[++i];
        } else if (arg == "--dump-asm") {
            options.dump_asm = true;
        } else if (!arg.starts_with("--")) {
            options.movie_path = arg;
        }
    }
    return options;
}

std::vector<std::uint8_t> readFileBytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open " + path);
    }
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
    return data;
}

} // namespace

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    if (options.movie_path.empty()) {
        std::cerr << "usage: roundtrip_validate <movie-file> [--script <name>] [--dump <prefix>]"
                  << std::endl;
        return 2;
    }
    const auto data = readFileBytes(options.movie_path);
    const auto source = libreshockwave::moviesource::MovieSource::load(options.movie_path, data);
    const int version = source->version();
    const bool capital_x = source->isCapitalX();

    std::size_t total = 0;
    std::size_t ok = 0;
    std::size_t failed = 0;
    int exit_code = 0;
    LingoDecompiler decompiler;

    for (const auto& chunk : source->scriptChunks()) {
        const std::string script_name = source->scriptNameForScript(*chunk);
        if (!options.script_filter.empty() && script_name != options.script_filter) {
            continue;
        }
        const auto original = source->scriptByChunkId(chunk->id().value());
        if (!original) {
            continue;
        }
        total++;
        const auto names = source->scriptNamesForScript(*chunk);
        const std::string text = decompiler.decompile(*chunk, names.get());
        if (!options.dump_prefix.empty()) {
            std::ofstream out(options.dump_prefix + "_" + script_name + ".lsw.lingo");
            out << text;
        }
        std::shared_ptr<ScriptAsset> recompiled;
        try {
            recompiled = recompile(text, original->names, version, capital_x);
        } catch (const CompileError& error) {
            failed++;
            exit_code = 1;
            std::cerr << "FAIL " << script_name << ": " << error.what() << std::endl;
            continue;
        }
        if (options.dump_asm) {
            std::cout << "-- names: ";
            for (std::size_t i = 0; i < original->names.size(); ++i) {
                std::cout << i << "=" << original->names[i] << " ";
            }
            std::cout << std::endl;
            for (std::size_t h = 0; h < original->handlers.size(); ++h) {
                const auto& a = original->handlers[h];
                const bool has_b = h < recompiled->handlers.size();
                const auto& b = has_b ? recompiled->handlers[h] : a;
                std::cout << "== " << script_name << " handler " << a.name_id << " (" << a.instructions.size()
                          << " vs " << (has_b ? b.instructions.size() : 0) << ") ==" << std::endl;
                const std::size_t n = std::max(a.instructions.size(), has_b ? b.instructions.size() : 0);
                for (std::size_t i = 0; i < n; ++i) {
                    auto fmt = [](const ScriptAsset::Handler& h, std::size_t i) -> std::string {
                        if (i >= h.instructions.size()) {
                            return "-";
                        }
                        const auto& ins = h.instructions[i];
                        char buf[64];
                        std::snprintf(buf, sizeof(buf), "@%d raw=0x%02x arg=%d",
                                     ins.offset, ins.raw_opcode, ins.argument);
                        return buf;
                    };
                    const std::string sa = fmt(a, i);
                    const std::string sb = fmt(b, i);
                    const char mark = (i < a.instructions.size() && has_b && i < b.instructions.size() &&
                                      a.instructions[i].raw_opcode == b.instructions[i].raw_opcode &&
                                      a.instructions[i].argument == b.instructions[i].argument) ? ' ' : 'X';
                    std::cout << mark << " " << sa << "  |  " << sb << std::endl;
                }
            }
        }
        const auto diffs = compareAssets(*original, *recompiled);
        if (diffs.empty()) {
            ok++;
            std::cout << "OK   " << script_name << std::endl;
        } else {
            failed++;
            exit_code = 1;
            std::cerr << "FAIL " << script_name << ": " << diffs.size() << " difference(s)"
                      << std::endl;
            for (const auto& diff : diffs) {
                if (diffs.size() <= 40) {
                    std::cerr << "  " << diff.section << ": " << diff.detail << std::endl;
                }
            }
            if (diffs.size() > 40) {
                std::cerr << "  ... and " << (diffs.size() - 40) << " more" << std::endl;
            }
        }
    }
    std::cout << "round-trip summary: " << total << " script(s), " << ok << " identical, "
              << failed << " failed" << std::endl;
    return exit_code;
}
