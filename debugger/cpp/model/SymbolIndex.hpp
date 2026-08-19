#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "DebuggerModel.hpp"

namespace libreshockwave::debugger {

/// Movie-wide declaration index built from a MovieTreeSnapshot on the main
/// thread.  Maps lower-cased names to their declaration sites (handlers,
/// behavior properties, and globals) so the code view's right-click "Go to
/// declaration" can resolve a word without touching live cast state.
///
/// Usage:
///   SymbolIndex index{snapshot};                 // from a VM-thread snapshot
///   auto target = index.find("foo", currentScriptId); // same-script preference
class SymbolIndex {
public:
    SymbolIndex() = default;
    explicit SymbolIndex(const MovieTreeSnapshot& snapshot);

    /// Look up a (case-insensitive) name and return every declaration site
    /// sharing it: methods first (the one in `preferScriptId` floated to the
    /// front, snapshot order kept for the rest), then properties, then
    /// globals.  Empty when the name is not declared anywhere in the movie.
    [[nodiscard]] std::vector<DeclarationTarget> find(
        const std::string& name, int preferScriptId) const;

    /// All lower-cased handler names, in snapshot order (duplicates removed).
    /// Used to color method calls in the decompiled view.
    [[nodiscard]] const std::vector<std::string>& methodNames() const;

    /// All lower-cased declaration names (methods, properties, and globals),
    /// in snapshot order (duplicates removed).  These are exactly the words
    /// the code view underlines as right-click "Go to declaration" targets.
    [[nodiscard]] const std::vector<std::string>& declarationNames() const;

    [[nodiscard]] bool empty() const;

    /// Lower-cased lookup key for a name.
    static std::string keyFor(const std::string& name);

private:
    std::unordered_map<std::string, std::vector<DeclarationTarget>> byName_;
    std::vector<std::string> methodNames_;
    std::vector<std::string> declarationNames_;
};

} // namespace libreshockwave::debugger
