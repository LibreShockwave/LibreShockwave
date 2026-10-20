#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

#include "libreshockwave/lingo/vm/trace_listener.hpp"

namespace libreshockwave::lingo::vm::trace {

class ConsoleTracePrinter final : public TraceListener {
public:
    using OutputHandler = std::function<void(std::string_view line)>;

    explicit ConsoleTracePrinter(OutputHandler output_handler = {});

    void setOutputHandler(OutputHandler output_handler);

    void onHandlerEnter(const HandlerInfo& info) override;
    void onHandlerExit(const HandlerInfo& info, const Datum& return_value) override;
    void onInstruction(const InstructionInfo& info) override;

    [[nodiscard]] static std::string formatInstruction(const InstructionInfo& info);
    [[nodiscard]] static std::string formatHandlerEnter(const HandlerInfo& info);
    [[nodiscard]] static std::optional<std::string> formatHandlerExit(const HandlerInfo& info,
                                                                      const Datum& return_value);

private:
    void emit(std::string_view line) const;
    void resetForHandler(int handler_id);
    [[nodiscard]] bool shouldSuppressInstruction(int offset);

    OutputHandler output_handler_;
    std::unordered_set<int> visited_offsets_;
    bool loop_suppressed_{false};
    int current_handler_id_{-1};
};

} // namespace libreshockwave::lingo::vm::trace
