#include "libreshockwave/cast/cast_member.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/bitmap/palette.hpp"
#include "libreshockwave/chunks/cast_member_chunk.hpp"

namespace libreshockwave::cast {

CastMember::CastMember(int id, int cast_lib, int member_num, std::shared_ptr<chunks::CastMemberChunk> chunk)
    : id_(id),
      cast_lib_(cast_lib),
      member_num_(member_num),
      member_type_(chunk ? chunk->memberType() : MemberType::Unknown),
      name_(chunk ? chunk->name() : ""),
      script_id_(chunk ? chunk->scriptId() : 0),
      raw_chunk_(std::move(chunk)) {
    parseSpecificData();
}

CastMember::CastMember(int cast_lib, int member_num, MemberType member_type)
    : id_(0),
      cast_lib_(cast_lib),
      member_num_(member_num),
      member_type_(member_type),
      script_id_(0) {
    reg_point_pinned_to_member_ = false;
}

int CastMember::id() const { return id_; }
int CastMember::castLib() const { return cast_lib_; }
int CastMember::memberNum() const { return member_num_; }
MemberType CastMember::memberType() const { return member_type_; }
const std::string& CastMember::name() const { return name_; }
void CastMember::setName(std::string name) { name_ = std::move(name); }
int CastMember::scriptId() const { return script_id_; }
const std::shared_ptr<chunks::CastMemberChunk>& CastMember::rawChunk() const { return raw_chunk_; }

const std::optional<std::vector<std::uint8_t>>& CastMember::runtimeSoundMedia() const {
    return runtime_sound_media_;
}

void CastMember::setRuntimeSoundMedia(std::vector<std::uint8_t> media) {
    runtime_sound_media_ = std::move(media);
}

const std::optional<BitmapInfo>& CastMember::bitmapInfo() const { return bitmap_info_; }
const std::optional<ShapeInfo>& CastMember::shapeInfo() const { return shape_info_; }
const std::optional<FilmLoopInfo>& CastMember::filmLoopInfo() const { return film_loop_info_; }
const std::optional<ScriptType>& CastMember::scriptType() const { return script_type_; }
const std::optional<Shockwave3DInfo>& CastMember::shockwave3DInfo() const { return shockwave3d_info_; }
std::shared_ptr<chunks::ScriptChunk> CastMember::runtimeScript() const { return runtime_script_; }
void CastMember::setRuntimeScript(std::shared_ptr<chunks::ScriptChunk> script) {
    runtime_script_ = std::move(script);
}

bool CastMember::isBitmap() const { return member_type_ == MemberType::Bitmap; }
bool CastMember::isText() const { return member_type_ == MemberType::Text; }
bool CastMember::isTextLike() const {
    return member_type_ == MemberType::Text ||
           member_type_ == MemberType::RichText ||
           member_type_ == MemberType::Button ||
           (member_type_ == MemberType::Xtra && raw_chunk_ != nullptr && raw_chunk_->isTextXtra());
}
bool CastMember::isSound() const { return member_type_ == MemberType::Sound; }
bool CastMember::isScript() const { return member_type_ == MemberType::Script; }
bool CastMember::isShape() const { return member_type_ == MemberType::Shape; }
bool CastMember::isFilmLoop() const { return member_type_ == MemberType::FilmLoop; }
bool CastMember::isPalette() const { return member_type_ == MemberType::Palette; }
bool CastMember::isFont() const { return member_type_ == MemberType::Font; }
bool CastMember::isShockwave3D() const { return member_type_ == MemberType::Shockwave3D; }
bool CastMember::isRuntimeDynamic() const { return raw_chunk_ == nullptr; }
bool CastMember::isReusableDynamicSlot() const {
    return isRuntimeDynamic() && member_type_ == MemberType::Null && name_.empty();
}

int CastMember::width() const {
    if (runtime_bitmap_) return runtime_bitmap_->width();
    if (bitmap_info_) return bitmap_info_->width;
    if (shape_info_) return shape_info_->width;
    if (film_loop_info_) return film_loop_info_->width();
    return 0;
}

int CastMember::height() const {
    if (runtime_bitmap_) return runtime_bitmap_->height();
    if (bitmap_info_) return bitmap_info_->height;
    if (shape_info_) return shape_info_->height;
    if (film_loop_info_) return film_loop_info_->height();
    return 0;
}

int CastMember::regX() const {
    if (runtime_reg_x_.has_value()) return *runtime_reg_x_;
    if (bitmap_info_) return bitmap_info_->reg_x;
    if (shape_info_) return shape_info_->reg_x;
    if (film_loop_info_) return film_loop_info_->regX();
    return 0;
}

int CastMember::regY() const {
    if (runtime_reg_y_.has_value()) return *runtime_reg_y_;
    if (bitmap_info_) return bitmap_info_->reg_y;
    if (shape_info_) return shape_info_->reg_y;
    if (film_loop_info_) return film_loop_info_->regY();
    return 0;
}

int CastMember::bitmapAlphaThreshold() const {
    return bitmap_alpha_threshold_;
}

void CastMember::setBitmapAlphaThreshold(int alpha_threshold) {
    bitmap_alpha_threshold_ = std::clamp(alpha_threshold, 0, 255);
}

void CastMember::setRegPoint(int x, int y) {
    setRegPointState(x, y, true);
}

bool CastMember::regPointPinnedToMember() const {
    return reg_point_pinned_to_member_;
}

void CastMember::setRegPointState(int x, int y, bool pinned_to_member) {
    runtime_reg_x_ = x;
    runtime_reg_y_ = y;
    reg_point_pinned_to_member_ = pinned_to_member;
    syncRuntimeBitmapAnchorState();
}

bool CastMember::shapeFilled() const {
    if (!isShape()) {
        return false;
    }
    return !shape_info_.has_value() || shape_info_->isFilled();
}

void CastMember::setShapeFilled(bool filled) {
    if (!isShape()) {
        return;
    }
    ensureRuntimeShapeInfo();
    shape_info_->fill_type = filled ? 1 : 0;
}

ShapeType CastMember::shapeType() const {
    if (!isShape()) {
        return ShapeType::Unknown;
    }
    return shape_info_.has_value() ? shape_info_->shape_type : ShapeType::Rect;
}

void CastMember::setShapeType(ShapeType type) {
    if (!isShape()) {
        return;
    }
    ensureRuntimeShapeInfo();
    shape_info_->shape_type = type == ShapeType::Unknown ? ShapeType::Rect : type;
}

int CastMember::shapeLineSize() const {
    if (runtime_shape_line_size_.has_value()) {
        return *runtime_shape_line_size_;
    }
    if (shape_info_.has_value()) {
        return std::max(0, shape_info_->line_thickness);
    }
    return isShape() ? 1 : 0;
}

bool CastMember::hasRuntimeShapeLineSize() const {
    return runtime_shape_line_size_.has_value();
}

void CastMember::setShapeLineSize(int line_size) {
    if (!isShape()) {
        return;
    }
    runtime_shape_line_size_ = std::max(0, line_size);
}

int CastMember::shapePattern() const {
    return isShape() ? shape_pattern_ : 1;
}

void CastMember::setShapePattern(int pattern) {
    if (!isShape()) {
        return;
    }
    shape_pattern_ = std::max(1, pattern);
}

void CastMember::setShapeWidth(int width) {
    if (!isShape()) {
        return;
    }
    ensureRuntimeShapeInfo();
    shape_info_->width = std::max(0, width);
}

void CastMember::setShapeHeight(int height) {
    if (!isShape()) {
        return;
    }
    ensureRuntimeShapeInfo();
    shape_info_->height = std::max(0, height);
}

void CastMember::setShapeSize(int width, int height) {
    if (!isShape()) {
        return;
    }
    ensureRuntimeShapeInfo();
    shape_info_->width = std::max(0, width);
    shape_info_->height = std::max(0, height);
}

std::shared_ptr<bitmap::Bitmap> CastMember::runtimeBitmap() const {
    return runtime_bitmap_;
}

bool CastMember::hasDynamicText() const {
    return dynamic_text_.has_value();
}

std::string CastMember::textContent() const {
    return dynamic_text_.value_or("");
}

void CastMember::setDynamicText(std::string text) {
    dynamic_text_ = std::move(text);
    ++text_revision_;
}

std::uint64_t CastMember::textRevision() const { return text_revision_; }

const std::string& CastMember::textFont() const { return text_font_; }
void CastMember::setTextFont(std::string font) { text_font_ = std::move(font); }
int CastMember::textFontSize() const { return text_font_size_; }
void CastMember::setTextFontSize(int size) { text_font_size_ = size; }
const std::string& CastMember::textFontStyle() const { return text_font_style_; }
void CastMember::setTextFontStyle(std::string style) { text_font_style_ = std::move(style); }
const std::string& CastMember::textAlignment() const { return text_alignment_; }
void CastMember::setTextAlignment(std::string alignment) { text_alignment_ = std::move(alignment); }
int CastMember::textColor() const { return text_color_; }
void CastMember::setTextColor(int argb) { text_color_ = argb; }
int CastMember::textBgColor() const { return text_bg_color_; }
void CastMember::setTextBgColor(int argb) { text_bg_color_ = argb; }
bool CastMember::textWordWrap() const { return text_word_wrap_; }
void CastMember::setTextWordWrap(bool word_wrap) { text_word_wrap_ = word_wrap; }
bool CastMember::textAntialias() const { return text_antialias_; }
void CastMember::setTextAntialias(bool antialias) { text_antialias_ = antialias; }
int CastMember::textBoxType() const { return text_box_type_; }
void CastMember::setTextBoxType(int box_type) { text_box_type_ = box_type; }
int CastMember::textRectLeft() const { return text_rect_left_; }
int CastMember::textRectTop() const { return text_rect_top_; }
int CastMember::textRectRight() const { return text_rect_right_; }
int CastMember::textRectBottom() const { return text_rect_bottom_; }
void CastMember::setTextRect(int left, int top, int right, int bottom) {
    text_rect_left_ = left;
    text_rect_top_ = top;
    text_rect_right_ = right;
    text_rect_bottom_ = bottom;
}
void CastMember::setTextWidth(int width) { text_rect_right_ = text_rect_left_ + width; }
void CastMember::setTextHeight(int height) { text_rect_bottom_ = text_rect_top_ + height; }
int CastMember::textFixedLineSpace() const { return text_fixed_line_space_; }
void CastMember::setTextFixedLineSpace(int line_space) { text_fixed_line_space_ = line_space; }
int CastMember::textTopSpacing() const { return text_top_spacing_; }
void CastMember::setTextTopSpacing(int spacing) { text_top_spacing_ = spacing; }
int CastMember::textGutterSize() const { return text_gutter_size_; }
void CastMember::setTextGutterSize(int gutter) { text_gutter_size_ = std::max(0, gutter); }
bool CastMember::editable() const { return editable_; }
void CastMember::setEditable(bool editable) { editable_ = editable; }

std::shared_ptr<const bitmap::Palette> CastMember::paletteData() const {
    return dynamic_palette_;
}

void CastMember::setPaletteData(std::shared_ptr<const bitmap::Palette> palette) {
    dynamic_palette_ = std::move(palette);
}

std::shared_ptr<const bitmap::Palette> CastMember::runtimePaletteOverride() const {
    return runtime_palette_override_;
}

int CastMember::paletteRefCastLib() const { return palette_ref_cast_lib_; }
int CastMember::paletteRefMemberNum() const { return palette_ref_member_num_; }
int CastMember::paletteVersion() const { return palette_version_; }

const std::optional<std::string>& CastMember::paletteRefSystemName() const {
    return palette_ref_system_name_;
}

void CastMember::setRuntimePaletteOverride(std::shared_ptr<const bitmap::Palette> palette,
                                           int palette_ref_cast_lib,
                                           int palette_ref_member_num,
                                           std::optional<std::string> palette_ref_system_name,
                                           bool remap_deep_bitmap_rgb) {
    const bool changed = runtime_palette_override_.get() != palette.get() ||
                         palette_ref_cast_lib_ != palette_ref_cast_lib ||
                         palette_ref_member_num_ != palette_ref_member_num ||
                         palette_ref_system_name_ != palette_ref_system_name;
    runtime_palette_override_ = std::move(palette);
    palette_ref_cast_lib_ = palette_ref_cast_lib;
    palette_ref_member_num_ = palette_ref_member_num;
    palette_ref_system_name_ = std::move(palette_ref_system_name);
    if (changed) {
        ++palette_version_;
    }

    if (!runtime_bitmap_ || !runtime_palette_override_) {
        return;
    }

    if (runtime_bitmap_authored_source_ && !runtime_bitmap_->isScriptModified()) {
        return;
    }

    if (runtime_bitmap_->bitDepth() <= 8 || remap_deep_bitmap_rgb) {
        (void)runtime_bitmap_->remapImagePalette(runtime_palette_override_);
    } else {
        runtime_bitmap_->setImagePalette(runtime_palette_override_);
    }
    if (palette_ref_cast_lib_ >= 1 && palette_ref_member_num_ >= 1) {
        runtime_bitmap_->setPaletteRefCastMember(palette_ref_cast_lib_, palette_ref_member_num_);
    } else if (palette_ref_system_name_.has_value()) {
        runtime_bitmap_->setPaletteRefSystemName(*palette_ref_system_name_);
    } else {
        runtime_bitmap_->clearPaletteRefMetadata();
    }
    runtime_bitmap_->markScriptModified();
}

void CastMember::setRuntimeBitmap(const bitmap::Bitmap& bitmap, bool mark_script_modified) {
    setRuntimeBitmapInternal(bitmap, mark_script_modified, false);
}

bool CastMember::shouldRedecodeAuthoredRuntimeBitmap() const {
    return runtime_bitmap_ &&
           runtime_bitmap_authored_source_ &&
           !runtime_bitmap_->isScriptModified() &&
           palette_version_ > runtime_bitmap_palette_version_;
}

void CastMember::setRuntimeBitmapFromAuthoredSource(const bitmap::Bitmap& bitmap) {
    setRuntimeBitmapInternal(bitmap, false, true);
}

void CastMember::setRuntimeBitmapDecodePlaceholder(const bitmap::Bitmap& bitmap) {
    setRuntimeBitmapInternal(bitmap, false, true);
    runtime_bitmap_decode_placeholder_ = true;
}

bool CastMember::hasRuntimeBitmapDecodePlaceholder() const {
    return runtime_bitmap_decode_placeholder_;
}

void CastMember::setRuntimeBitmapInternal(const bitmap::Bitmap& bitmap,
                                          bool mark_script_modified,
                                          bool authored_source) {
    const int current_reg_x = regX();
    const int current_reg_y = regY();
    auto copy = authored_source ? bitmap.copyWithDegenerateNativeAlphaOpaque() : bitmap.copy();
    if (authored_source && !copy.isNativeAlpha()) {
        // Authored 32-bit records without native alpha use the alpha byte as
        // padding. Preserve their RGB pixels when they are exposed directly
        // through member.image, rather than letting zero-alpha edge pixels
        // disappear during a later copyPixels operation.
        copy = copy.copyWithNonNativeAlphaOpaque();
    }
    if (mark_script_modified) {
        copy.markScriptModified();
    }
    if (reg_point_pinned_to_member_) {
        runtime_reg_x_ = current_reg_x;
        runtime_reg_y_ = current_reg_y;
    } else if (bitmap.hasAnchorPoint()) {
        runtime_reg_x_ = bitmap.anchorX();
        runtime_reg_y_ = bitmap.anchorY();
    } else {
        runtime_reg_x_ = 0;
        runtime_reg_y_ = 0;
    }
    if (runtime_bitmap_) {
        *runtime_bitmap_ = std::move(copy);
    } else {
        runtime_bitmap_ = std::make_shared<bitmap::Bitmap>(std::move(copy));
    }
    runtime_bitmap_authored_source_ = authored_source;
    runtime_bitmap_decode_placeholder_ = false;
    runtime_bitmap_palette_version_ = authored_source ? palette_version_ : 0;
    syncRuntimeBitmapAnchorState();
}

void CastMember::erase() {
    resetRuntimePayload();
    member_type_ = MemberType::Null;
}

void CastMember::reuseAs(MemberType member_type) {
    resetRuntimePayload();
    member_type_ = member_type;
}

std::string CastMember::toString() const {
    std::ostringstream out;
    out << "CastMember[lib=" << cast_lib_
        << ", num=" << member_num_
        << ", type=" << libreshockwave::cast::name(member_type_);
    if (!name_.empty()) {
        out << ", name=\"" << name_ << "\"";
    }
    if (bitmap_info_) {
        out << ", " << bitmap_info_->width << "x" << bitmap_info_->height
            << "x" << bitmap_info_->bit_depth << "bit";
    }
    if (script_type_) {
        out << ", scriptType=" << libreshockwave::cast::name(*script_type_);
    }
    out << "]";
    return out.str();
}

void CastMember::parseSpecificData() {
    if (!raw_chunk_) {
        return;
    }

    const auto& specific_data = raw_chunk_->specificData();
    switch (member_type_) {
        case MemberType::Bitmap:
            bitmap_info_ = BitmapInfo::parse(specific_data);
            bitmap_alpha_threshold_ = bitmap_info_->alpha_threshold;
            break;
        case MemberType::Shape:
            shape_info_ = ShapeInfo::parse(specific_data);
            break;
        case MemberType::FilmLoop:
            film_loop_info_ = FilmLoopInfo::parse(specific_data);
            break;
        case MemberType::Script:
            if (specific_data.size() >= 2) {
                const int type_code = (static_cast<int>(specific_data[0]) << 8) |
                                     static_cast<int>(specific_data[1]);
                script_type_ = scriptTypeFromCode(type_code);
            }
            break;
        case MemberType::Shockwave3D:
            shockwave3d_info_ = Shockwave3DInfo::parse(specific_data);
            break;
        default:
            break;
    }
}

void CastMember::resetRuntimePayload() {
    name_.clear();
    script_id_ = 0;
    bitmap_info_.reset();
    shape_info_.reset();
    runtime_shape_line_size_.reset();
    shape_pattern_ = 1;
    film_loop_info_.reset();
    script_type_.reset();
    shockwave3d_info_.reset();
    runtime_script_.reset();
    runtime_bitmap_.reset();
    runtime_sound_media_.reset();
    runtime_bitmap_authored_source_ = false;
    runtime_bitmap_decode_placeholder_ = false;
    runtime_bitmap_palette_version_ = 0;
    runtime_palette_override_.reset();
    palette_ref_cast_lib_ = -1;
    palette_ref_member_num_ = -1;
    palette_ref_system_name_.reset();
    runtime_reg_x_.reset();
    runtime_reg_y_.reset();
    dynamic_palette_.reset();
    bitmap_alpha_threshold_ = 0;
    reg_point_pinned_to_member_ = false;
    dynamic_text_.reset();
    ++text_revision_;
    resetTextProperties();
}

void CastMember::resetTextProperties() {
    text_font_ = "Arial";
    text_font_size_ = 12;
    text_font_style_ = "plain";
    text_alignment_ = "left";
    text_color_ = static_cast<int>(0xFF000000U);
    text_bg_color_ = static_cast<int>(0xFFFFFFFFU);
    text_word_wrap_ = false;
    text_antialias_ = false;
    text_box_type_ = 0;
    text_rect_left_ = 0;
    text_rect_top_ = 0;
    text_rect_right_ = 480;
    text_rect_bottom_ = 480;
    text_fixed_line_space_ = 0;
    text_top_spacing_ = 0;
    text_gutter_size_ = 2;
    editable_ = false;
}

void CastMember::ensureRuntimeShapeInfo() {
    if (shape_info_.has_value()) {
        return;
    }
    shape_info_ = ShapeInfo{ShapeType::Rect, 0, 0, 0, 0, 0, 0, 1, 1, 0};
}

void CastMember::syncRuntimeBitmapAnchorState() {
    if (!runtime_bitmap_) {
        return;
    }
    if (reg_point_pinned_to_member_) {
        runtime_bitmap_->setAnchorPoint(regX(), regY());
        return;
    }
    if (runtime_bitmap_->hasAnchorPoint()) {
        runtime_reg_x_ = runtime_bitmap_->anchorX();
        runtime_reg_y_ = runtime_bitmap_->anchorY();
    }
}

} // namespace libreshockwave::cast
