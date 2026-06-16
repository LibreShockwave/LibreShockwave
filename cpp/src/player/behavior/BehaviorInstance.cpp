#include "libreshockwave/player/behavior/BehaviorInstance.hpp"

#include <sstream>
#include <utility>

namespace libreshockwave::player::behavior {

BehaviorInstance::BehaviorInstance(std::shared_ptr<chunks::ScriptChunk> script,
                                   score::ScoreBehaviorRef behaviorRef,
                                   int spriteNum)
    : id_(nextId()),
      script_(std::move(script)),
      behaviorRef_(std::move(behaviorRef)),
      spriteNum_(spriteNum),
      receiver_(lingo::Datum::scriptInstance(scriptDisplayName(script_))) {
    receiver_.scriptInstanceValue().setProperty("spriteNum", lingo::Datum::of(spriteNum_));
}

int BehaviorInstance::id() const { return id_; }
const std::shared_ptr<chunks::ScriptChunk>& BehaviorInstance::script() const { return script_; }
const score::ScoreBehaviorRef& BehaviorInstance::behaviorRef() const { return behaviorRef_; }
int BehaviorInstance::spriteNum() const { return spriteNum_; }
bool BehaviorInstance::isFrameBehavior() const { return spriteNum_ == 0; }

lingo::Datum BehaviorInstance::getProperty(const std::string& name) const {
    return receiver_.scriptInstanceValue().getProperty(name);
}

void BehaviorInstance::setProperty(const std::string& name, lingo::Datum value) {
    receiver_.scriptInstanceValue().setProperty(name, std::move(value));
}

const std::vector<std::pair<std::string, lingo::Datum>>& BehaviorInstance::properties() const {
    return receiver_.scriptInstanceValue().properties();
}

bool BehaviorInstance::isBeginSpriteCalled() const { return beginSpriteCalled_; }
void BehaviorInstance::setBeginSpriteCalled(bool called) { beginSpriteCalled_ = called; }
bool BehaviorInstance::isEndSpriteCalled() const { return endSpriteCalled_; }
void BehaviorInstance::setEndSpriteCalled(bool called) { endSpriteCalled_ = called; }

lingo::Datum BehaviorInstance::toDatum() const {
    return receiver_;
}

std::string BehaviorInstance::toString() const {
    std::ostringstream out;
    out << "BehaviorInstance{id=" << id_
        << ", spriteNum=" << spriteNum_
        << ", script=";
    if (script_) {
        out << "\"script #" << script_->id().value() << "\" #" << script_->id().value();
    } else {
        out << "null";
    }
    out << "}";
    return out.str();
}

int BehaviorInstance::nextId() {
    static int next = 1;
    return next++;
}

std::string BehaviorInstance::scriptDisplayName(const std::shared_ptr<chunks::ScriptChunk>& script) {
    return script ? "script #" + std::to_string(script->id().value()) : "null";
}

} // namespace libreshockwave::player::behavior
