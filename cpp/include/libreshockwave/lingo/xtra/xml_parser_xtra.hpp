#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/lingo/xtra/xtra_manager.hpp"

namespace libreshockwave::lingo::xtra {

class XmlParserXtra : public Xtra {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] int createInstance(const std::vector<Datum>& args = {}) override;
    void destroyInstance(int instance_id) override;

    [[nodiscard]] Datum callHandler(int instance_id,
                                    std::string_view handler_name,
                                    const std::vector<Datum>& args = {}) override;
    [[nodiscard]] Datum getProperty(int instance_id, std::string_view property_name) const override;
    void setProperty(int instance_id, std::string_view property_name, const Datum& value) override;

private:
    struct InstanceState {
        Datum root;
        std::string error;
        bool has_error = false;
    };

    [[nodiscard]] static Datum emptyDocument();
    [[nodiscard]] static Datum count(const Datum& root, const std::vector<Datum>& args);
    [[nodiscard]] static Datum getProp(const Datum& root, const std::vector<Datum>& args);
    [[nodiscard]] static Datum getRootProperty(const Datum& root, std::string_view property_name);
    [[nodiscard]] static std::string keyName(const Datum& datum);
    [[nodiscard]] static bool keyIsSymbol(const Datum& datum);

    std::unordered_map<int, InstanceState> instances_;
    int next_instance_id_ = 1;
};

} // namespace libreshockwave::lingo::xtra
