#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/moviesource/records/script_asset.hpp"

namespace libreshockwave {
namespace moviesource {
class MovieSource;
}

}

namespace libreshockwave::lingo::vm {

class Scope {
public:
    Scope(const moviesource::ScriptAsset* script,
          const moviesource::ScriptAsset::Handler& handler,
          std::vector<Datum> arguments = {},
          Datum receiver = Datum::voidValue(),
          bool first_param_declared_receiver = false,
          std::shared_ptr<const moviesource::ScriptAsset> script_owner = nullptr,
          std::shared_ptr<const moviesource::MovieSource> file_owner = nullptr);
    Scope(const moviesource::ScriptAsset* script,
          const moviesource::ScriptAsset::Handler* handler,
          std::vector<Datum> arguments = {},
          Datum receiver = Datum::voidValue(),
          bool first_param_declared_receiver = false,
          std::shared_ptr<const moviesource::ScriptAsset> script_owner = nullptr,
          std::shared_ptr<const moviesource::MovieSource> file_owner = nullptr);

    [[nodiscard]] const moviesource::ScriptAsset* script() const;
    [[nodiscard]] const std::shared_ptr<const moviesource::ScriptAsset>& scriptOwner() const;
    [[nodiscard]] const std::shared_ptr<const moviesource::MovieSource>& fileOwner() const;
    [[nodiscard]] const moviesource::ScriptAsset::Handler& handler() const;
    [[nodiscard]] const std::vector<Datum>& arguments() const;
    [[nodiscard]] int displayArgumentCount() const;
    [[nodiscard]] Datum displayArgument(int index) const;
    [[nodiscard]] const Datum& receiver() const;

    [[nodiscard]] int bytecodeIndex() const { return bytecode_index_; }
    void setBytecodeIndex(int index) { bytecode_index_ = index; }
    void advanceBytecodeIndex() { ++bytecode_index_; }
    [[nodiscard]] bool hasMoreInstructions() const {
        return handler_ != nullptr && bytecode_index_ >= 0 &&
               bytecode_index_ < static_cast<int>(handler_->instructions.size());
    }
    [[nodiscard]] const moviesource::ScriptAsset::Instruction* currentInstruction() const {
        if (!hasMoreInstructions()) {
            return nullptr;
        }
        return &handler_->instructions[static_cast<std::size_t>(bytecode_index_)];
    }

    void push(Datum value) { stack_.emplace_back(std::move(value)); }
    [[nodiscard]] Datum pop() {
        if (stack_.empty()) {
            return Datum::voidValue();
        }
        Datum value = std::move(stack_.back());
        stack_.pop_back();
        return value;
    }
    [[nodiscard]] Datum peek() const { return peek(0); }
    [[nodiscard]] Datum peek(int depth) const {
        const int index = static_cast<int>(stack_.size()) - 1 - depth;
        if (index < 0 || index >= static_cast<int>(stack_.size())) {
            return Datum::voidValue();
        }
        return stack_[static_cast<std::size_t>(index)];
    }
    [[nodiscard]] const Datum& peekRef(int depth = 0) const {
        static const Datum empty = Datum::voidValue();
        const int index = static_cast<int>(stack_.size()) - 1 - depth;
        if (index < 0 || index >= static_cast<int>(stack_.size())) {
            return empty;
        }
        return stack_[static_cast<std::size_t>(index)];
    }
    [[nodiscard]] int stackSize() const { return static_cast<int>(stack_.size()); }
    void swap() {
        if (stack_.size() >= 2) {
            std::iter_swap(stack_.end() - 1, stack_.end() - 2);
        }
    }
    void replaceTop(Datum value) {
        if (stack_.empty()) {
            push(std::move(value));
            return;
        }
        stack_.back() = std::move(value);
    }
    void replaceTopTwo(Datum value) {
        if (stack_.size() >= 2) {
            stack_[stack_.size() - 2] = std::move(value);
            stack_.pop_back();
            return;
        }
        stack_.clear();
        push(std::move(value));
    }
    void drop(int count) {
        if (count <= 0 || stack_.empty()) {
            return;
        }
        if (count == 1) {
            stack_.pop_back();
            return;
        }
        const auto drop_count = static_cast<std::size_t>(count);
        if (drop_count >= stack_.size()) {
            stack_.clear();
            return;
        }
        stack_.resize(stack_.size() - drop_count);
    }

    [[nodiscard]] Datum getParam(int index) const;
    void pushParam(int index);
    void setParam(int index, Datum value);
    [[nodiscard]] Datum getLocal(int index) const {
        if (index >= 0 && index < static_cast<int>(locals_.size())) {
            return locals_[static_cast<std::size_t>(index)];
        }
        return Datum::voidValue();
    }
    void pushLocal(int index) {
        if (index >= 0 && index < static_cast<int>(locals_.size())) {
            pushCopy(locals_[static_cast<std::size_t>(index)]);
            return;
        }
        push(Datum::voidValue());
    }
    void setLocal(int index, Datum value) {
        if (index >= 0 && index < static_cast<int>(locals_.size())) {
            locals_[static_cast<std::size_t>(index)] = std::move(value);
        }
    }

    [[nodiscard]] bool returned() const { return returned_; }
    void setReturned(bool returned) { returned_ = returned; }
    [[nodiscard]] Datum returnValue() const { return return_value_; }
    [[nodiscard]] Datum takeReturnValue() { return std::move(return_value_); }
    void setReturnValue(Datum value) {
        return_value_ = std::move(value);
        returned_ = true;
    }

    void pushLoopReturnIndex(int index);
    [[nodiscard]] int popLoopReturnIndex();
    [[nodiscard]] bool inLoop() const;
    [[nodiscard]] int indexedCollectionSnapshotCount(int loop_header_index,
                                                     const void* collection_identity,
                                                     const Datum& collection);
    [[nodiscard]] std::optional<Datum> indexedCollectionSnapshotValue(int loop_header_index,
                                                                       const void* collection_identity,
                                                                       const Datum& collection,
                                                                       int position);
    void clearIndexedCollectionSnapshots(int loop_header_index);

    [[nodiscard]] std::string toString() const;

private:
    struct IndexedCollectionSnapshot {
        int loop_header_index;
        const void* collection_identity;
        std::vector<Datum> values;
    };

    [[nodiscard]] int paramOffset() const;
    [[nodiscard]] int displayArgumentOffset() const;
    void pushCopy(const Datum& value) {
        if (const auto* int_value = value.asInt()) {
            push(Datum::of(int_value->value));
            return;
        }
        if (const auto* float_value = value.asFloat()) {
            push(Datum::of(float_value->value));
            return;
        }
        if (value.isVoid()) {
            push(Datum::voidValue());
            return;
        }
        if (value.isNull()) {
            push(Datum::nullValue());
            return;
        }
        push(value);
    }
    [[nodiscard]] IndexedCollectionSnapshot& indexedCollectionSnapshot(int loop_header_index,
                                                                       const void* collection_identity,
                                                                       const Datum& collection);

    const moviesource::ScriptAsset* script_;
    std::shared_ptr<const moviesource::ScriptAsset> script_owner_;
    std::shared_ptr<const moviesource::MovieSource> file_owner_;
    const moviesource::ScriptAsset::Handler* handler_;
    std::vector<Datum> arguments_;
    Datum receiver_;
    bool first_param_declared_receiver_;
    mutable int cached_param_offset_ = -1;

    std::vector<Datum> locals_;
    std::vector<std::optional<Datum>> modified_params_;
    std::vector<Datum> stack_;
    int bytecode_index_ = 0;
    Datum return_value_{Datum::voidValue()};
    bool returned_ = false;
    std::vector<int> loop_return_stack_;
    std::vector<IndexedCollectionSnapshot> indexed_collection_snapshots_;
};

} // namespace libreshockwave::lingo::vm
