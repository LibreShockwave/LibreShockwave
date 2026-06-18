#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "libreshockwave/chunks/ScriptChunk.hpp"
#include "libreshockwave/lingo/Datum.hpp"

namespace libreshockwave {
class DirectorFile;
}

namespace libreshockwave::chunks {
class ScriptNamesChunk;
}

namespace libreshockwave::lingo::vm {

class Scope {
public:
    Scope(const chunks::ScriptChunk* script,
          const chunks::ScriptChunk::Handler& handler,
          std::vector<Datum> arguments = {},
          Datum receiver = Datum::voidValue(),
          bool firstParamDeclaredMe = false,
          std::shared_ptr<const chunks::ScriptChunk> scriptOwner = nullptr,
          std::shared_ptr<const DirectorFile> fileOwner = nullptr,
          std::shared_ptr<const chunks::ScriptNamesChunk> scriptNamesOwner = nullptr);
    Scope(const chunks::ScriptChunk* script,
          const chunks::ScriptChunk::Handler* handler,
          std::vector<Datum> arguments = {},
          Datum receiver = Datum::voidValue(),
          bool firstParamDeclaredMe = false,
          std::shared_ptr<const chunks::ScriptChunk> scriptOwner = nullptr,
          std::shared_ptr<const DirectorFile> fileOwner = nullptr,
          std::shared_ptr<const chunks::ScriptNamesChunk> scriptNamesOwner = nullptr);

    [[nodiscard]] const chunks::ScriptChunk* script() const;
    [[nodiscard]] const std::shared_ptr<const chunks::ScriptChunk>& scriptOwner() const;
    [[nodiscard]] const std::shared_ptr<const DirectorFile>& fileOwner() const;
    [[nodiscard]] const std::shared_ptr<const chunks::ScriptNamesChunk>& scriptNamesOwner() const;
    [[nodiscard]] const chunks::ScriptChunk::Handler& handler() const;
    [[nodiscard]] const std::vector<Datum>& arguments() const;
    [[nodiscard]] int displayArgumentCount() const;
    [[nodiscard]] Datum displayArgument(int index) const;
    [[nodiscard]] const Datum& receiver() const;

    [[nodiscard]] int bytecodeIndex() const { return bytecodeIndex_; }
    void setBytecodeIndex(int index) { bytecodeIndex_ = index; }
    void advanceBytecodeIndex() { ++bytecodeIndex_; }
    [[nodiscard]] bool hasMoreInstructions() const {
        return handler_ != nullptr && bytecodeIndex_ >= 0 &&
               bytecodeIndex_ < static_cast<int>(handler_->instructions.size());
    }
    [[nodiscard]] const chunks::ScriptChunk::Instruction* currentInstruction() const {
        if (!hasMoreInstructions()) {
            return nullptr;
        }
        return &handler_->instructions[static_cast<std::size_t>(bytecodeIndex_)];
    }

    void push(Datum value) { stack_.push_back(std::move(value)); }
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
        const auto newSize = static_cast<std::size_t>(std::max(0, static_cast<int>(stack_.size()) - count));
        stack_.resize(newSize);
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
    [[nodiscard]] Datum returnValue() const { return returnValue_; }
    void setReturnValue(Datum value) {
        returnValue_ = std::move(value);
        returned_ = true;
    }

    void pushLoopReturnIndex(int index);
    [[nodiscard]] int popLoopReturnIndex();
    [[nodiscard]] bool inLoop() const;
    [[nodiscard]] int indexedCollectionSnapshotCount(int loopHeaderIndex,
                                                     const void* collectionIdentity,
                                                     const Datum& collection);
    [[nodiscard]] std::optional<Datum> indexedCollectionSnapshotValue(int loopHeaderIndex,
                                                                       const void* collectionIdentity,
                                                                       const Datum& collection,
                                                                       int position);
    void clearIndexedCollectionSnapshots(int loopHeaderIndex);

    [[nodiscard]] std::string toString() const;

private:
    struct IndexedCollectionSnapshot {
        int loopHeaderIndex;
        const void* collectionIdentity;
        std::vector<Datum> values;
    };

    [[nodiscard]] int paramOffset() const;
    [[nodiscard]] int displayArgumentOffset() const;
    void pushCopy(const Datum& value) {
        if (const auto* intValue = value.asInt()) {
            push(Datum::of(intValue->value));
            return;
        }
        if (const auto* floatValue = value.asFloat()) {
            push(Datum::of(floatValue->value));
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
    [[nodiscard]] IndexedCollectionSnapshot& indexedCollectionSnapshot(int loopHeaderIndex,
                                                                       const void* collectionIdentity,
                                                                       const Datum& collection);

    const chunks::ScriptChunk* script_;
    std::shared_ptr<const chunks::ScriptChunk> scriptOwner_;
    std::shared_ptr<const DirectorFile> fileOwner_;
    std::shared_ptr<const chunks::ScriptNamesChunk> scriptNamesOwner_;
    const chunks::ScriptChunk::Handler* handler_;
    std::vector<Datum> arguments_;
    Datum receiver_;
    bool firstParamDeclaredMe_;
    mutable int cachedParamOffset_ = -1;

    std::vector<Datum> locals_;
    std::vector<std::optional<Datum>> modifiedParams_;
    std::vector<Datum> stack_;
    int bytecodeIndex_ = 0;
    Datum returnValue_{Datum::voidValue()};
    bool returned_ = false;
    std::vector<int> loopReturnStack_;
    std::vector<IndexedCollectionSnapshot> indexedCollectionSnapshots_;
};

} // namespace libreshockwave::lingo::vm
