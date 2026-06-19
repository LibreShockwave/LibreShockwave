#include "libreshockwave/cast/CastMember.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/bitmap/Palette.hpp"
#include "libreshockwave/chunks/CastMemberChunk.hpp"

namespace libreshockwave::cast {

CastMember::CastMember(int id, int castLib, int memberNum, std::shared_ptr<chunks::CastMemberChunk> chunk)
    : id_(id),
      castLib_(castLib),
      memberNum_(memberNum),
      memberType_(chunk ? chunk->memberType() : MemberType::Unknown),
      name_(chunk ? chunk->name() : ""),
      scriptId_(chunk ? chunk->scriptId() : 0),
      rawChunk_(std::move(chunk)) {
    parseSpecificData();
}

CastMember::CastMember(int castLib, int memberNum, MemberType memberType)
    : id_(0),
      castLib_(castLib),
      memberNum_(memberNum),
      memberType_(memberType),
      scriptId_(0) {
    regPointPinnedToMember_ = false;
}

int CastMember::id() const { return id_; }
int CastMember::castLib() const { return castLib_; }
int CastMember::memberNum() const { return memberNum_; }
MemberType CastMember::memberType() const { return memberType_; }
const std::string& CastMember::name() const { return name_; }
void CastMember::setName(std::string name) { name_ = std::move(name); }
int CastMember::scriptId() const { return scriptId_; }
const std::shared_ptr<chunks::CastMemberChunk>& CastMember::rawChunk() const { return rawChunk_; }

const std::optional<BitmapInfo>& CastMember::bitmapInfo() const { return bitmapInfo_; }
const std::optional<ShapeInfo>& CastMember::shapeInfo() const { return shapeInfo_; }
const std::optional<FilmLoopInfo>& CastMember::filmLoopInfo() const { return filmLoopInfo_; }
const std::optional<ScriptType>& CastMember::scriptType() const { return scriptType_; }
const std::optional<Shockwave3DInfo>& CastMember::shockwave3DInfo() const { return shockwave3DInfo_; }
std::shared_ptr<chunks::ScriptChunk> CastMember::runtimeScript() const { return runtimeScript_; }
void CastMember::setRuntimeScript(std::shared_ptr<chunks::ScriptChunk> script) {
    runtimeScript_ = std::move(script);
}

bool CastMember::isBitmap() const { return memberType_ == MemberType::Bitmap; }
bool CastMember::isText() const { return memberType_ == MemberType::Text; }
bool CastMember::isTextLike() const {
    return memberType_ == MemberType::Text ||
           memberType_ == MemberType::RichText ||
           memberType_ == MemberType::Button ||
           (memberType_ == MemberType::Xtra && rawChunk_ != nullptr && rawChunk_->isTextXtra());
}
bool CastMember::isSound() const { return memberType_ == MemberType::Sound; }
bool CastMember::isScript() const { return memberType_ == MemberType::Script; }
bool CastMember::isShape() const { return memberType_ == MemberType::Shape; }
bool CastMember::isFilmLoop() const { return memberType_ == MemberType::FilmLoop; }
bool CastMember::isPalette() const { return memberType_ == MemberType::Palette; }
bool CastMember::isFont() const { return memberType_ == MemberType::Font; }
bool CastMember::isShockwave3D() const { return memberType_ == MemberType::Shockwave3D; }
bool CastMember::isRuntimeDynamic() const { return rawChunk_ == nullptr; }
bool CastMember::isReusableDynamicSlot() const {
    return isRuntimeDynamic() && memberType_ == MemberType::Null && name_.empty();
}

int CastMember::width() const {
    if (runtimeBitmap_) return runtimeBitmap_->width();
    if (bitmapInfo_) return bitmapInfo_->width;
    if (shapeInfo_) return shapeInfo_->width;
    if (filmLoopInfo_) return filmLoopInfo_->width();
    return 0;
}

int CastMember::height() const {
    if (runtimeBitmap_) return runtimeBitmap_->height();
    if (bitmapInfo_) return bitmapInfo_->height;
    if (shapeInfo_) return shapeInfo_->height;
    if (filmLoopInfo_) return filmLoopInfo_->height();
    return 0;
}

int CastMember::regX() const {
    if (runtimeRegX_.has_value()) return *runtimeRegX_;
    if (bitmapInfo_) return bitmapInfo_->regX;
    if (shapeInfo_) return shapeInfo_->regX;
    if (filmLoopInfo_) return filmLoopInfo_->regX();
    return 0;
}

int CastMember::regY() const {
    if (runtimeRegY_.has_value()) return *runtimeRegY_;
    if (bitmapInfo_) return bitmapInfo_->regY;
    if (shapeInfo_) return shapeInfo_->regY;
    if (filmLoopInfo_) return filmLoopInfo_->regY();
    return 0;
}

int CastMember::bitmapAlphaThreshold() const {
    return bitmapAlphaThreshold_;
}

void CastMember::setBitmapAlphaThreshold(int alphaThreshold) {
    bitmapAlphaThreshold_ = std::clamp(alphaThreshold, 0, 255);
}

void CastMember::setRegPoint(int x, int y) {
    setRegPointState(x, y, true);
}

bool CastMember::regPointPinnedToMember() const {
    return regPointPinnedToMember_;
}

void CastMember::setRegPointState(int x, int y, bool pinnedToMember) {
    runtimeRegX_ = x;
    runtimeRegY_ = y;
    regPointPinnedToMember_ = pinnedToMember;
    syncRuntimeBitmapAnchorState();
}

bool CastMember::shapeFilled() const {
    if (!isShape()) {
        return false;
    }
    return !shapeInfo_.has_value() || shapeInfo_->isFilled();
}

void CastMember::setShapeFilled(bool filled) {
    if (!isShape()) {
        return;
    }
    ensureRuntimeShapeInfo();
    shapeInfo_->fillType = filled ? 1 : 0;
}

ShapeType CastMember::shapeType() const {
    if (!isShape()) {
        return ShapeType::Unknown;
    }
    return shapeInfo_.has_value() ? shapeInfo_->shapeType : ShapeType::Rect;
}

void CastMember::setShapeType(ShapeType type) {
    if (!isShape()) {
        return;
    }
    ensureRuntimeShapeInfo();
    shapeInfo_->shapeType = type == ShapeType::Unknown ? ShapeType::Rect : type;
}

int CastMember::shapeLineSize() const {
    if (runtimeShapeLineSize_.has_value()) {
        return *runtimeShapeLineSize_;
    }
    if (shapeInfo_.has_value()) {
        return std::max(0, shapeInfo_->lineThickness);
    }
    return isShape() ? 1 : 0;
}

bool CastMember::hasRuntimeShapeLineSize() const {
    return runtimeShapeLineSize_.has_value();
}

void CastMember::setShapeLineSize(int lineSize) {
    if (!isShape()) {
        return;
    }
    runtimeShapeLineSize_ = std::max(0, lineSize);
}

int CastMember::shapePattern() const {
    return isShape() ? shapePattern_ : 1;
}

void CastMember::setShapePattern(int pattern) {
    if (!isShape()) {
        return;
    }
    shapePattern_ = std::max(1, pattern);
}

void CastMember::setShapeWidth(int width) {
    if (!isShape()) {
        return;
    }
    ensureRuntimeShapeInfo();
    shapeInfo_->width = std::max(0, width);
}

void CastMember::setShapeHeight(int height) {
    if (!isShape()) {
        return;
    }
    ensureRuntimeShapeInfo();
    shapeInfo_->height = std::max(0, height);
}

void CastMember::setShapeSize(int width, int height) {
    if (!isShape()) {
        return;
    }
    ensureRuntimeShapeInfo();
    shapeInfo_->width = std::max(0, width);
    shapeInfo_->height = std::max(0, height);
}

std::shared_ptr<bitmap::Bitmap> CastMember::runtimeBitmap() const {
    return runtimeBitmap_;
}

bool CastMember::hasDynamicText() const {
    return dynamicText_.has_value();
}

std::string CastMember::textContent() const {
    return dynamicText_.value_or("");
}

void CastMember::setDynamicText(std::string text) {
    dynamicText_ = std::move(text);
    ++textRevision_;
}

std::uint64_t CastMember::textRevision() const { return textRevision_; }

const std::string& CastMember::textFont() const { return textFont_; }
void CastMember::setTextFont(std::string font) { textFont_ = std::move(font); }
int CastMember::textFontSize() const { return textFontSize_; }
void CastMember::setTextFontSize(int size) { textFontSize_ = size; }
const std::string& CastMember::textFontStyle() const { return textFontStyle_; }
void CastMember::setTextFontStyle(std::string style) { textFontStyle_ = std::move(style); }
const std::string& CastMember::textAlignment() const { return textAlignment_; }
void CastMember::setTextAlignment(std::string alignment) { textAlignment_ = std::move(alignment); }
int CastMember::textColor() const { return textColor_; }
void CastMember::setTextColor(int argb) { textColor_ = argb; }
int CastMember::textBgColor() const { return textBgColor_; }
void CastMember::setTextBgColor(int argb) { textBgColor_ = argb; }
bool CastMember::textWordWrap() const { return textWordWrap_; }
void CastMember::setTextWordWrap(bool wordWrap) { textWordWrap_ = wordWrap; }
bool CastMember::textAntialias() const { return textAntialias_; }
void CastMember::setTextAntialias(bool antialias) { textAntialias_ = antialias; }
int CastMember::textBoxType() const { return textBoxType_; }
void CastMember::setTextBoxType(int boxType) { textBoxType_ = boxType; }
int CastMember::textRectLeft() const { return textRectLeft_; }
int CastMember::textRectTop() const { return textRectTop_; }
int CastMember::textRectRight() const { return textRectRight_; }
int CastMember::textRectBottom() const { return textRectBottom_; }
void CastMember::setTextRect(int left, int top, int right, int bottom) {
    textRectLeft_ = left;
    textRectTop_ = top;
    textRectRight_ = right;
    textRectBottom_ = bottom;
}
void CastMember::setTextWidth(int width) { textRectRight_ = textRectLeft_ + width; }
void CastMember::setTextHeight(int height) { textRectBottom_ = textRectTop_ + height; }
int CastMember::textFixedLineSpace() const { return textFixedLineSpace_; }
void CastMember::setTextFixedLineSpace(int lineSpace) { textFixedLineSpace_ = lineSpace; }
int CastMember::textTopSpacing() const { return textTopSpacing_; }
void CastMember::setTextTopSpacing(int spacing) { textTopSpacing_ = spacing; }
bool CastMember::editable() const { return editable_; }
void CastMember::setEditable(bool editable) { editable_ = editable; }

std::shared_ptr<const bitmap::Palette> CastMember::paletteData() const {
    return dynamicPalette_;
}

void CastMember::setPaletteData(std::shared_ptr<const bitmap::Palette> palette) {
    dynamicPalette_ = std::move(palette);
}

std::shared_ptr<const bitmap::Palette> CastMember::runtimePaletteOverride() const {
    return runtimePaletteOverride_;
}

int CastMember::paletteRefCastLib() const { return paletteRefCastLib_; }
int CastMember::paletteRefMemberNum() const { return paletteRefMemberNum_; }
int CastMember::paletteVersion() const { return paletteVersion_; }

const std::optional<std::string>& CastMember::paletteRefSystemName() const {
    return paletteRefSystemName_;
}

void CastMember::setRuntimePaletteOverride(std::shared_ptr<const bitmap::Palette> palette,
                                           int paletteRefCastLib,
                                           int paletteRefMemberNum,
                                           std::optional<std::string> paletteRefSystemName,
                                           bool remapDeepBitmapRgb) {
    const bool changed = runtimePaletteOverride_.get() != palette.get() ||
                         paletteRefCastLib_ != paletteRefCastLib ||
                         paletteRefMemberNum_ != paletteRefMemberNum ||
                         paletteRefSystemName_ != paletteRefSystemName;
    runtimePaletteOverride_ = std::move(palette);
    paletteRefCastLib_ = paletteRefCastLib;
    paletteRefMemberNum_ = paletteRefMemberNum;
    paletteRefSystemName_ = std::move(paletteRefSystemName);
    if (changed) {
        ++paletteVersion_;
    }

    if (!runtimeBitmap_ || !runtimePaletteOverride_) {
        return;
    }

    if (runtimeBitmapAuthoredSource_ && !runtimeBitmap_->isScriptModified()) {
        return;
    }

    if (runtimeBitmap_->bitDepth() <= 8 || remapDeepBitmapRgb) {
        (void)runtimeBitmap_->remapImagePalette(runtimePaletteOverride_);
    } else {
        runtimeBitmap_->setImagePalette(runtimePaletteOverride_);
    }
    if (paletteRefCastLib_ >= 1 && paletteRefMemberNum_ >= 1) {
        runtimeBitmap_->setPaletteRefCastMember(paletteRefCastLib_, paletteRefMemberNum_);
    } else if (paletteRefSystemName_.has_value()) {
        runtimeBitmap_->setPaletteRefSystemName(*paletteRefSystemName_);
    } else {
        runtimeBitmap_->clearPaletteRefMetadata();
    }
    runtimeBitmap_->markScriptModified();
}

void CastMember::setRuntimeBitmap(const bitmap::Bitmap& bitmap, bool markScriptModified) {
    setRuntimeBitmapInternal(bitmap, markScriptModified, false);
}

bool CastMember::shouldRedecodeAuthoredRuntimeBitmap() const {
    return runtimeBitmap_ &&
           runtimeBitmapAuthoredSource_ &&
           !runtimeBitmap_->isScriptModified() &&
           paletteVersion_ > runtimeBitmapPaletteVersion_;
}

void CastMember::setRuntimeBitmapFromAuthoredSource(const bitmap::Bitmap& bitmap) {
    setRuntimeBitmapInternal(bitmap, false, true);
}

void CastMember::setRuntimeBitmapInternal(const bitmap::Bitmap& bitmap,
                                          bool markScriptModified,
                                          bool authoredSource) {
    const int currentRegX = regX();
    const int currentRegY = regY();
    auto copy = authoredSource ? bitmap.copyWithDegenerateNativeAlphaOpaque() : bitmap.copy();
    if (markScriptModified) {
        copy.markScriptModified();
    }
    if (regPointPinnedToMember_) {
        runtimeRegX_ = currentRegX;
        runtimeRegY_ = currentRegY;
    } else if (bitmap.hasAnchorPoint()) {
        runtimeRegX_ = bitmap.anchorX();
        runtimeRegY_ = bitmap.anchorY();
    } else {
        runtimeRegX_ = 0;
        runtimeRegY_ = 0;
    }
    if (runtimeBitmap_) {
        *runtimeBitmap_ = std::move(copy);
    } else {
        runtimeBitmap_ = std::make_shared<bitmap::Bitmap>(std::move(copy));
    }
    runtimeBitmapAuthoredSource_ = authoredSource;
    runtimeBitmapPaletteVersion_ = authoredSource ? paletteVersion_ : 0;
    syncRuntimeBitmapAnchorState();
}

void CastMember::erase() {
    resetRuntimePayload();
    memberType_ = MemberType::Null;
}

void CastMember::reuseAs(MemberType memberType) {
    resetRuntimePayload();
    memberType_ = memberType;
}

std::string CastMember::toString() const {
    std::ostringstream out;
    out << "CastMember[lib=" << castLib_
        << ", num=" << memberNum_
        << ", type=" << libreshockwave::cast::name(memberType_);
    if (!name_.empty()) {
        out << ", name=\"" << name_ << "\"";
    }
    if (bitmapInfo_) {
        out << ", " << bitmapInfo_->width << "x" << bitmapInfo_->height
            << "x" << bitmapInfo_->bitDepth << "bit";
    }
    if (scriptType_) {
        out << ", scriptType=" << libreshockwave::cast::name(*scriptType_);
    }
    out << "]";
    return out.str();
}

void CastMember::parseSpecificData() {
    if (!rawChunk_) {
        return;
    }

    const auto& specificData = rawChunk_->specificData();
    switch (memberType_) {
        case MemberType::Bitmap:
            bitmapInfo_ = BitmapInfo::parse(specificData);
            bitmapAlphaThreshold_ = bitmapInfo_->alphaThreshold;
            break;
        case MemberType::Shape:
            shapeInfo_ = ShapeInfo::parse(specificData);
            break;
        case MemberType::FilmLoop:
            filmLoopInfo_ = FilmLoopInfo::parse(specificData);
            break;
        case MemberType::Script:
            if (specificData.size() >= 2) {
                const int typeCode = (static_cast<int>(specificData[0]) << 8) |
                                     static_cast<int>(specificData[1]);
                scriptType_ = scriptTypeFromCode(typeCode);
            }
            break;
        case MemberType::Shockwave3D:
            shockwave3DInfo_ = Shockwave3DInfo::parse(specificData);
            break;
        default:
            break;
    }
}

void CastMember::resetRuntimePayload() {
    name_.clear();
    scriptId_ = 0;
    bitmapInfo_.reset();
    shapeInfo_.reset();
    runtimeShapeLineSize_.reset();
    shapePattern_ = 1;
    filmLoopInfo_.reset();
    scriptType_.reset();
    shockwave3DInfo_.reset();
    runtimeScript_.reset();
    runtimeBitmap_.reset();
    runtimeBitmapAuthoredSource_ = false;
    runtimeBitmapPaletteVersion_ = 0;
    runtimePaletteOverride_.reset();
    paletteRefCastLib_ = -1;
    paletteRefMemberNum_ = -1;
    paletteRefSystemName_.reset();
    runtimeRegX_.reset();
    runtimeRegY_.reset();
    dynamicPalette_.reset();
    bitmapAlphaThreshold_ = 0;
    regPointPinnedToMember_ = false;
    dynamicText_.reset();
    ++textRevision_;
    resetTextProperties();
}

void CastMember::resetTextProperties() {
    textFont_ = "Arial";
    textFontSize_ = 12;
    textFontStyle_ = "plain";
    textAlignment_ = "left";
    textColor_ = static_cast<int>(0xFF000000U);
    textBgColor_ = static_cast<int>(0xFFFFFFFFU);
    textWordWrap_ = false;
    textAntialias_ = false;
    textBoxType_ = 0;
    textRectLeft_ = 0;
    textRectTop_ = 0;
    textRectRight_ = 480;
    textRectBottom_ = 480;
    textFixedLineSpace_ = 0;
    textTopSpacing_ = 0;
    editable_ = false;
}

void CastMember::ensureRuntimeShapeInfo() {
    if (shapeInfo_.has_value()) {
        return;
    }
    shapeInfo_ = ShapeInfo{ShapeType::Rect, 0, 0, 0, 0, 0, 0, 1, 1, 0};
}

void CastMember::syncRuntimeBitmapAnchorState() {
    if (!runtimeBitmap_) {
        return;
    }
    if (regPointPinnedToMember_) {
        runtimeBitmap_->setAnchorPoint(regX(), regY());
        return;
    }
    if (runtimeBitmap_->hasAnchorPoint()) {
        runtimeRegX_ = runtimeBitmap_->anchorX();
        runtimeRegY_ = runtimeBitmap_->anchorY();
    }
}

} // namespace libreshockwave::cast
