#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

#include "libreshockwave/lingo/vm/TraceListener.hpp"

namespace libreshockwave::lingo::vm::trace {

class ConsoleTracePrinter final : public TraceListener {
public:
    using OutputHandler = std::function<void(std::string_view line)>;

    explicit ConsoleTracePrinter(OutputHandler outputHandler = {});

    void setOutputHandler(OutputHandler outputHandler);

    void onHandlerEnter(const HandlerInfo& info) override;
    void onHandlerExit(const HandlerInfo& info, const Datum& returnValue) override;
    void onInstruction(const InstructionInfo& info) override;

    [[nodiscard]] static std::string formatInstruction(const InstructionInfo& info);
    [[nodiscard]] static std::string formatHandlerEnter(const HandlerInfo& info);
    [[nodiscard]] static std::optional<std::string> formatHandlerExit(const HandlerInfo& info,
                                                                      const Datum& returnValue);

private:
    void emit(std::string_view line) const;
    void resetForHandler(int handlerId);
    [[nodiscard]] bool shouldSuppressInstruction(int offset);

    OutputHandler outputHandler_;
    std::unordered_set<int> visitedOffsets_;
    bool loopSuppressed_{false};
    int currentHandlerId_{-1};
};

} // namespace libreshockwave::lingo::vm::trace
