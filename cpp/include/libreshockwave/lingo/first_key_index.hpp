#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace libreshockwave::lingo {

/// Shared maintenance step for the "first property with this key" indexes used
/// by prop lists and script instances. Removing one property shifts every later
/// property down by one, so the stored positions must follow it. The index can
/// only be adjusted in place while each key appears once; when duplicate keys
/// exist the next occurrence of the erased key is unknown, so the function
/// reports false and the caller marks the index for a lazy rebuild.
/// The index map is passed as type-erased operations so this helper serves
/// both the case-sensitive and case-insensitive index maps without a template.
/// `erase_key` removes the erased key from the map. `for_each_entry` must
/// visit the stored position of every remaining entry by reference.
/// Returns true when the index is still exact after the call.
/// Example:
/// `if (!maintainFirstKeyIndexAfterErase(index.size(), name, position, properties.size(),
///         erase_op, visit_op)) index_dirty = true;`
[[nodiscard]] inline bool maintainFirstKeyIndexAfterErase(
    std::size_t index_size,
    const std::string& erased_key,
    int erased_index,
    std::size_t property_count,
    const std::function<void(const std::string&)>& erase_key,
    const std::function<void(const std::function<void(int&)>&)>& for_each_entry) {
    if (index_size != property_count) {
        return false;
    }
    erase_key(erased_key);
    for_each_entry([&](int& position) {
        if (position > erased_index) {
            --position;
        }
    });
    return true;
}

} // namespace libreshockwave::lingo
