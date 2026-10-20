#include "libreshockwave/lingo/vm/scope.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace libreshockwave::lingo::vm {

Scope::Scope(const chunks::ScriptChunk* script,
             const chunks::ScriptChunk::Handler& handler,
             std::vector<Datum> arguments,
             Datum receiver,
             bool first_param_declared_receiver,
             std::shared_ptr<const chunks::ScriptChunk> script_owner,
             std::shared_ptr<const DirectorFile> file_owner,
             std::shared_ptr<const chunks::ScriptNamesChunk> script_names_owner)
    : Scope(script,
            &handler,
            std::move(arguments),
            std::move(receiver),
            first_param_declared_receiver,
            std::move(script_owner),
            std::move(file_owner),
            std::move(script_names_owner)) {}

Scope::Scope(const chunks::ScriptChunk* script,
             const chunks::ScriptChunk::Handler* handler,
             std::vector<Datum> arguments,
             Datum receiver,
             bool first_param_declared_receiver,
             std::shared_ptr<const chunks::ScriptChunk> script_owner,
             std::shared_ptr<const DirectorFile> file_owner,
             std::shared_ptr<const chunks::ScriptNamesChunk> script_names_owner)
    : script_(script),
      script_owner_(std::move(script_owner)),
      file_owner_(std::move(file_owner)),
      script_names_owner_(std::move(script_names_owner)),
      handler_(handler),
      arguments_(std::move(arguments)),
      receiver_(std::move(receiver)),
      first_param_declared_receiver_(first_param_declared_receiver),
      locals_(static_cast<std::size_t>(std::max(0, handler_ != nullptr ? handler_->local_count : 0))) {
    stack_.reserve(16);
    loop_return_stack_.reserve(4);
}

const chunks::ScriptChunk* Scope::script() const {
    return script_;
}

const std::shared_ptr<const chunks::ScriptChunk>& Scope::scriptOwner() const {
    return script_owner_;
}

const std::shared_ptr<const DirectorFile>& Scope::fileOwner() const {
    return file_owner_;
}

const std::shared_ptr<const chunks::ScriptNamesChunk>& Scope::scriptNamesOwner() const {
    return script_names_owner_;
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
    const auto modified_index = static_cast<std::size_t>(index);
    if (modified_index < modified_params_.size() && modified_params_[modified_index]) {
        return *modified_params_[modified_index];
    }

    const int original_index = index + displayArgumentOffset();
    if (original_index >= 0 && original_index < static_cast<int>(arguments_.size())) {
        return arguments_[static_cast<std::size_t>(original_index)];
    }
    return Datum::voidValue();
}

const Datum& Scope::receiver() const {
    return receiver_;
}

Datum Scope::getParam(int index) const {
    const int actual_index = index + paramOffset();
    if (index >= 0 && index < static_cast<int>(modified_params_.size())) {
        const auto& modified = modified_params_[static_cast<std::size_t>(index)];
        if (modified) {
            return *modified;
        }
    }
    if (actual_index >= 0 && actual_index < static_cast<int>(arguments_.size())) {
        return arguments_[static_cast<std::size_t>(actual_index)];
    }
    return Datum::voidValue();
}

void Scope::pushParam(int index) {
    const int actual_index = index + paramOffset();
    if (index >= 0 && index < static_cast<int>(modified_params_.size())) {
        const auto& modified = modified_params_[static_cast<std::size_t>(index)];
        if (modified) {
            pushCopy(*modified);
            return;
        }
    }
    if (actual_index >= 0 && actual_index < static_cast<int>(arguments_.size())) {
        pushCopy(arguments_[static_cast<std::size_t>(actual_index)]);
        return;
    }
    push(Datum::voidValue());
}

void Scope::setParam(int index, Datum value) {
    if (index < 0) {
        return;
    }
    const auto target_size = static_cast<std::size_t>(index + 1);
    if (modified_params_.size() < target_size) {
        modified_params_.resize(target_size);
    }
    modified_params_[static_cast<std::size_t>(index)] = std::move(value);
}

void Scope::pushLoopReturnIndex(int index) {
    loop_return_stack_.push_back(index);
}

int Scope::popLoopReturnIndex() {
    if (loop_return_stack_.empty()) {
        return -1;
    }
    const int index = loop_return_stack_.back();
    loop_return_stack_.pop_back();
    return index;
}

bool Scope::inLoop() const {
    return !loop_return_stack_.empty();
}

Scope::IndexedCollectionSnapshot& Scope::indexedCollectionSnapshot(int loop_header_index,
                                                                   const void* collection_identity,
                                                                   const Datum& collection) {
    auto snapshot = std::find_if(indexed_collection_snapshots_.begin(),
                                 indexed_collection_snapshots_.end(),
                                 [&](const IndexedCollectionSnapshot& candidate) {
                                     return candidate.loop_header_index == loop_header_index &&
                                            candidate.collection_identity == collection_identity;
                                 });
    if (snapshot == indexed_collection_snapshots_.end()) {
        IndexedCollectionSnapshot created;
        created.loop_header_index = loop_header_index;
        created.collection_identity = collection_identity;
        if (collection.isList()) {
            created.values = collection.listValue().items();
        } else {
            const auto& properties = collection.propListValue().properties();
            created.values.reserve(properties.size());
            for (const auto& entry : properties) {
                created.values.emplace_back(entry.second);
            }
        }
        indexed_collection_snapshots_.push_back(std::move(created));
        snapshot = indexed_collection_snapshots_.end() - 1;
    }
    return *snapshot;
}

int Scope::indexedCollectionSnapshotCount(int loop_header_index,
                                          const void* collection_identity,
                                          const Datum& collection) {
    if (collection_identity == nullptr || (!collection.isList() && !collection.isPropList())) {
        return 0;
    }
    return static_cast<int>(indexedCollectionSnapshot(loop_header_index, collection_identity, collection).values.size());
}

std::optional<Datum> Scope::indexedCollectionSnapshotValue(int loop_header_index,
                                                           const void* collection_identity,
                                                           const Datum& collection,
                                                           int position) {
    if (collection_identity == nullptr || position < 1 || (!collection.isList() && !collection.isPropList())) {
        return std::nullopt;
    }

    auto& snapshot = indexedCollectionSnapshot(loop_header_index, collection_identity, collection);
    const auto index = static_cast<std::size_t>(position - 1);
    if (index >= snapshot.values.size()) {
        return Datum::voidValue();
    }
    return snapshot.values[index];
}

void Scope::clearIndexedCollectionSnapshots(int loop_header_index) {
    std::erase_if(indexed_collection_snapshots_, [loop_header_index](const IndexedCollectionSnapshot& snapshot) {
        return snapshot.loop_header_index == loop_header_index;
    });
}

std::string Scope::toString() const {
    std::ostringstream out;
    out << "Scope{handler#" << handler().name_id << ", bytecodeIndex=" << bytecode_index_
        << ", stackSize=" << stack_.size() << ", returned=" << (returned_ ? "true" : "false") << "}";
    return out.str();
}

int Scope::paramOffset() const {
    if (cached_param_offset_ < 0) {
        if (!receiver_.isVoid() && !receiver_.isNull() && !arguments_.empty() &&
            arguments_.front() == receiver_ && !first_param_declared_receiver_) {
            cached_param_offset_ = 1;
        } else {
            cached_param_offset_ = 0;
        }
    }
    return cached_param_offset_;
}

int Scope::displayArgumentOffset() const {
    if (!receiver_.isVoid() && !receiver_.isNull() && !arguments_.empty() && arguments_.front() == receiver_) {
        return 1;
    }
    return 0;
}

} // namespace libreshockwave::lingo::vm
