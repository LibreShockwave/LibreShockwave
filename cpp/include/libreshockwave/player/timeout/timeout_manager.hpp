#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/lingo/datum.hpp"

namespace libreshockwave::player::timeout {

struct TimeoutEntry {
    std::string name;
    int period_ms{0};
    std::string handler;
    lingo::Datum target;
    bool persistent{false};
    bool one_shot{false};
    long long last_fired_ms{0};
};

class TimeoutManager {
public:
    using SystemEventInvoker = std::function<void(const lingo::Datum& target, std::string_view handler_name)>;
    using TimeoutInvoker = std::function<void(const TimeoutEntry& entry)>;

    [[nodiscard]] lingo::Datum createTimeout(std::string name,
                                             int period_ms,
                                             std::string handler,
                                             lingo::Datum target);
    [[nodiscard]] lingo::Datum createTimeout(std::string name,
                                             int period_ms,
                                             std::string handler,
                                             lingo::Datum target,
                                             bool one_shot);
    void forgetTimeout(const std::string& name);
    [[nodiscard]] bool timeoutExists(const std::string& name) const;
    [[nodiscard]] lingo::Datum getTimeoutProp(const std::string& name, const std::string& prop) const;
    bool setTimeoutProp(const std::string& name, const std::string& prop, lingo::Datum value);
    [[nodiscard]] std::vector<std::string> getTimeoutNames() const;
    [[nodiscard]] int getTimeoutCount() const;
    void processTimeouts(const TimeoutInvoker& invoker);
    void processTimeouts(long long current_time_ms, const TimeoutInvoker& invoker);
    void dispatchSystemEvent(std::string_view handler_name, const SystemEventInvoker& invoker) const;
    void clear();

    [[nodiscard]] const TimeoutEntry* getEntry(const std::string& name) const;

private:
    [[nodiscard]] static long long currentTimeMillis();

    std::map<std::string, TimeoutEntry> timeouts_;
};

} // namespace libreshockwave::player::timeout
