#pragma once

#include <cstddef>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace libreshockwave::lingo {

/// Shared maintenance step for the "first property with this key" indexes used
/// by prop lists and script instances. Removing one property shifts every later
/// property down by one, so the stored positions must follow it. The index can
/// only be adjusted in place while each key appears once; when duplicate keys
/// exist the next occurrence of the erased key is unknown, so the function
/// reports false and the caller marks the index for a lazy rebuild.
/// Returns true when the index is still exact after the call.
/// Example:
/// `if (!maintainFirstKeyIndexAfterErase(index, name, position, properties.size()))
///     index_dirty = true;`
template <typename IndexMap, typename Key>
[[nodiscard]] bool maintainFirstKeyIndexAfterErase(IndexMap& index,
                                                   const Key& erased_key,
                                                   int erased_index,
                                                   std::size_t property_count) {
    if (index.size() != property_count) {
        return false;
    }
    index.erase(erased_key);
    for (auto& entry : index) {
        if (entry.second > erased_index) {
            --entry.second;
        }
    }
    return true;
}

} // namespace libreshockwave::lingo
