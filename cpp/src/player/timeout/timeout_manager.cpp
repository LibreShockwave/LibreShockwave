#include "libreshockwave/player/timeout/timeout_manager.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <utility>

namespace libreshockwave::player::timeout {
namespace {

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

} // namespace

lingo::Datum TimeoutManager::createTimeout(std::string name,
                                           int period_ms,
                                           std::string handler,
                                           lingo::Datum target) {
    return createTimeout(std::move(name), period_ms, std::move(handler), std::move(target), false);
}

lingo::Datum TimeoutManager::createTimeout(std::string name,
                                           int period_ms,
                                           std::string handler,
                                           lingo::Datum target,
                                           bool one_shot) {
    const auto now = currentTimeMillis();
    const std::string timeout_name = name;
    TimeoutEntry entry{std::move(name), period_ms, std::move(handler), std::move(target), false, one_shot, now};
    timeouts_[timeout_name] = std::move(entry);
    return lingo::Datum::timeoutRef(timeout_name);
}

void TimeoutManager::forgetTimeout(const std::string& name) {
    timeouts_.erase(name);
}

bool TimeoutManager::timeoutExists(const std::string& name) const {
    return timeouts_.contains(name);
}

lingo::Datum TimeoutManager::getTimeoutProp(const std::string& name, const std::string& prop) const {
    const auto* entry = getEntry(name);
    if (entry == nullptr) {
        return lingo::Datum::voidValue();
    }

    const auto key = lowerAscii(prop);
    if (key == "name") {
        return lingo::Datum::of(entry->name);
    }
    if (key == "target") {
        return entry->target;
    }
    if (key == "period") {
        return lingo::Datum::of(entry->period_ms);
    }
    if (key == "handler") {
        return lingo::Datum::symbol(entry->handler);
    }
    if (key == "persistent") {
        return entry->persistent ? lingo::Datum::TRUE : lingo::Datum::FALSE;
    }
    if (key == "time") {
        return lingo::Datum::of(static_cast<int>(currentTimeMillis() - entry->last_fired_ms));
    }
    return lingo::Datum::voidValue();
}

bool TimeoutManager::setTimeoutProp(const std::string& name, const std::string& prop, lingo::Datum value) {
    auto it = timeouts_.find(name);
    if (it == timeouts_.end()) {
        return false;
    }

    auto& entry = it->second;
    const auto key = lowerAscii(prop);
    if (key == "target") {
        entry.target = std::move(value);
    } else if (key == "period") {
        entry.period_ms = value.intValue();
    } else if (key == "handler") {
        if (const auto* symbol = value.asSymbol()) {
            entry.handler = symbol->name;
        } else {
            entry.handler = value.stringValue();
        }
    } else if (key == "persistent") {
        entry.persistent = value.boolValue();
    } else if (key == "oneshot") {
        entry.one_shot = value.boolValue();
    } else {
        return false;
    }
    return true;
}

std::vector<std::string> TimeoutManager::getTimeoutNames() const {
    std::vector<std::string> names;
    names.reserve(timeouts_.size());
    for (const auto& [name, entry] : timeouts_) {
        (void)entry;
        names.push_back(name);
    }
    return names;
}

int TimeoutManager::getTimeoutCount() const {
    return static_cast<int>(timeouts_.size());
}

void TimeoutManager::processTimeouts(const TimeoutInvoker& invoker) {
    processTimeouts(currentTimeMillis(), invoker);
}

void TimeoutManager::processTimeouts(long long current_time_ms, const TimeoutInvoker& invoker) {
    if (!invoker || timeouts_.empty()) {
        return;
    }

    std::vector<std::string> keys;
    keys.reserve(timeouts_.size());
    for (const auto& [name, entry] : timeouts_) {
        (void)entry;
        keys.push_back(name);
    }

    for (const auto& key : keys) {
        auto it = timeouts_.find(key);
        if (it == timeouts_.end()) {
            continue;
        }

        auto& entry = it->second;
        if (current_time_ms - entry.last_fired_ms < entry.period_ms) {
            continue;
        }

        entry.last_fired_ms = current_time_ms;
        TimeoutEntry fired = entry;
        if (entry.one_shot) {
            timeouts_.erase(it);
        }
        invoker(fired);
    }
}

void TimeoutManager::dispatchSystemEvent(std::string_view handler_name, const SystemEventInvoker& invoker) const {
    if (!invoker || timeouts_.empty()) {
        return;
    }

    std::vector<TimeoutEntry> targets;
    targets.reserve(timeouts_.size());
    for (const auto& [_, entry] : timeouts_) {
        targets.push_back(entry);
    }

    for (const auto& entry : targets) {
        if (entry.target.type() == lingo::DatumType::ScriptInstanceRef) {
            invoker(entry.target, handler_name);
        }
    }
}

void TimeoutManager::clear() {
    timeouts_.clear();
}

const TimeoutEntry* TimeoutManager::getEntry(const std::string& name) const {
    const auto it = timeouts_.find(name);
    return it == timeouts_.end() ? nullptr : &it->second;
}

long long TimeoutManager::currentTimeMillis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace libreshockwave::player::timeout
