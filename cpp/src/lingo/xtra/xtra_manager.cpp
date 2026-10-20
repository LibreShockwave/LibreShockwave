#include "libreshockwave/lingo/xtra/xtra_manager.hpp"

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
    if (const auto existing = by_name_.find(key); existing != by_name_.end()) {
        for (auto& registered : xtras_) {
            if (registered.get() == existing->second) {
                registered = std::move(xtra);
                existing->second = raw;
                return;
            }
        }
    }
    xtras_.push_back(std::move(xtra));
    by_name_[std::move(key)] = raw;
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

Datum XtraManager::createInstance(std::string_view xtra_name, const std::vector<Datum>& args) {
    auto* xtra = getXtra(xtra_name);
    if (xtra == nullptr) {
        return Datum::voidValue();
    }
    return Datum::xtraInstance(std::string(xtra_name), xtra->createInstance(args));
}

Datum XtraManager::callHandler(const Datum::XtraInstance& instance,
                               std::string_view handler_name,
                               const std::vector<Datum>& args) {
    auto* xtra = getXtra(instance.xtra_name);
    if (xtra == nullptr) {
        return Datum::voidValue();
    }
    return xtra->callHandler(instance.instance_id, handler_name, args);
}

Datum XtraManager::getProperty(const Datum::XtraInstance& instance, std::string_view property_name) const {
    const auto* xtra = getXtra(instance.xtra_name);
    if (xtra == nullptr) {
        return Datum::voidValue();
    }
    return xtra->getProperty(instance.instance_id, property_name);
}

void XtraManager::setProperty(const Datum::XtraInstance& instance,
                              std::string_view property_name,
                              const Datum& value) {
    auto* xtra = getXtra(instance.xtra_name);
    if (xtra != nullptr) {
        xtra->setProperty(instance.instance_id, property_name, value);
    }
}

void XtraManager::destroyInstance(const Datum::XtraInstance& instance) {
    auto* xtra = getXtra(instance.xtra_name);
    if (xtra != nullptr) {
        xtra->destroyInstance(instance.instance_id);
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
    const auto found = by_name_.find(toLookupName(name));
    return found == by_name_.end() ? nullptr : found->second;
}

const Xtra* XtraManager::getXtra(std::string_view name) const {
    const auto found = by_name_.find(toLookupName(name));
    return found == by_name_.end() ? nullptr : found->second;
}

} // namespace libreshockwave::lingo::xtra
