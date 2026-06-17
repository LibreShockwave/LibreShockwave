#include "libreshockwave/player/cast/CastLib.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/bitmap/Palette.hpp"
#include "libreshockwave/cast/CastMember.hpp"
#include "libreshockwave/cast/MemberType.hpp"
#include "libreshockwave/chunks/CastChunk.hpp"
#include "libreshockwave/chunks/CastMemberChunk.hpp"
#include "libreshockwave/chunks/ConfigChunk.hpp"
#include "libreshockwave/chunks/KeyTableChunk.hpp"
#include "libreshockwave/chunks/RawChunk.hpp"
#include "libreshockwave/chunks/ScriptChunk.hpp"
#include "libreshockwave/chunks/ScriptNamesChunk.hpp"
#include "libreshockwave/chunks/SoundChunk.hpp"
#include "libreshockwave/chunks/TextChunk.hpp"
#include "libreshockwave/format/ChunkType.hpp"
#include "libreshockwave/io/BinaryReader.hpp"
#include "libreshockwave/player/audio/SoundManager.hpp"
#include "libreshockwave/player/cast/FontRegistry.hpp"
#include "libreshockwave/util/FileUtil.hpp"

namespace libreshockwave::player::cast {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trim(const std::string& value) {
    auto begin = value.begin();
    while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    auto end = value.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return std::string(begin, end);
}

bool equalsIgnoreCase(const std::string& lhs, const std::string& rhs) {
    return lower(lhs) == lower(rhs);
}

bool startsWithIgnoreCase(const std::string& value, const std::string& prefix) {
    if (value.size() < prefix.size()) {
        return false;
    }
    return equalsIgnoreCase(value.substr(0, prefix.size()), prefix);
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

lingo::Datum stringDatum(const std::string& value) {
    return lingo::Datum::of(value);
}

std::string shapeTypeSymbolName(libreshockwave::cast::ShapeType type) {
    switch (type) {
        case libreshockwave::cast::ShapeType::Rect: return "rect";
        case libreshockwave::cast::ShapeType::OvalRect: return "roundRect";
        case libreshockwave::cast::ShapeType::Oval: return "oval";
        case libreshockwave::cast::ShapeType::Line: return "line";
        case libreshockwave::cast::ShapeType::Unknown: return "unknown";
    }
    return "unknown";
}

std::optional<libreshockwave::cast::ShapeType> shapeTypeFromDatum(const lingo::Datum& value) {
    using libreshockwave::cast::ShapeType;

    if (value.isString() || value.isSymbol()) {
        std::string name = lower(trim(value.stringValue()));
        if (!name.empty() && name.front() == '#') {
            name.erase(name.begin());
        }
        name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char ch) {
            return ch == '_' || ch == '-' || std::isspace(ch);
        }), name.end());

        if (name == "rect" || name == "rectangle") return ShapeType::Rect;
        if (name == "roundrect" || name == "roundedrect" || name == "ovalrect") return ShapeType::OvalRect;
        if (name == "oval" || name == "ellipse") return ShapeType::Oval;
        if (name == "line") return ShapeType::Line;
        if (!value.isString()) {
            return std::nullopt;
        }
    }

    if (value.isNumber() || value.isString()) {
        const auto type = libreshockwave::cast::shapeTypeFromCode(value.intValue());
        if (type != ShapeType::Unknown) {
            return type;
        }
    }
    return std::nullopt;
}

int textBoxTypeFromDatum(const lingo::Datum& value) {
    if (value.isString() || value.isSymbol()) {
        std::string name = lower(trim(value.stringValue()));
        if (!name.empty() && name.front() == '#') {
            name.erase(name.begin());
        }
        name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char ch) {
            return ch == '_' || ch == '-' || std::isspace(ch);
        }), name.end());

        if (name == "adjust" || name == "auto" || name == "autosize") return 0;
        if (name == "fixed") return 1;
        if (name == "scroll" || name == "scrolling") return 2;
        if (name == "limit") return 3;
        if (!value.isString()) {
            throw lingo::LingoException("Cannot convert symbol to text box type");
        }
    }

    return value.intValue();
}

std::string directorMemberTypeName(const std::shared_ptr<libreshockwave::cast::CastMember>& member) {
    if (!member || member->memberType() == libreshockwave::cast::MemberType::Null) {
        return "empty";
    }
    const auto type = member->memberType();
    if (type == libreshockwave::cast::MemberType::Text ||
        type == libreshockwave::cast::MemberType::Button ||
        (type == libreshockwave::cast::MemberType::Xtra &&
         member->rawChunk() != nullptr &&
         member->rawChunk()->isTextXtra())) {
        return "field";
    }
    return std::string(libreshockwave::cast::name(type));
}

std::string scriptChunkTypePropName(chunks::ScriptChunkType type) {
    switch (type) {
        case chunks::ScriptChunkType::Score: return "score";
        case chunks::ScriptChunkType::Behavior: return "behavior";
        case chunks::ScriptChunkType::MovieScript: return "movie_script";
        case chunks::ScriptChunkType::Parent: return "parent";
        case chunks::ScriptChunkType::Unknown: return "unknown";
    }
    return "unknown";
}

chunks::ScriptChunkType castMemberScriptTypeToScriptChunkType(chunks::CastMemberScriptType type) {
    switch (type) {
        case chunks::CastMemberScriptType::Score: return chunks::ScriptChunkType::Score;
        case chunks::CastMemberScriptType::Behavior: return chunks::ScriptChunkType::Behavior;
        case chunks::CastMemberScriptType::MovieScript: return chunks::ScriptChunkType::MovieScript;
        case chunks::CastMemberScriptType::Parent: return chunks::ScriptChunkType::Parent;
        case chunks::CastMemberScriptType::Unknown: return chunks::ScriptChunkType::Unknown;
    }
    return chunks::ScriptChunkType::Unknown;
}

lingo::Datum bitmapPaletteRefDatum(const std::shared_ptr<libreshockwave::cast::CastMember>& member,
                                   id::CastLibId castLibId) {
    if (!member || !member->isBitmap()) {
        return lingo::Datum::voidValue();
    }

    if (member->paletteRefCastLib() >= 1 && member->paletteRefMemberNum() >= 1) {
        return lingo::Datum::castMemberRef(id::CastLibId(member->paletteRefCastLib()),
                                           id::MemberId(member->paletteRefMemberNum()));
    }
    if (const auto& systemName = member->paletteRefSystemName(); systemName.has_value()) {
        return lingo::Datum::symbol(*systemName);
    }

    const auto runtime = member->runtimeBitmap();
    if (runtime) {
        if (runtime->paletteRefCastLib() >= 1 && runtime->paletteRefMemberNum() >= 1) {
            return lingo::Datum::castMemberRef(id::CastLibId(runtime->paletteRefCastLib()),
                                               id::MemberId(runtime->paletteRefMemberNum()));
        }
        const auto& systemName = runtime->paletteRefSystemName();
        if (systemName.has_value()) {
            return lingo::Datum::symbol(*systemName);
        }
    }

    const auto& info = member->bitmapInfo();
    if (info.has_value()) {
        const auto builtInSymbol = bitmap::Palette::builtInSymbolName(info->paletteId);
        if (builtInSymbol.has_value() && info->paletteId != bitmap::Palette::SYSTEM_MAC) {
            return lingo::Datum::symbol(*builtInSymbol);
        }
        const int paletteMemberNumber = info->paletteId + 1;
        if (paletteMemberNumber >= 1) {
            return lingo::Datum::castMemberRef(castLibId, id::MemberId(paletteMemberNumber));
        }
    }

    return lingo::Datum::voidValue();
}

std::string normalizeTextLineEndings(std::string value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char ch = value[index];
        if (ch == '\r') {
            result.push_back('\r');
            if (index + 1 < value.size() && value[index + 1] == '\n') {
                ++index;
            }
        } else if (ch == '\n') {
            result.push_back('\r');
        } else {
            result.push_back(ch);
        }
    }
    return result;
}

std::string pickTextLineDelimiter(std::string_view value) {
    if (value.find("\r\n") != std::string_view::npos) {
        return "\r\n";
    }
    if (value.find('\n') != std::string_view::npos) {
        return "\n";
    }
    if (value.find('\r') != std::string_view::npos) {
        return "\r";
    }
    return "\r\n";
}

std::vector<std::string> splitTextLines(std::string_view value) {
    if (value.empty()) {
        return {""};
    }

    const std::string delimiter = pickTextLineDelimiter(value);
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (true) {
        const std::size_t found = value.find(delimiter, start);
        if (found == std::string_view::npos) {
            lines.emplace_back(value.substr(start));
            break;
        }
        lines.emplace_back(value.substr(start, found - start));
        start = found + delimiter.size();
    }
    return lines;
}

std::string stripHtmlTags(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    bool inTag = false;
    for (const char ch : value) {
        if (ch == '<') {
            inTag = true;
            continue;
        }
        if (ch == '>' && inTag) {
            inTag = false;
            continue;
        }
        if (!inTag) {
            result.push_back(ch);
        }
    }
    return result;
}

int textColorToArgb(const lingo::Datum& value) {
    if (const auto* color = value.asColorRef()) {
        return static_cast<int>(0xFF000000U |
                                ((static_cast<std::uint32_t>(color->r) & 0xFFU) << 16U) |
                                ((static_cast<std::uint32_t>(color->g) & 0xFFU) << 8U) |
                                (static_cast<std::uint32_t>(color->b) & 0xFFU));
    }
    if (value.isString()) {
        std::string text = trim(value.stringValue());
        if (text.size() >= 2) {
            const char first = text.front();
            const char last = text.back();
            if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
                text = trim(text.substr(1, text.size() - 2));
            }
        }
        if (!text.empty() && text.front() == '#') {
            text.erase(text.begin());
        }
        if (text.size() == 6) {
            try {
                const int rgb = std::stoi(text, nullptr, 16);
                return static_cast<int>(0xFF000000U | (static_cast<std::uint32_t>(rgb) & 0x00FFFFFFU));
            } catch (const std::exception&) {
                return static_cast<int>(0xFF000000U);
            }
        }
        return static_cast<int>(0xFF000000U);
    }
    const int raw = value.intValue();
    if (raw > 255) {
        return static_cast<int>(0xFF000000U | (static_cast<std::uint32_t>(raw) & 0x00FFFFFFU));
    }
    const int gray = 255 - std::clamp(raw, 0, 255);
    return static_cast<int>(0xFF000000U |
                            (static_cast<std::uint32_t>(gray) << 16U) |
                            (static_cast<std::uint32_t>(gray) << 8U) |
                            static_cast<std::uint32_t>(gray));
}

lingo::Datum colorDatumFromArgb(int argb) {
    return lingo::Datum::colorRef((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
}

void applyRuntimePaletteMetadata(bitmap::Bitmap& bitmap,
                                 const std::shared_ptr<libreshockwave::cast::CastMember>& member) {
    if (member == nullptr || !member->runtimePaletteOverride()) {
        return;
    }
    bitmap.setImagePalette(member->runtimePaletteOverride());
    if (member->paletteRefCastLib() >= 1 && member->paletteRefMemberNum() >= 1) {
        bitmap.setPaletteRefCastMember(member->paletteRefCastLib(), member->paletteRefMemberNum());
    } else if (member->paletteRefSystemName().has_value()) {
        bitmap.setPaletteRefSystemName(*member->paletteRefSystemName());
    }
}

lingo::Datum paletteColorListDatum(const bitmap::Palette& palette) {
    std::vector<lingo::Datum> colors;
    colors.reserve(static_cast<std::size_t>(palette.size()));
    for (int index = 0; index < palette.size(); ++index) {
        colors.push_back(colorDatumFromArgb(static_cast<int>(palette.getColor(index))));
    }
    return lingo::Datum::list(std::move(colors));
}

std::string fontStyleFromDatum(const lingo::Datum& value) {
    if (!value.isList()) {
        return value.stringValue();
    }
    std::string result;
    for (const auto& item : value.listValue().items()) {
        if (!result.empty()) {
            result += ',';
        }
        result += item.stringValue();
    }
    return result;
}

} // namespace

CastLib::CastLib(int number,
                 std::shared_ptr<chunks::CastChunk> castChunk,
                 const chunks::CastListChunk::CastListEntry* listEntry)
    : castLibId_(number),
      selection_(lingo::Datum::list()),
      castChunk_(std::move(castChunk)) {
    if (listEntry != nullptr) {
        name_ = listEntry->name;
        fileName_ = listEntry->path;
        authoredFileName_ = fileName_;
        preloadMode_ = listEntry->preloadSettings;
    }

    if (name_.empty() && number == 1) {
        name_ = "Internal";
    }
}

void CastLib::setSourceFile(std::shared_ptr<DirectorFile> file) {
    sourceFile_ = std::move(file);
}

std::shared_ptr<DirectorFile> CastLib::sourceFile() const {
    return sourceFile_;
}

void CastLib::reloadFromFile(std::shared_ptr<DirectorFile> file) {
    if (!file) {
        return;
    }
    sourceFile_ = std::move(file);
    state_ = State::None;
    memberChunks_.clear();
    members_.clear();
    invalidateMemberNameIndex();
    scripts_.clear();
    scriptTypesByPointer_.clear();
    scriptTypesById_.clear();
    cachedScripts_.clear();
    totalSlotCount_ = 0;
    load();
}

id::CastLibId CastLib::castLibId() const { return castLibId_; }
int CastLib::number() const { return castLibId_.value(); }
const std::string& CastLib::name() const { return name_; }

void CastLib::setName(std::string name) {
    name_ = std::move(name);
    nameExplicitlyAssigned_ = true;
    nameLoadedFromExternalFile_ = false;
}

const std::string& CastLib::fileName() const { return fileName_; }

void CastLib::setFileName(std::string fileName) {
    if (!sameFileBinding(fileName_, fileName)) {
        invalidateFileBackedBinding();
    }
    fileName_ = std::move(fileName);
    if (!nameExplicitlyAssigned_ && hasAuthoredExternalBinding() && !usesAuthoredExternalBinding()) {
        name_ = fileName_;
        nameLoadedFromExternalFile_ = false;
    }
}

bool CastLib::isExternal() const {
    return !fileName_.empty();
}

bool CastLib::isFetched() const {
    return sourceFile_ != nullptr || !fetchedExternalData_.empty() || !isExternal();
}

bool CastLib::hasAuthoredExternalBinding() const {
    return !authoredFileName_.empty();
}

bool CastLib::matchesAuthoredExternalFile(const std::string& baseName) const {
    if (!hasAuthoredExternalBinding() || baseName.empty()) {
        return false;
    }
    const auto authoredBaseName = util::getFileNameWithoutExtension(util::getFileName(authoredFileName_));
    return equalsIgnoreCase(authoredBaseName, baseName);
}

bool CastLib::usesAuthoredExternalBinding() const {
    if (!hasAuthoredExternalBinding()) {
        return true;
    }
    if (fileName_.empty()) {
        return false;
    }
    const auto currentBaseName = util::getFileNameWithoutExtension(util::getFileName(fileName_));
    return matchesAuthoredExternalFile(currentBaseName);
}

bool CastLib::usesStableRegistryBinding() const {
    if (!hasAuthoredExternalBinding() || usesAuthoredExternalBinding()) {
        return true;
    }
    if (nameLoadedFromExternalFile_) {
        return false;
    }
    const auto runtimeName = trim(name_);
    if (runtimeName.empty() || usesGeneratedPlaceholderName(runtimeName)) {
        return false;
    }
    return !looksLikeDirectFileBindingName(runtimeName);
}

CastLib::State CastLib::state() const { return state_; }
bool CastLib::isLoaded() const { return state_ == State::Loaded; }
bool CastLib::isFetching() const { return state_ == State::Fetching; }

void CastLib::markFetching() {
    if (state_ == State::None) {
        state_ = State::Fetching;
    }
}

int CastLib::preloadMode() const { return preloadMode_; }

void CastLib::setPreloadMode(int preloadMode) {
    preloadMode_ = preloadMode;
}

void CastLib::load() {
    if (state_ == State::Loaded) {
        return;
    }

    state_ = State::Loading;

    if (!sourceFile_) {
        if (!fetchedExternalData_.empty() && setExternalData(fetchedExternalData_)) {
            return;
        }
        state_ = isExternal() ? State::None : State::Loaded;
        return;
    }

    if (isExternal()) {
        if (!sourceFile_->casts().empty()) {
            loadFromExternalFile();
        }
        scanXmedFonts();
        state_ = State::Loaded;
        return;
    }

    if (!castChunk_ && !sourceFile_->casts().empty()) {
        loadFromExternalFile();
        scanXmedFonts();
        state_ = State::Loaded;
        return;
    }

    if (castChunk_) {
        loadMembersFromCast(castChunk_, minMember());
    }

    scanXmedFonts();
    state_ = State::Loaded;
}

int CastLib::memberCount() {
    if (!isLoaded()) {
        load();
    }
    return totalSlotCount_ > 0 ? totalSlotCount_ : static_cast<int>(memberChunks_.size());
}

const std::map<int, std::shared_ptr<chunks::CastMemberChunk>>& CastLib::memberChunks() {
    if (!isLoaded()) {
        load();
    }
    return memberChunks_;
}

std::shared_ptr<chunks::CastMemberChunk> CastLib::findMemberByNumber(int memberNumber) {
    if (!isLoaded()) {
        load();
    }
    if (const auto found = memberChunks_.find(memberNumber); found != memberChunks_.end()) {
        return found->second;
    }
    return nullptr;
}

std::shared_ptr<chunks::CastMemberChunk> CastLib::findMemberByName(const std::string& memberName) {
    if (!isLoaded()) {
        load();
    }
    if (auto direct = findMemberChunkByNameExact(memberName)) {
        return direct;
    }
    if (const auto prefixed = sourcePrefixedLookupName(memberName); prefixed.has_value()) {
        return findMemberChunkByNameExact(*prefixed);
    }
    return nullptr;
}

std::shared_ptr<libreshockwave::cast::CastMember> CastLib::getMember(int memberNumber) {
    if (!isLoaded()) {
        load();
    }
    if (const auto found = members_.find(memberNumber); found != members_.end()) {
        return found->second;
    }
    auto chunk = findMemberByNumber(memberNumber);
    if (!chunk) {
        return nullptr;
    }
    auto member = std::make_shared<libreshockwave::cast::CastMember>(
        chunk->id().value(), castLibId_.value(), memberNumber, chunk);
    members_[memberNumber] = member;
    return member;
}

std::shared_ptr<libreshockwave::cast::CastMember> CastLib::getMemberByName(const std::string& memberName) {
    if (!isLoaded()) {
        load();
    }
    if (auto direct = findMemberByNameExact(memberName)) {
        return direct;
    }
    if (const auto prefixed = sourcePrefixedLookupName(memberName); prefixed.has_value()) {
        return findMemberByNameExact(*prefixed);
    }
    return nullptr;
}

std::shared_ptr<libreshockwave::cast::CastMember> CastLib::getPaletteMemberByName(
    const std::string& memberName) {
    if (!isLoaded()) {
        load();
    }
    if (memberName.empty()) {
        return nullptr;
    }

    auto findExact = [this](const std::string& name) -> std::shared_ptr<libreshockwave::cast::CastMember> {
        for (const auto& [_, member] : members_) {
            if (member && member->isPalette() && equalsIgnoreCase(member->name(), name)) {
                return member;
            }
        }
        for (const auto& [number, chunk] : memberChunks_) {
            if (!chunk || !equalsIgnoreCase(chunk->name(), name)) {
                continue;
            }
            auto member = getMember(number);
            if (member && member->isPalette()) {
                return member;
            }
        }
        return nullptr;
    };

    if (auto direct = findExact(memberName)) {
        return direct;
    }
    if (const auto prefixed = sourcePrefixedLookupName(memberName); prefixed.has_value()) {
        return findExact(*prefixed);
    }
    return nullptr;
}

std::shared_ptr<libreshockwave::cast::CastMember> CastLib::createDynamicMember(const std::string& memberType) {
    if (!isLoaded()) {
        load();
    }
    const auto type = dynamicMemberTypeFor(memberType);
    for (int memberNumber = 10000; memberNumber < nextDynamicMember_; ++memberNumber) {
        if (memberChunks_.contains(memberNumber)) {
            continue;
        }
        if (const auto found = members_.find(memberNumber);
            found != members_.end() && found->second && found->second->isReusableDynamicSlot()) {
            found->second->reuseAs(type);
            invalidateMemberNameIndex();
            return found->second;
        }
    }
    const int memberNumber = nextAvailableDynamicMemberNumber();
    auto member = std::make_shared<libreshockwave::cast::CastMember>(
        castLibId_.value(),
        memberNumber,
        type);
    members_[memberNumber] = member;
    invalidateMemberNameIndex();
    return member;
}

std::shared_ptr<libreshockwave::cast::CastMember> CastLib::createDynamicMember(
    const std::string& memberName,
    const std::string& memberType) {
    auto member = createDynamicMember(memberType);
    if (member) {
        member->setName(memberName);
        invalidateMemberNameIndex();
    }
    return member;
}

bool CastLib::hasMemberNamedExact(const std::string& memberName) {
    return findMemberChunkByNameExact(memberName) != nullptr || findMemberByNameExact(memberName) != nullptr;
}

bool CastLib::hasMemberNumber(int memberNumber) {
    if (!isLoaded()) {
        load();
    }
    if (memberChunks_.contains(memberNumber)) {
        return true;
    }
    const auto found = members_.find(memberNumber);
    return found != members_.end() && found->second != nullptr;
}

int CastLib::getMemberNumber(const std::shared_ptr<chunks::CastMemberChunk>& member) {
    if (!isLoaded()) {
        load();
    }
    for (const auto& [number, existing] : memberChunks_) {
        if (sameAuthoredMember(existing, member)) {
            return number;
        }
    }
    return -1;
}

std::shared_ptr<chunks::ScriptChunk> CastLib::getScript(int memberNumber) {
    if (!isLoaded()) {
        load();
    }
    if (auto member = getMember(memberNumber); member && member->runtimeScript()) {
        return member->runtimeScript();
    }
    if (const auto found = scripts_.find(memberNumber); found != scripts_.end()) {
        return found->second;
    }
    return nullptr;
}

chunks::ScriptChunkType CastLib::scriptTypeForScript(const std::shared_ptr<chunks::ScriptChunk>& script) {
    if (!script) {
        return chunks::ScriptChunkType::Unknown;
    }
    if (!isLoaded()) {
        load();
    }

    if (const auto found = scriptTypesByPointer_.find(script.get()); found != scriptTypesByPointer_.end()) {
        return found->second;
    }
    if (const auto found = scriptTypesById_.find(script->id().value()); found != scriptTypesById_.end()) {
        scriptTypesByPointer_[script.get()] = found->second;
        return found->second;
    }

    for (const auto& [memberNumber, candidate] : scripts_) {
        if (!candidate) {
            continue;
        }
        if (candidate.get() != script.get() && candidate->id().value() != script->id().value()) {
            continue;
        }
        if (const auto found = memberChunks_.find(memberNumber);
            found != memberChunks_.end() && found->second) {
            if (const auto memberType = found->second->getScriptType(); memberType.has_value()) {
                const auto scriptType = castMemberScriptTypeToScriptChunkType(memberType.value());
                scriptTypesByPointer_[script.get()] = scriptType;
                scriptTypesById_[script->id().value()] = scriptType;
                return scriptType;
            }
        }
    }

    const auto scriptType = script->scriptType();
    scriptTypesByPointer_[script.get()] = scriptType;
    scriptTypesById_[script->id().value()] = scriptType;
    return scriptType;
}

const std::vector<std::shared_ptr<chunks::ScriptChunk>>& CastLib::allScripts() {
    if (!isLoaded()) {
        load();
    }
    cachedScripts_.clear();
    if (sourceFile_) {
        cachedScripts_ = sourceFile_->scripts();
    } else {
        for (const auto& [_, script] : scripts_) {
            cachedScripts_.push_back(script);
        }
    }
    return cachedScripts_;
}

std::shared_ptr<chunks::ScriptNamesChunk> CastLib::scriptNames() const {
    return sourceFile_ ? sourceFile_->scriptNames() : nullptr;
}

lingo::Datum CastLib::getProp(const std::string& propName) {
    const auto prop = lower(propName);
    if (prop == "number") return lingo::Datum::of(castLibId_.value());
    if (prop == "name") return stringDatum(name_);
    if (prop == "filename") return stringDatum(fileName_);
    if (prop == "preloadmode") return lingo::Datum::of(preloadMode_);
    if (prop == "selection") return selection_;
    if (prop == "loaded") return isLoaded() ? lingo::Datum::TRUE : lingo::Datum::FALSE;
    if (prop.find("member") != std::string::npos) return lingo::Datum::of(memberCount());
    return lingo::Datum::voidValue();
}

bool CastLib::setProp(const std::string& propName, const lingo::Datum& value) {
    const auto prop = lower(propName);
    if (prop == "name") {
        setName(value.stringValue());
        return true;
    }
    if (prop == "filename") {
        setFileName(value.stringValue());
        return true;
    }
    if (prop == "preloadmode") {
        preloadMode_ = value.intValue();
        return true;
    }
    if (prop == "selection") {
        selection_ = value.isList() ? value : lingo::Datum::list();
        return true;
    }
    return false;
}

lingo::Datum CastLib::getMemberProp(int memberNumber, const std::string& propName) {
    auto member = getMember(memberNumber);
    if (!member) {
        return getInvalidMemberProp(propName);
    }

    const auto prop = lower(propName);
    if (prop == "name") return stringDatum(member->name());
    if (prop == "number") return lingo::Datum::of(id::SlotId::of(castLibId_, id::MemberId(memberNumber)).value());
    if (prop == "membernum") return lingo::Datum::of(memberNumber);
    if (prop == "type") return lingo::Datum::symbol(directorMemberTypeName(member));
    if (prop == "castlibnum") return lingo::Datum::of(castLibId_.value());
    if (prop == "castlib") return lingo::Datum::castLibRef(castLibId_);
    if (prop == "script") {
        return getScript(memberNumber)
            ? lingo::Datum::scriptRef(lingo::Datum::CastMemberRef::of(castLibId_, id::MemberId(memberNumber)))
            : lingo::Datum::voidValue();
    }
    if (prop == "scripttext") return stringDatum("");
    if (prop == "media") return lingo::Datum::castMemberRef(castLibId_, id::MemberId(memberNumber));
    if (prop == "mediaready") return lingo::Datum::of(1);
    if (member->isSound() && prop == "duration") {
        if (sourceFile_ != nullptr && member->rawChunk()) {
            if (const auto sound = player::audio::SoundManager::findSoundForMember(*sourceFile_, member->rawChunk())) {
                return lingo::Datum::of(static_cast<int>(std::lround(sound->durationSeconds() * 1000.0)));
            }
        }
        return lingo::Datum::of(0);
    }
    if (member->isTextLike()) {
        if (prop == "text") return stringDatum(resolveMemberText(member));
        if (prop == "linecount") {
            const auto lines = splitTextLines(resolveMemberText(member));
            return lingo::Datum::of(static_cast<int>(lines.size()));
        }
        if (prop == "line") {
            std::vector<lingo::Datum> lines;
            const auto textLines = splitTextLines(resolveMemberText(member));
            for (const auto& line : textLines) {
                lines.push_back(stringDatum(line));
            }
            return lingo::Datum::list(std::move(lines));
        }
        if (prop == "width") return lingo::Datum::of(member->textRectRight() - member->textRectLeft());
        if (prop == "height") return lingo::Datum::of(member->textRectBottom() - member->textRectTop());
        if (prop == "rect") {
            return lingo::Datum::intRect(member->textRectLeft(),
                                         member->textRectTop(),
                                         member->textRectRight(),
                                         member->textRectBottom());
        }
        if (prop == "font") return stringDatum(member->textFont());
        if (prop == "fontsize") return lingo::Datum::of(member->textFontSize());
        if (prop == "fontstyle") return stringDatum(member->textFontStyle());
        if (prop == "alignment") return lingo::Datum::symbol(member->textAlignment());
        if (prop == "color") return colorDatumFromArgb(member->textColor());
        if (prop == "bgcolor") return colorDatumFromArgb(member->textBgColor());
        if (prop == "wordwrap") return lingo::Datum::of(member->textWordWrap() ? 1 : 0);
        if (prop == "antialias") return lingo::Datum::of(member->textAntialias() ? 1 : 0);
        if (prop == "boxtype") return lingo::Datum::of(member->textBoxType());
        if (prop == "fixedlinespace") return lingo::Datum::of(member->textFixedLineSpace());
        if (prop == "topspacing") return lingo::Datum::of(member->textTopSpacing());
        if (prop == "editable") return lingo::Datum::of(member->editable() ? 1 : 0);
    }
    if (member->isPalette() && prop == "color") {
        auto palette = member->paletteData();
        if (!palette && sourceFile_) {
            palette = sourceFile_->resolvePaletteByMemberNumber(memberNumber);
        }
        return palette ? paletteColorListDatum(*palette) : lingo::Datum::voidValue();
    }
    if (member->isBitmap() && (prop == "paletteref" || prop == "palette")) {
        return bitmapPaletteRefDatum(member, castLibId_);
    }
    if (member->isShape()) {
        if (prop == "filled") return lingo::Datum::of(member->shapeFilled() ? 1 : 0);
        if (prop == "shapetype") return lingo::Datum::symbol(shapeTypeSymbolName(member->shapeType()));
        if (prop == "linesize") return lingo::Datum::of(member->shapeLineSize());
        if (prop == "pattern") return lingo::Datum::of(member->shapePattern());
    }
    if (member->isScript()) {
        if (prop == "text") return stringDatum("");
        if (prop == "scripttype") {
            auto script = getScript(memberNumber);
            return script ? stringDatum(scriptChunkTypePropName(script->scriptType())) : lingo::Datum::voidValue();
        }
    }
    if (prop == "width") return lingo::Datum::of(member->width());
    if (prop == "height") return lingo::Datum::of(member->height());
    if (prop == "depth") {
        auto bitmap = member->runtimeBitmap();
        if (bitmap) {
            return lingo::Datum::of(bitmap->bitDepth());
        }
        return lingo::Datum::of(member->bitmapInfo().has_value() ? member->bitmapInfo()->bitDepth : 0);
    }
    if (member->isBitmap() && prop == "alphathreshold") {
        return lingo::Datum::of(member->bitmapAlphaThreshold());
    }
    if (prop == "regpoint") return lingo::Datum::intPoint(member->regX(), member->regY());
    if (prop == "rect") return lingo::Datum::intRect(0, 0, member->width(), member->height());
    if (prop == "image") {
        auto bitmap = member->runtimeBitmap();
        if ((!bitmap || member->shouldRedecodeAuthoredRuntimeBitmap()) &&
            member->isBitmap() &&
            member->rawChunk() &&
            sourceFile_) {
            const bool hadRuntimeBitmap = bitmap != nullptr;
            const auto paletteOverride = member->runtimePaletteOverride();
            auto decoded = sourceFile_->decodeBitmap(member->rawChunk(), paletteOverride.get());
            if (decoded.has_value()) {
                applyRuntimePaletteMetadata(*decoded, member);
                member->setRuntimeBitmapFromAuthoredSource(*decoded);
                bitmap = member->runtimeBitmap();
            } else if (!hadRuntimeBitmap &&
                       member->bitmapInfo().has_value() &&
                       member->bitmapInfo()->width > 0 &&
                       member->bitmapInfo()->height > 0) {
                bitmap::Bitmap placeholder(member->bitmapInfo()->width,
                                           member->bitmapInfo()->height,
                                           std::max(member->bitmapInfo()->bitDepth, 32));
                placeholder.setNativeAlpha(true);
                member->setRuntimeBitmapFromAuthoredSource(placeholder);
                bitmap = member->runtimeBitmap();
            }
        }
        if (!bitmap && member->isBitmap() && member->isRuntimeDynamic()) {
            bitmap::Bitmap defaultBitmap(1, 1, 32);
            defaultBitmap.fill(0xFFFFFFFFU);
            member->setRuntimeBitmap(defaultBitmap, false);
            bitmap = member->runtimeBitmap();
        }
        if (!bitmap) {
            return lingo::Datum::voidValue();
        }
        std::weak_ptr<libreshockwave::cast::CastMember> weakMember(member);
        return lingo::Datum::imageRef(
            std::move(bitmap),
            [weakMember](bitmap::Bitmap&) {
                if (auto locked = weakMember.lock()) {
                    locked->syncRuntimeBitmapAnchorState();
                }
            });
    }
    return lingo::Datum::voidValue();
}

bool CastLib::setMemberProp(int memberNumber, const std::string& propName, const lingo::Datum& value) {
    auto member = getMember(memberNumber);
    if (!member) {
        return false;
    }

    const auto prop = lower(propName);
    if (prop == "name") {
        member->setName(value.stringValue());
        invalidateMemberNameIndex();
        return true;
    }
    if (prop == "regpoint") {
        const auto* point = value.asIntPoint();
        if (point == nullptr) {
            return false;
        }
        member->setRegPoint(point->x, point->y);
        return true;
    }
    if (member->isTextLike()) {
        if (prop == "text") {
            member->setDynamicText(value.stringValue());
            return true;
        }
        if (prop == "html") {
            member->setDynamicText(stripHtmlTags(value.stringValue()));
            return true;
        }
        if (prop == "media" && (value.isString() || value.isSymbol())) {
            member->setDynamicText(value.stringValue());
            return true;
        }
        if (prop == "image") {
            const auto* image = value.asImageRef();
            if (image == nullptr || image->bitmap == nullptr) {
                return false;
            }
            member->setRuntimeBitmap(*image->bitmap);
            return true;
        }
        if (prop == "font") {
            member->setTextFont(value.stringValue());
            return true;
        }
        if (prop == "fontsize") {
            member->setTextFontSize(value.intValue());
            return true;
        }
        if (prop == "fontstyle") {
            member->setTextFontStyle(fontStyleFromDatum(value));
            return true;
        }
        if (prop == "alignment") {
            member->setTextAlignment(lower(value.stringValue()));
            return true;
        }
        if (prop == "color") {
            if (!value.isVoid()) {
                member->setTextColor(textColorToArgb(value));
            }
            return true;
        }
        if (prop == "bgcolor") {
            if (!value.isVoid()) {
                member->setTextBgColor(textColorToArgb(value));
            }
            return true;
        }
        if (prop == "wordwrap") {
            member->setTextWordWrap(value.intValue() != 0);
            return true;
        }
        if (prop == "antialias") {
            member->setTextAntialias(value.intValue() != 0);
            return true;
        }
        if (prop == "boxtype") {
            member->setTextBoxType(textBoxTypeFromDatum(value));
            return true;
        }
        if (prop == "rect") {
            const auto* rect = value.asIntRect();
            if (rect == nullptr) {
                return false;
            }
            member->setTextRect(rect->left, rect->top, rect->right, rect->bottom);
            return true;
        }
        if (prop == "width") {
            member->setTextWidth(value.intValue());
            return true;
        }
        if (prop == "height") {
            member->setTextHeight(value.intValue());
            return true;
        }
        if (prop == "fixedlinespace") {
            member->setTextFixedLineSpace(value.intValue());
            return true;
        }
        if (prop == "topspacing") {
            member->setTextTopSpacing(value.intValue());
            return true;
        }
        if (prop == "editable") {
            member->setEditable(value.intValue() != 0);
            return true;
        }
    }
    if (member->isBitmap() && prop == "alphathreshold") {
        member->setBitmapAlphaThreshold(value.intValue());
        return true;
    }
    if (member->isShape()) {
        if (prop == "filled") {
            member->setShapeFilled(value.boolValue());
            return true;
        }
        if (prop == "shapetype") {
            const auto type = shapeTypeFromDatum(value);
            if (!type.has_value()) {
                return false;
            }
            member->setShapeType(*type);
            return true;
        }
        if (prop == "linesize") {
            member->setShapeLineSize(value.intValue());
            return true;
        }
        if (prop == "pattern") {
            member->setShapePattern(value.intValue());
            return true;
        }
        if (prop == "width") {
            member->setShapeWidth(value.intValue());
            return true;
        }
        if (prop == "height") {
            member->setShapeHeight(value.intValue());
            return true;
        }
        if (prop == "rect") {
            const auto* rect = value.asIntRect();
            if (rect == nullptr) {
                return false;
            }
            member->setShapeSize(rect->width(), rect->height());
            return true;
        }
    }
    if (member->isBitmap() && prop == "width") {
        const int newWidth = value.intValue();
        if (newWidth > 0) {
            auto current = member->runtimeBitmap();
            const int height = current ? current->height() : 1;
            const int depth = current ? current->bitDepth() : 32;
            bitmap::Bitmap resized(newWidth, height, depth);
            resized.fill(0xFFFFFFFFU);
            resized.setAnchorPoint(member->regX(), member->regY());
            member->setRuntimeBitmap(resized);
        }
        return true;
    }
    if (member->isBitmap() && prop == "height") {
        const int newHeight = value.intValue();
        if (newHeight > 0) {
            auto current = member->runtimeBitmap();
            const int width = current ? current->width() : 1;
            const int depth = current ? current->bitDepth() : 32;
            bitmap::Bitmap resized(width, newHeight, depth);
            resized.fill(0xFFFFFFFFU);
            resized.setAnchorPoint(member->regX(), member->regY());
            member->setRuntimeBitmap(resized);
        }
        return true;
    }
    if (prop == "image") {
        const auto* image = value.asImageRef();
        if (image == nullptr || image->bitmap == nullptr) {
            return false;
        }
        member->setRuntimeBitmap(*image->bitmap);
        return true;
    }
    if (prop == "media" && member->isBitmap()) {
        const auto* image = value.asImageRef();
        if (image == nullptr || image->bitmap == nullptr) {
            return false;
        }
        member->setRuntimeBitmap(*image->bitmap);
        return true;
    }
    return false;
}

bool CastLib::setExternalData(const std::vector<std::uint8_t>& data) {
    if (data.empty()) {
        return false;
    }

    try {
        fetchedExternalData_ = data;
        auto file = DirectorFile::load(data);
        if (!file) {
            return false;
        }

        sourceFile_ = std::move(file);
        const auto nameBeforeLoad = name_;
        if (auto externalCastList = sourceFile_->castList();
            externalCastList && !externalCastList->entries().empty()) {
            const auto& internalName = externalCastList->entries().front().name;
            if (!internalName.empty() && !nameExplicitlyAssigned_) {
                name_ = internalName;
                nameLoadedFromExternalFile_ = !usesAuthoredExternalBinding();
            }
        }
        if (name_.empty()) {
            name_ = nameBeforeLoad;
            nameLoadedFromExternalFile_ = false;
        }

        state_ = State::None;
        memberChunks_.clear();
        members_.clear();
        invalidateMemberNameIndex();
        scripts_.clear();
        scriptTypesByPointer_.clear();
        scriptTypesById_.clear();
        cachedScripts_.clear();
        totalSlotCount_ = 0;
        load();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void CastLib::cacheFetchedExternalData(const std::vector<std::uint8_t>& data) {
    if (data.empty()) {
        return;
    }
    fetchedExternalData_ = data;
    const auto dataString = std::string(data.begin(), data.end());
    if (dataString.find("font") != std::string::npos || dataString.find("FONT") != std::string::npos) {
        try {
            registerFontAliases(DirectorFile::load(data));
        } catch (const std::exception&) {
        }
    }
}

lingo::Datum CastLib::getInvalidMemberProp(const std::string& propName) {
    const auto prop = lower(propName);
    if (prop == "name") return stringDatum("");
    if (prop == "number" || prop == "membernum") return lingo::Datum::of(0);
    if (prop == "type") return lingo::Datum::symbol("empty");
    return lingo::Datum::voidValue();
}

void CastLib::registerFontAliases(const std::shared_ptr<DirectorFile>& file) {
    if (!file) {
        return;
    }
    for (const auto& member : file->castMembers()) {
        if (!member) {
            continue;
        }
        if (const auto aliasInfo = parseFontAlias(member->specificData(), member->name()); aliasInfo.has_value()) {
            FontRegistry::registerFontAlias(aliasInfo->alias, aliasInfo->fontName, aliasInfo->bold);
        }
    }
}

int CastLib::minMember() const {
    if (!sourceFile_) {
        return 1;
    }
    if (auto castList = sourceFile_->castList();
        castList && castLibId_.value() - 1 < static_cast<int>(castList->entries().size())) {
        const int value = castList->entries()[static_cast<std::size_t>(castLibId_.value() - 1)].minMember;
        return value > 0 ? value : 1;
    }
    if (auto config = sourceFile_->config()) {
        const int value = config->minMember();
        return value > 0 ? value : 1;
    }
    return 1;
}

void CastLib::cacheScriptTypeForMember(int memberNumber, const std::shared_ptr<chunks::ScriptChunk>& script) {
    if (!script) {
        return;
    }
    auto scriptType = script->scriptType();
    if (const auto found = memberChunks_.find(memberNumber);
        found != memberChunks_.end() && found->second) {
        if (const auto memberType = found->second->getScriptType(); memberType.has_value()) {
            scriptType = castMemberScriptTypeToScriptChunkType(memberType.value());
        }
    }

    scriptTypesByPointer_[script.get()] = scriptType;
    scriptTypesById_[script->id().value()] = scriptType;
}

void CastLib::loadMembersFromCast(const std::shared_ptr<chunks::CastChunk>& cast, int minMemberValue) {
    if (!cast || !sourceFile_) {
        return;
    }

    totalSlotCount_ = static_cast<int>(cast->memberIds().size()) + minMemberValue - 1;
    for (int index = 0; index < static_cast<int>(cast->memberIds().size()); ++index) {
        const int chunkId = cast->memberIds()[static_cast<std::size_t>(index)];
        if (chunkId <= 0) {
            continue;
        }

        const int memberNumber = index + minMemberValue;
        for (const auto& member : sourceFile_->castMembers()) {
            if (!member || member->id().value() != chunkId) {
                continue;
            }
            memberChunks_[memberNumber] = member;
            if (member->isScript() && member->scriptId() > 0) {
                if (auto script = sourceFile_->getScriptForCastMember(member, cast)) {
                    scripts_[memberNumber] = script;
                    cacheScriptTypeForMember(memberNumber, script);
                }
            }
            break;
        }
    }
}

void CastLib::loadFromExternalFile() {
    if (!sourceFile_ || sourceFile_->casts().empty()) {
        return;
    }

    int minMemberValue = 1;
    if (auto externalCastList = sourceFile_->castList();
        externalCastList && !externalCastList->entries().empty()) {
        minMemberValue = externalCastList->entries().front().minMember;
    } else if (auto config = sourceFile_->config()) {
        minMemberValue = config->minMember();
    }
    if (minMemberValue <= 0) {
        minMemberValue = 1;
    }

    loadMembersFromCast(sourceFile_->casts().front(), minMemberValue);
}

void CastLib::scanXmedFonts() {
    if (!sourceFile_) {
        return;
    }

    registerFontAliases(sourceFile_);
    const auto keyTable = sourceFile_->keyTable();
    if (!keyTable) {
        return;
    }

    const auto xmedFourCC = io::BinaryReader::fourCC("XMED");
    for (const auto& member : sourceFile_->castMembers()) {
        if (!member) {
            continue;
        }
        for (const auto& chunk : sourceFile_->getLinkedChunksForMember(member, xmedFourCC)) {
            auto raw = std::dynamic_pointer_cast<chunks::RawChunk>(chunk);
            if (!raw || raw->data().size() < 4) {
                continue;
            }
            const auto& data = raw->data();
            if (data[0] == static_cast<std::uint8_t>('P') &&
                data[1] == static_cast<std::uint8_t>('F') &&
                data[2] == static_cast<std::uint8_t>('R') &&
                data[3] == static_cast<std::uint8_t>('1') &&
                !member->name().empty()) {
                FontRegistry::registerPfr1Font(member->name(), data);
            }
        }
    }
}

void CastLib::invalidateFileBackedBinding() {
    sourceFile_.reset();
    fetchedExternalData_.clear();
    state_ = State::None;
    totalSlotCount_ = 0;
    nameLoadedFromExternalFile_ = false;
    memberChunks_.clear();
    members_.clear();
    invalidateMemberNameIndex();
    scripts_.clear();
    scriptTypesByPointer_.clear();
    scriptTypesById_.clear();
    cachedScripts_.clear();
}

bool CastLib::sameFileBinding(const std::string& currentFileName, const std::string& newFileName) const {
    return currentFileName == newFileName || (currentFileName.empty() && newFileName.empty());
}

bool CastLib::usesGeneratedPlaceholderName(const std::string& candidateName) const {
    if (candidateName.empty()) {
        return true;
    }
    const auto authoredBaseName = util::getFileNameWithoutExtension(util::getFileName(authoredFileName_));
    if (authoredBaseName.empty()) {
        return false;
    }

    const auto normalizedName = lower(trim(candidateName));
    const auto normalizedBase = lower(trim(authoredBaseName));
    if (normalizedName == normalizedBase) {
        return true;
    }
    const auto prefix = normalizedBase + " ";
    if (!startsWithIgnoreCase(normalizedName, prefix)) {
        return false;
    }
    const auto suffix = normalizedName.substr(prefix.size());
    return !suffix.empty() && std::all_of(suffix.begin(), suffix.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

bool CastLib::looksLikeDirectFileBindingName(const std::string& candidateName) const {
    const auto normalizedName = lower(trim(candidateName));
    if (normalizedName.empty()) {
        return true;
    }
    if (normalizedName.find("://") != std::string::npos ||
        normalizedName.find('/') != std::string::npos ||
        normalizedName.find('\\') != std::string::npos ||
        normalizedName.find('?') != std::string::npos ||
        normalizedName.find('#') != std::string::npos) {
        return true;
    }
    if (endsWith(normalizedName, ".cct") ||
        endsWith(normalizedName, ".cst") ||
        endsWith(normalizedName, ".dcr") ||
        endsWith(normalizedName, ".dir")) {
        return true;
    }
    return !fileName_.empty() && normalizedName == lower(trim(fileName_));
}

std::string CastLib::resolveMemberText(const std::shared_ptr<libreshockwave::cast::CastMember>& member) {
    if (!member || !member->isTextLike()) {
        return "";
    }
    if (member->hasDynamicText()) {
        return member->textContent();
    }
    if (sourceFile_ && member->rawChunk()) {
        if (auto styledText = sourceFile_->getXmedStyledTextForMember(member->rawChunk())) {
            return normalizeTextLineEndings(styledText->text);
        }
        if (auto text = sourceFile_->getTextForMember(member->rawChunk())) {
            return normalizeTextLineEndings(text->text());
        }
    }
    return "";
}

int CastLib::nextAvailableDynamicMemberNumber() {
    while (memberChunks_.contains(nextDynamicMember_) || members_.contains(nextDynamicMember_)) {
        ++nextDynamicMember_;
    }
    return nextDynamicMember_++;
}

std::shared_ptr<chunks::CastMemberChunk> CastLib::findMemberChunkByNameExact(const std::string& memberName) {
    if (memberName.empty()) {
        return nullptr;
    }
    rebuildMemberNameIndex();
    const auto foundIndex = memberChunkNameIndex_.find(lower(memberName));
    if (foundIndex == memberChunkNameIndex_.end()) {
        return nullptr;
    }
    const auto foundMember = memberChunks_.find(foundIndex->second);
    return foundMember == memberChunks_.end() ? nullptr : foundMember->second;
}

std::shared_ptr<libreshockwave::cast::CastMember> CastLib::findMemberByNameExact(const std::string& memberName) {
    if (memberName.empty()) {
        return nullptr;
    }
    rebuildMemberNameIndex();
    const auto key = lower(memberName);
    const auto foundChunkIndex = memberChunkNameIndex_.find(key);
    if (foundChunkIndex != memberChunkNameIndex_.end()) {
        return getMember(foundChunkIndex->second);
    }
    const auto foundRuntimeIndex = runtimeMemberNameIndex_.find(key);
    if (foundRuntimeIndex == runtimeMemberNameIndex_.end()) {
        return nullptr;
    }
    const auto foundMember = members_.find(foundRuntimeIndex->second);
    return foundMember == members_.end() ? nullptr : foundMember->second;
}

void CastLib::invalidateMemberNameIndex() const {
    memberNameIndexDirty_ = true;
}

void CastLib::rebuildMemberNameIndex() const {
    if (!memberNameIndexDirty_) {
        return;
    }

    memberChunkNameIndex_.clear();
    runtimeMemberNameIndex_.clear();
    memberChunkNameIndex_.reserve(memberChunks_.size());
    runtimeMemberNameIndex_.reserve(members_.size());
    for (const auto& [number, member] : memberChunks_) {
        if (member && !member->name().empty()) {
            memberChunkNameIndex_.try_emplace(lower(member->name()), number);
        }
    }
    for (const auto& [number, member] : members_) {
        if (member && !member->name().empty()) {
            runtimeMemberNameIndex_.try_emplace(lower(member->name()), number);
        }
    }
    memberNameIndexDirty_ = false;
}

std::optional<std::string> CastLib::sourcePrefixedLookupName(const std::string& requestedName) {
    if (requestedName.empty() || startsWithIgnoreCase(requestedName, "s_")) {
        return std::nullopt;
    }
    return "s_" + requestedName;
}

libreshockwave::cast::MemberType CastLib::dynamicMemberTypeFor(const std::string& typeName) {
    const auto type = lower(typeName);
    if (type == "field" || type == "text") return libreshockwave::cast::MemberType::Text;
    if (type == "bitmap") return libreshockwave::cast::MemberType::Bitmap;
    if (type == "palette") return libreshockwave::cast::MemberType::Palette;
    if (type == "script") return libreshockwave::cast::MemberType::Script;
    if (type == "button") return libreshockwave::cast::MemberType::Button;
    if (type == "shape") return libreshockwave::cast::MemberType::Shape;
    if (type == "sound") return libreshockwave::cast::MemberType::Sound;
    return libreshockwave::cast::MemberType::Text;
}

bool CastLib::sameAuthoredMember(const std::shared_ptr<chunks::CastMemberChunk>& left,
                                 const std::shared_ptr<chunks::CastMemberChunk>& right) {
    if (left == right) {
        return true;
    }
    return left && right && left->file() == right->file() && left->id().value() == right->id().value();
}

std::optional<CastLib::FontAliasInfo> CastLib::parseFontAlias(const std::vector<std::uint8_t>& data,
                                                              const std::string& memberName) {
    if (data.size() < 12) {
        return std::nullopt;
    }

    const auto strings = extractPrintableNullTerminatedStrings(data);
    if (strings.size() < 2 || !equalsIgnoreCase(strings.front(), "font")) {
        return std::nullopt;
    }

    std::string fontName;
    for (auto it = strings.rbegin(); it != strings.rend(); ++it) {
        if (!equalsIgnoreCase(*it, "font")) {
            fontName = *it;
            break;
        }
    }
    if (trim(fontName).empty()) {
        return std::nullopt;
    }

    std::string alias = memberName;
    if (trim(alias).empty() && strings.size() >= 3) {
        alias = strings[strings.size() - 2];
    }
    if (trim(alias).empty() || equalsIgnoreCase(alias, "fontName")) {
        return std::nullopt;
    }

    const bool bold = data.size() > 23 && (data[23] & 0x01U) != 0U;
    return FontAliasInfo{alias, fontName, bold};
}

std::vector<std::string> CastLib::extractPrintableNullTerminatedStrings(const std::vector<std::uint8_t>& data) {
    std::vector<std::string> strings;
    int start = -1;
    for (int index = 0; index <= static_cast<int>(data.size()); ++index) {
        const int value = index < static_cast<int>(data.size()) ? static_cast<int>(data[static_cast<std::size_t>(index)]) : 0;
        const bool printable = value >= 0x20 && value <= 0x7E;
        if (printable) {
            if (start < 0) {
                start = index;
            }
        } else if (start >= 0) {
            if (value == 0 && index > start) {
                strings.emplace_back(data.begin() + start, data.begin() + index);
            }
            start = -1;
        }
    }
    return strings;
}

} // namespace libreshockwave::player::cast
