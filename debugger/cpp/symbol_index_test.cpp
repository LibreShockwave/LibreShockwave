#include "model/SymbolIndex.hpp"

#include <cassert>
#include <string>
#include <vector>

using libreshockwave::debugger::DeclarationKind;
using libreshockwave::debugger::DeclarationTarget;
using libreshockwave::debugger::MovieTreeSnapshot;
using libreshockwave::debugger::SymbolIndex;

namespace {

MovieTreeSnapshot makeSnapshot() {
    MovieTreeSnapshot snapshot;
    MovieTreeSnapshot::MovieEntry movie;
    movie.castLibNumber = 1;
    movie.name = "test.cct";

    MovieTreeSnapshot::ScriptEntry mainScript;
    mainScript.scriptId = 1;
    mainScript.castLibNumber = 1;
    mainScript.displayName = "MovieScript 1";
    mainScript.handlers = {{"handleMouseUp"}, {"foo"}};
    mainScript.propertyNames = {"speed"};
    mainScript.globalNames = {"gCount"};
    movie.scripts.push_back(mainScript);

    MovieTreeSnapshot::ScriptEntry behavior;
    behavior.scriptId = 2;
    behavior.castLibNumber = 1;
    behavior.displayName = "Figure Class";
    behavior.handlers = {{"foo"}, {"draw"}};
    behavior.propertyNames = {"color"};
    movie.scripts.push_back(behavior);

    snapshot.movies.push_back(std::move(movie));
    return snapshot;
}

} // namespace

int main() {
    // Empty index.
    SymbolIndex empty;
    assert(empty.empty());
    assert(empty.find("foo", 1).empty());
    assert(empty.methodNames().empty());

    SymbolIndex index{makeSnapshot()};
    assert(!index.empty());

    // Method lookup prefers the given script, snapshot order otherwise.
    std::vector<DeclarationTarget> methods = index.find("foo", 2);
    assert(methods.size() == 2);
    assert(methods[0].kind == DeclarationKind::Method);
    assert(methods[0].castLibNumber == 1);
    assert(methods[0].scriptId == 2);
    assert(methods[0].scriptName == "Figure Class");
    assert(methods[0].handlerName == "foo");
    assert(methods[1].scriptId == 1);
    assert(methods[1].scriptName == "MovieScript 1");

    // Lookup is case-insensitive; preference flips the order.
    methods = index.find("FOO", 1);
    assert(methods.size() == 2);
    assert(methods[0].scriptId == 1);
    assert(methods[1].scriptId == 2);

    // Property and global targets carry no handler name.
    auto properties = index.find("speed", 0);
    assert(properties.size() == 1);
    assert(properties[0].kind == DeclarationKind::Property);
    assert(properties[0].scriptId == 1);
    assert(properties[0].handlerName.empty());

    auto globals = index.find("gcount", 0);
    assert(globals.size() == 1);
    assert(globals[0].kind == DeclarationKind::Global);
    assert(globals[0].scriptId == 1);

    // Unknown names resolve to nothing.
    assert(index.find("missing", 1).empty());

    // Method names: lower-cased, snapshot order, duplicates removed.
    const std::vector<std::string>& names = index.methodNames();
    assert(names.size() == 3);
    assert(names[0] == "handlemouseup");
    assert(names[1] == "foo");
    assert(names[2] == "draw");

    // Scripts sharing an ID across cast libraries stay distinct.
    MovieTreeSnapshot multi = makeSnapshot();
    MovieTreeSnapshot::MovieEntry second;
    second.castLibNumber = 2;
    second.name = "extra.cct";
    MovieTreeSnapshot::ScriptEntry extra;
    extra.scriptId = 1; // same ID as the first movie's main script
    extra.castLibNumber = 2;
    extra.displayName = "MovieScript 2";
    extra.handlers = {{"foo"}};
    second.scripts.push_back(extra);
    multi.movies.push_back(std::move(second));

    SymbolIndex multiIndex{multi};
    methods = multiIndex.find("foo", 0);
    assert(methods.size() == 3);
    assert(methods[0].castLibNumber == 1 && methods[0].scriptId == 1);
    assert(methods[1].castLibNumber == 1 && methods[1].scriptId == 2);
    assert(methods[2].castLibNumber == 2 && methods[2].scriptId == 1);

    return 0;
}
