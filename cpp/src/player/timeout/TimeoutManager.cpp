#include "libreshockwave/player/timeout/TimeoutManager.hpp"

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
                                           int periodMs,
                                           std::string handler,
                                           lingo::Datum target) {
    return createTimeout(std::move(name), periodMs, std::move(handler), std::move(target), false);
}

lingo::Datum TimeoutManager::createTimeout(std::string name,
                                           int periodMs,
                                           std::string handler,
                                           lingo::Datum target,
                                           bool oneShot) {
    const auto now = currentTimeMillis();
    const std::string timeoutName = name;
    TimeoutEntry entry{std::move(name), periodMs, std::move(handler), std::move(target), false, oneShot, now};
    timeouts_[timeoutName] = std::move(entry);
    return lingo::Datum::timeoutRef(timeoutName);
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
        return lingo::Datum::of(entry->periodMs);
    }
    if (key == "handler") {
        return lingo::Datum::symbol(entry->handler);
    }
    if (key == "persistent") {
        return entry->persistent ? lingo::Datum::TRUE : lingo::Datum::FALSE;
    }
    if (key == "time") {
        return lingo::Datum::of(static_cast<int>(currentTimeMillis() - entry->lastFiredMs));
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
        entry.periodMs = value.intValue();
    } else if (key == "handler") {
        if (const auto* symbol = value.asSymbol()) {
            entry.handler = symbol->name;
        } else {
            entry.handler = value.stringValue();
        }
    } else if (key == "persistent") {
        entry.persistent = value.boolValue();
    } else if (key == "oneshot") {
        entry.oneShot = value.boolValue();
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

void TimeoutManager::processTimeouts(long long currentTimeMs, const TimeoutInvoker& invoker) {
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
        if (currentTimeMs - entry.lastFiredMs < entry.periodMs) {
            continue;
        }

        entry.lastFiredMs = currentTimeMs;
        TimeoutEntry fired = entry;
        if (entry.oneShot) {
            timeouts_.erase(it);
        }
        invoker(fired);
    }
}

void TimeoutManager::dispatchSystemEvent(std::string_view handlerName, const SystemEventInvoker& invoker) const {
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
            invoker(entry.target, handlerName);
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
