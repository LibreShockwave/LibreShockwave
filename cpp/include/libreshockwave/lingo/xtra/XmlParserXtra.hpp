#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "libreshockwave/lingo/Datum.hpp"
#include "libreshockwave/lingo/xtra/XtraManager.hpp"

namespace libreshockwave::lingo::xtra {

class XmlParserXtra : public Xtra {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] int createInstance(const std::vector<Datum>& args = {}) override;
    void destroyInstance(int instanceId) override;

    [[nodiscard]] Datum callHandler(int instanceId,
                                    std::string_view handlerName,
                                    const std::vector<Datum>& args = {}) override;
    [[nodiscard]] Datum getProperty(int instanceId, std::string_view propertyName) const override;
    void setProperty(int instanceId, std::string_view propertyName, const Datum& value) override;

private:
    struct InstanceState {
        Datum root;
        std::string error;
        bool hasError = false;
    };

    [[nodiscard]] static Datum emptyDocument();
    [[nodiscard]] static Datum count(const Datum& root, const std::vector<Datum>& args);
    [[nodiscard]] static Datum getProp(const Datum& root, const std::vector<Datum>& args);
    [[nodiscard]] static Datum getRootProperty(const Datum& root, std::string_view propertyName);
    [[nodiscard]] static std::string keyName(const Datum& datum);
    [[nodiscard]] static bool keyIsSymbol(const Datum& datum);

    std::unordered_map<int, InstanceState> instances_;
    int nextInstanceId_ = 1;
};

} // namespace libreshockwave::lingo::xtra
