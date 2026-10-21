#pragma once

#include <stdexcept>
#include <string>

namespace libreshockwave::lingo::compiler {

// Thrown when a Lingo script fails to lex or parse. Carries a diagnostic
// that names the failure point (line and column) and a short reason. The load
// policy is strict: one unparseable script fails the whole load, so this error
// propagates up to the loader, which reports it and aborts.
class CompileError : public std::runtime_error {
public:
    CompileError(std::string message, int line, int column)
        : std::runtime_error(formatMessage(message, line, column)),
          line_(line),
          column_(column) {}

    [[nodiscard]] int line() const { return line_; }
    [[nodiscard]] int column() const { return column_; }

private:
    [[nodiscard]] static std::string formatMessage(const std::string& message, int line, int column) {
        return "Lingo compile error at line " + std::to_string(line) +
               ", column " + std::to_string(column) + ": " + message;
    }
    int line_;
    int column_;
};

} // namespace libreshockwave::lingo::compiler
