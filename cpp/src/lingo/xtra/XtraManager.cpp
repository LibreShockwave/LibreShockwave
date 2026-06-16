#include "libreshockwave/lingo/xtra/XtraManager.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace libreshockwave::lingo::xtra {

void XtraManager::registerXtra(std::unique_ptr<Xtra> xtra) {
    if (xtra == nullptr) {
        return;
    }
    auto key = toLookupName(xtra->name());
    auto* raw = xtra.get();
    if (const auto existing = byName_.find(key); existing != byName_.end()) {
        for (auto& registered : xtras_) {
            if (registered.get() == existing->second) {
                registered = std::move(xtra);
                existing->second = raw;
                return;
            }
        }
    }
    xtras_.push_back(std::move(xtra));
    byName_[std::move(key)] = raw;
}

bool XtraManager::isXtraRegistered(std::string_view name) const {
    return getXtra(name) != nullptr;
}

std::vector<std::string> XtraManager::registeredXtraNames() const {
    std::vector<std::string> names;
    names.reserve(xtras_.size());
    for (const auto& xtra : xtras_) {
        names.push_back(toDirectorXtraListName(xtra->name()));
    }
    return names;
}

int XtraManager::registeredXtraCount() const {
    return static_cast<int>(xtras_.size());
}

Datum XtraManager::createInstance(std::string_view xtraName, const std::vector<Datum>& args) {
    auto* xtra = getXtra(xtraName);
    if (xtra == nullptr) {
        return Datum::voidValue();
    }
    return Datum::xtraInstance(std::string(xtraName), xtra->createInstance(args));
}

Datum XtraManager::callHandler(const Datum::XtraInstance& instance,
                               std::string_view handlerName,
                               const std::vector<Datum>& args) {
    auto* xtra = getXtra(instance.xtraName);
    if (xtra == nullptr) {
        return Datum::voidValue();
    }
    return xtra->callHandler(instance.instanceId, handlerName, args);
}

Datum XtraManager::getProperty(const Datum::XtraInstance& instance, std::string_view propertyName) const {
    const auto* xtra = getXtra(instance.xtraName);
    if (xtra == nullptr) {
        return Datum::voidValue();
    }
    return xtra->getProperty(instance.instanceId, propertyName);
}

void XtraManager::setProperty(const Datum::XtraInstance& instance,
                              std::string_view propertyName,
                              const Datum& value) {
    auto* xtra = getXtra(instance.xtraName);
    if (xtra != nullptr) {
        xtra->setProperty(instance.instanceId, propertyName, value);
    }
}

void XtraManager::destroyInstance(const Datum::XtraInstance& instance) {
    auto* xtra = getXtra(instance.xtraName);
    if (xtra != nullptr) {
        xtra->destroyInstance(instance.instanceId);
    }
}

void XtraManager::tickAll() {
    for (const auto& xtra : xtras_) {
        xtra->tick();
    }
}

std::string XtraManager::toLookupName(std::string_view name) {
    std::string result(name);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (result == "multiusr") {
        return "multiuser";
    }
    return result;
}

std::string XtraManager::toDirectorXtraListName(std::string_view name) {
    const auto lookup = toLookupName(name);
    if (lookup == "multiuser") {
        return "Multiusr";
    }
    return std::string(name);
}

Xtra* XtraManager::getXtra(std::string_view name) {
    const auto found = byName_.find(toLookupName(name));
    return found == byName_.end() ? nullptr : found->second;
}

const Xtra* XtraManager::getXtra(std::string_view name) const {
    const auto found = byName_.find(toLookupName(name));
    return found == byName_.end() ? nullptr : found->second;
}

} // namespace libreshockwave::lingo::xtra
