#include "libreshockwave/lingo/vm/Scope.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace libreshockwave::lingo::vm {

Scope::Scope(const chunks::ScriptChunk* script,
             const chunks::ScriptChunk::Handler& handler,
             std::vector<Datum> arguments,
             Datum receiver,
             bool firstParamDeclaredMe,
             std::shared_ptr<const chunks::ScriptChunk> scriptOwner,
             std::shared_ptr<const DirectorFile> fileOwner,
             std::shared_ptr<const chunks::ScriptNamesChunk> scriptNamesOwner)
    : Scope(script,
            &handler,
            std::move(arguments),
            std::move(receiver),
            firstParamDeclaredMe,
            std::move(scriptOwner),
            std::move(fileOwner),
            std::move(scriptNamesOwner)) {}

Scope::Scope(const chunks::ScriptChunk* script,
             const chunks::ScriptChunk::Handler* handler,
             std::vector<Datum> arguments,
             Datum receiver,
             bool firstParamDeclaredMe,
             std::shared_ptr<const chunks::ScriptChunk> scriptOwner,
             std::shared_ptr<const DirectorFile> fileOwner,
             std::shared_ptr<const chunks::ScriptNamesChunk> scriptNamesOwner)
    : script_(script),
      scriptOwner_(std::move(scriptOwner)),
      fileOwner_(std::move(fileOwner)),
      scriptNamesOwner_(std::move(scriptNamesOwner)),
      handler_(handler),
      arguments_(std::move(arguments)),
      receiver_(std::move(receiver)),
      firstParamDeclaredMe_(firstParamDeclaredMe),
      locals_(static_cast<std::size_t>(std::max(0, handler_ != nullptr ? handler_->localCount : 0))) {
    stack_.reserve(16);
    loopReturnStack_.reserve(4);
}

const chunks::ScriptChunk* Scope::script() const {
    return script_;
}

const std::shared_ptr<const chunks::ScriptChunk>& Scope::scriptOwner() const {
    return scriptOwner_;
}

const std::shared_ptr<const DirectorFile>& Scope::fileOwner() const {
    return fileOwner_;
}

const std::shared_ptr<const chunks::ScriptNamesChunk>& Scope::scriptNamesOwner() const {
    return scriptNamesOwner_;
}

const chunks::ScriptChunk::Handler& Scope::handler() const {
    static const chunks::ScriptChunk::Handler empty{};
    return handler_ != nullptr ? *handler_ : empty;
}

const std::vector<Datum>& Scope::arguments() const {
    return arguments_;
}

int Scope::displayArgumentCount() const {
    return std::max(0, static_cast<int>(arguments_.size()) - displayArgumentOffset());
}

Datum Scope::displayArgument(int index) const {
    if (index < 0 || index >= displayArgumentCount()) {
        return Datum::voidValue();
    }
    const auto modifiedIndex = static_cast<std::size_t>(index);
    if (modifiedIndex < modifiedParams_.size() && modifiedParams_[modifiedIndex]) {
        return *modifiedParams_[modifiedIndex];
    }

    const int originalIndex = index + displayArgumentOffset();
    if (originalIndex >= 0 && originalIndex < static_cast<int>(arguments_.size())) {
        return arguments_[static_cast<std::size_t>(originalIndex)];
    }
    return Datum::voidValue();
}

const Datum& Scope::receiver() const {
    return receiver_;
}

Datum Scope::getParam(int index) const {
    const int actualIndex = index + paramOffset();
    if (index >= 0 && index < static_cast<int>(modifiedParams_.size())) {
        const auto& modified = modifiedParams_[static_cast<std::size_t>(index)];
        if (modified) {
            return *modified;
        }
    }
    if (actualIndex >= 0 && actualIndex < static_cast<int>(arguments_.size())) {
        return arguments_[static_cast<std::size_t>(actualIndex)];
    }
    return Datum::voidValue();
}

void Scope::pushParam(int index) {
    const int actualIndex = index + paramOffset();
    if (index >= 0 && index < static_cast<int>(modifiedParams_.size())) {
        const auto& modified = modifiedParams_[static_cast<std::size_t>(index)];
        if (modified) {
            push(*modified);
            return;
        }
    }
    if (actualIndex >= 0 && actualIndex < static_cast<int>(arguments_.size())) {
        pushCopy(arguments_[static_cast<std::size_t>(actualIndex)]);
        return;
    }
    push(Datum::voidValue());
}

void Scope::setParam(int index, Datum value) {
    if (index < 0) {
        return;
    }
    const auto targetSize = static_cast<std::size_t>(index + 1);
    if (modifiedParams_.size() < targetSize) {
        modifiedParams_.resize(targetSize);
    }
    modifiedParams_[static_cast<std::size_t>(index)] = std::move(value);
}

void Scope::pushLoopReturnIndex(int index) {
    loopReturnStack_.push_back(index);
}

int Scope::popLoopReturnIndex() {
    if (loopReturnStack_.empty()) {
        return -1;
    }
    const int index = loopReturnStack_.back();
    loopReturnStack_.pop_back();
    return index;
}

bool Scope::inLoop() const {
    return !loopReturnStack_.empty();
}

Scope::IndexedCollectionSnapshot& Scope::indexedCollectionSnapshot(int loopHeaderIndex,
                                                                   const void* collectionIdentity,
                                                                   const Datum& collection) {
    auto snapshot = std::find_if(indexedCollectionSnapshots_.begin(),
                                 indexedCollectionSnapshots_.end(),
                                 [&](const IndexedCollectionSnapshot& candidate) {
                                     return candidate.loopHeaderIndex == loopHeaderIndex &&
                                            candidate.collectionIdentity == collectionIdentity;
                                 });
    if (snapshot == indexedCollectionSnapshots_.end()) {
        IndexedCollectionSnapshot created;
        created.loopHeaderIndex = loopHeaderIndex;
        created.collectionIdentity = collectionIdentity;
        if (collection.isList()) {
            created.values = collection.listValue().items();
        } else {
            const auto& properties = collection.propListValue().properties();
            created.values.reserve(properties.size());
            for (const auto& entry : properties) {
                created.values.push_back(entry.second);
            }
        }
        indexedCollectionSnapshots_.push_back(std::move(created));
        snapshot = indexedCollectionSnapshots_.end() - 1;
    }
    return *snapshot;
}

int Scope::indexedCollectionSnapshotCount(int loopHeaderIndex,
                                          const void* collectionIdentity,
                                          const Datum& collection) {
    if (collectionIdentity == nullptr || (!collection.isList() && !collection.isPropList())) {
        return 0;
    }
    return static_cast<int>(indexedCollectionSnapshot(loopHeaderIndex, collectionIdentity, collection).values.size());
}

std::optional<Datum> Scope::indexedCollectionSnapshotValue(int loopHeaderIndex,
                                                           const void* collectionIdentity,
                                                           const Datum& collection,
                                                           int position) {
    if (collectionIdentity == nullptr || position < 1 || (!collection.isList() && !collection.isPropList())) {
        return std::nullopt;
    }

    auto& snapshot = indexedCollectionSnapshot(loopHeaderIndex, collectionIdentity, collection);
    const auto index = static_cast<std::size_t>(position - 1);
    if (index >= snapshot.values.size()) {
        return Datum::voidValue();
    }
    return snapshot.values[index];
}

void Scope::clearIndexedCollectionSnapshots(int loopHeaderIndex) {
    std::erase_if(indexedCollectionSnapshots_, [loopHeaderIndex](const IndexedCollectionSnapshot& snapshot) {
        return snapshot.loopHeaderIndex == loopHeaderIndex;
    });
}

std::string Scope::toString() const {
    std::ostringstream out;
    out << "Scope{handler#" << handler().nameId << ", bytecodeIndex=" << bytecodeIndex_
        << ", stackSize=" << stack_.size() << ", returned=" << (returned_ ? "true" : "false") << "}";
    return out.str();
}

int Scope::paramOffset() const {
    if (cachedParamOffset_ < 0) {
        if (!receiver_.isVoid() && !receiver_.isNull() && !arguments_.empty() &&
            arguments_.front() == receiver_ && !firstParamDeclaredMe_) {
            cachedParamOffset_ = 1;
        } else {
            cachedParamOffset_ = 0;
        }
    }
    return cachedParamOffset_;
}

int Scope::displayArgumentOffset() const {
    if (!receiver_.isVoid() && !receiver_.isNull() && !arguments_.empty() && arguments_.front() == receiver_) {
        return 1;
    }
    return 0;
}

} // namespace libreshockwave::lingo::vm
