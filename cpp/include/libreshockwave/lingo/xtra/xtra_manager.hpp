#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "libreshockwave/lingo/datum.hpp"

namespace libreshockwave::lingo::xtra {

class Xtra {
public:
    virtual ~Xtra() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual int createInstance(const std::vector<Datum>& args) = 0;
    virtual void destroyInstance(int instance_id) = 0;
    [[nodiscard]] virtual Datum callHandler(int instance_id,
                                            std::string_view handler_name,
                                            const std::vector<Datum>& args) = 0;
    [[nodiscard]] virtual Datum getProperty(int instance_id, std::string_view property_name) const = 0;
    virtual void setProperty(int instance_id, std::string_view property_name, const Datum& value) = 0;
    virtual void tick() {}
};

class XtraManager {
public:
    void registerXtra(std::unique_ptr<Xtra> xtra);
    [[nodiscard]] bool isXtraRegistered(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> registeredXtraNames() const;
    [[nodiscard]] int registeredXtraCount() const;
    [[nodiscard]] Datum createInstance(std::string_view xtra_name, const std::vector<Datum>& args);
    [[nodiscard]] Datum callHandler(const Datum::XtraInstance& instance,
                                    std::string_view handler_name,
                                    const std::vector<Datum>& args);
    [[nodiscard]] Datum getProperty(const Datum::XtraInstance& instance, std::string_view property_name) const;
    void setProperty(const Datum::XtraInstance& instance, std::string_view property_name, const Datum& value);
    void destroyInstance(const Datum::XtraInstance& instance);
    void tickAll();

    [[nodiscard]] static std::string toLookupName(std::string_view name);
    [[nodiscard]] static std::string toDirectorXtraListName(std::string_view name);

private:
    [[nodiscard]] Xtra* getXtra(std::string_view name);
    [[nodiscard]] const Xtra* getXtra(std::string_view name) const;

    std::vector<std::unique_ptr<Xtra>> xtras_;
    std::unordered_map<std::string, Xtra*> by_name_;
};

} // namespace libreshockwave::lingo::xtra
