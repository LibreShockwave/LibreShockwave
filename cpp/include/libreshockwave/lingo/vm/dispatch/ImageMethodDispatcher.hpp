#pragma once

#include "libreshockwave/lingo/Datum.hpp"

#include <functional>
#include <string_view>
#include <vector>

namespace libreshockwave::lingo::builtin {
struct BuiltinContext;
} // namespace libreshockwave::lingo::builtin

namespace libreshockwave::lingo::vm::dispatch {

class ImageMethodDispatcher {
public:
    static void setImageMutationCallback(std::function<void()> callback);
    static void setImageMutationCallback(void* owner, std::function<void()> callback);
    static void clearImageMutationCallback(void* owner);
    [[nodiscard]] static Datum dispatch(const Datum::ImageRef& imageRef,
                                        std::string_view methodName,
                                        const std::vector<Datum>& args);
    [[nodiscard]] static Datum getProperty(const Datum::ImageRef& imageRef, std::string_view propName);
    static void setProperty(builtin::BuiltinContext* context,
                            const Datum::ImageRef& imageRef,
                            std::string_view propName,
                            const Datum& value);
};

} // namespace libreshockwave::lingo::vm::dispatch
