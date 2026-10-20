#include "libreshockwave/player/behavior/behavior_instance.hpp"

#include <sstream>
#include <utility>

namespace libreshockwave::player::behavior {

BehaviorInstance::BehaviorInstance(std::shared_ptr<chunks::ScriptChunk> script,
                                   score::ScoreBehaviorRef behavior_ref,
                                   int sprite_num)
    : id_(nextId()),
      script_(std::move(script)),
      behavior_ref_(std::move(behavior_ref)),
      sprite_num_(sprite_num),
      receiver_(lingo::Datum::scriptInstance(scriptDisplayName(script_))) {
    receiver_.scriptInstanceValue().setProperty("sprite_num", lingo::Datum::of(sprite_num_));
}

int BehaviorInstance::id() const { return id_; }
const std::shared_ptr<chunks::ScriptChunk>& BehaviorInstance::script() const { return script_; }
const score::ScoreBehaviorRef& BehaviorInstance::behaviorRef() const { return behavior_ref_; }
int BehaviorInstance::spriteNum() const { return sprite_num_; }
bool BehaviorInstance::isFrameBehavior() const { return sprite_num_ == 0; }

lingo::Datum BehaviorInstance::getProperty(const std::string& name) const {
    return receiver_.scriptInstanceValue().getProperty(name);
}

void BehaviorInstance::setProperty(const std::string& name, lingo::Datum value) {
    receiver_.scriptInstanceValue().setProperty(name, std::move(value));
}

const std::vector<std::pair<std::string, lingo::Datum>>& BehaviorInstance::properties() const {
    return receiver_.scriptInstanceValue().properties();
}

bool BehaviorInstance::isBeginSpriteCalled() const { return begin_sprite_called_; }
void BehaviorInstance::setBeginSpriteCalled(bool called) { begin_sprite_called_ = called; }
bool BehaviorInstance::isEndSpriteCalled() const { return end_sprite_called_; }
void BehaviorInstance::setEndSpriteCalled(bool called) { end_sprite_called_ = called; }

lingo::Datum BehaviorInstance::toDatum() const {
    return receiver_;
}

std::string BehaviorInstance::toString() const {
    std::ostringstream out;
    out << "BehaviorInstance{id=" << id_
        << ", sprite_num=" << sprite_num_
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
