#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "libreshockwave/cast/bitmap_info.hpp"
#include "libreshockwave/cast/film_loop_info.hpp"
#include "libreshockwave/cast/member_type.hpp"
#include "libreshockwave/cast/script_type.hpp"
#include "libreshockwave/cast/shape_info.hpp"
#include "libreshockwave/cast/shockwave3d_info.hpp"

namespace libreshockwave::chunks {
class CastMemberChunk;
class ScriptChunk;
}

namespace libreshockwave::bitmap {
class Bitmap;
class Palette;
}

namespace libreshockwave::cast {

class CastMember {
public:
    CastMember(int id, int cast_lib, int member_num, std::shared_ptr<chunks::CastMemberChunk> chunk);
    CastMember(int cast_lib, int member_num, MemberType member_type);

    [[nodiscard]] int id() const;
    [[nodiscard]] int castLib() const;
    [[nodiscard]] int memberNum() const;
    [[nodiscard]] MemberType memberType() const;
    [[nodiscard]] const std::string& name() const;
    void setName(std::string name);
    [[nodiscard]] int scriptId() const;
    [[nodiscard]] const std::shared_ptr<chunks::CastMemberChunk>& rawChunk() const;

    [[nodiscard]] const std::optional<BitmapInfo>& bitmapInfo() const;
    [[nodiscard]] const std::optional<ShapeInfo>& shapeInfo() const;
    [[nodiscard]] const std::optional<FilmLoopInfo>& filmLoopInfo() const;
    [[nodiscard]] const std::optional<ScriptType>& scriptType() const;
    [[nodiscard]] const std::optional<Shockwave3DInfo>& shockwave3DInfo() const;
    [[nodiscard]] std::shared_ptr<chunks::ScriptChunk> runtimeScript() const;
    void setRuntimeScript(std::shared_ptr<chunks::ScriptChunk> script);

    [[nodiscard]] bool isBitmap() const;
    [[nodiscard]] bool isText() const;
    [[nodiscard]] bool isTextLike() const;
    [[nodiscard]] bool isSound() const;
    [[nodiscard]] bool isScript() const;
    [[nodiscard]] bool isShape() const;
    [[nodiscard]] bool isFilmLoop() const;
    [[nodiscard]] bool isPalette() const;
    [[nodiscard]] bool isFont() const;
    [[nodiscard]] bool isShockwave3D() const;
    [[nodiscard]] bool isRuntimeDynamic() const;
    [[nodiscard]] bool isReusableDynamicSlot() const;

    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] int regX() const;
    [[nodiscard]] int regY() const;
    [[nodiscard]] int bitmapAlphaThreshold() const;
    void setBitmapAlphaThreshold(int alpha_threshold);
    [[nodiscard]] bool regPointPinnedToMember() const;
    void setRegPoint(int x, int y);
    void setRegPointState(int x, int y, bool pinned_to_member);
    [[nodiscard]] bool shapeFilled() const;
    void setShapeFilled(bool filled);
    [[nodiscard]] ShapeType shapeType() const;
    void setShapeType(ShapeType type);
    [[nodiscard]] int shapeLineSize() const;
    [[nodiscard]] bool hasRuntimeShapeLineSize() const;
    void setShapeLineSize(int line_size);
    [[nodiscard]] int shapePattern() const;
    void setShapePattern(int pattern);
    void setShapeWidth(int width);
    void setShapeHeight(int height);
    void setShapeSize(int width, int height);
    [[nodiscard]] bool hasDynamicText() const;
    [[nodiscard]] std::string textContent() const;
    void setDynamicText(std::string text);
    [[nodiscard]] std::uint64_t textRevision() const;
    [[nodiscard]] const std::string& textFont() const;
    void setTextFont(std::string font);
    [[nodiscard]] int textFontSize() const;
    void setTextFontSize(int size);
    [[nodiscard]] const std::string& textFontStyle() const;
    void setTextFontStyle(std::string style);
    [[nodiscard]] const std::string& textAlignment() const;
    void setTextAlignment(std::string alignment);
    [[nodiscard]] int textColor() const;
    void setTextColor(int argb);
    [[nodiscard]] int textBgColor() const;
    void setTextBgColor(int argb);
    [[nodiscard]] bool textWordWrap() const;
    void setTextWordWrap(bool word_wrap);
    [[nodiscard]] bool textAntialias() const;
    void setTextAntialias(bool antialias);
    [[nodiscard]] int textBoxType() const;
    void setTextBoxType(int box_type);
    [[nodiscard]] int textRectLeft() const;
    [[nodiscard]] int textRectTop() const;
    [[nodiscard]] int textRectRight() const;
    [[nodiscard]] int textRectBottom() const;
    void setTextRect(int left, int top, int right, int bottom);
    void setTextWidth(int width);
    void setTextHeight(int height);
    [[nodiscard]] int textFixedLineSpace() const;
    void setTextFixedLineSpace(int line_space);
    [[nodiscard]] int textTopSpacing() const;
    void setTextTopSpacing(int spacing);
    [[nodiscard]] int textGutterSize() const;
    void setTextGutterSize(int gutter);
    [[nodiscard]] bool editable() const;
    void setEditable(bool editable);
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> paletteData() const;
    void setPaletteData(std::shared_ptr<const bitmap::Palette> palette);
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> runtimePaletteOverride() const;
    [[nodiscard]] int paletteRefCastLib() const;
    [[nodiscard]] int paletteRefMemberNum() const;
    [[nodiscard]] int paletteVersion() const;
    [[nodiscard]] const std::optional<std::string>& paletteRefSystemName() const;
    void setRuntimePaletteOverride(std::shared_ptr<const bitmap::Palette> palette,
                                   int palette_ref_cast_lib,
                                   int palette_ref_member_num,
                                   std::optional<std::string> palette_ref_system_name,
                                   bool remap_deep_bitmap_rgb);
    [[nodiscard]] std::shared_ptr<bitmap::Bitmap> runtimeBitmap() const;
    [[nodiscard]] bool shouldRedecodeAuthoredRuntimeBitmap() const;
    void setRuntimeBitmap(const bitmap::Bitmap& bitmap, bool mark_script_modified = true);
    void setRuntimeBitmapFromAuthoredSource(const bitmap::Bitmap& bitmap);
    void setRuntimeBitmapDecodePlaceholder(const bitmap::Bitmap& bitmap);
    [[nodiscard]] bool hasRuntimeBitmapDecodePlaceholder() const;
    void syncRuntimeBitmapAnchorState();
    void erase();
    void reuseAs(MemberType member_type);
    [[nodiscard]] std::string toString() const;

private:
    void parseSpecificData();
    void resetRuntimePayload();
    void resetTextProperties();
    void ensureRuntimeShapeInfo();
    void setRuntimeBitmapInternal(const bitmap::Bitmap& bitmap, bool mark_script_modified, bool authored_source);

    int id_;
    int cast_lib_;
    int member_num_;
    MemberType member_type_;
    std::string name_;
    int script_id_;
    std::shared_ptr<chunks::CastMemberChunk> raw_chunk_;

    std::optional<BitmapInfo> bitmap_info_;
    std::optional<ShapeInfo> shape_info_;
    std::optional<int> runtime_shape_line_size_;
    int shape_pattern_{1};
    std::optional<FilmLoopInfo> film_loop_info_;
    std::optional<ScriptType> script_type_;
    std::optional<Shockwave3DInfo> shockwave3d_info_;
    std::shared_ptr<chunks::ScriptChunk> runtime_script_;
    std::shared_ptr<bitmap::Bitmap> runtime_bitmap_;
    bool runtime_bitmap_authored_source_ = false;
    bool runtime_bitmap_decode_placeholder_ = false;
    int runtime_bitmap_palette_version_ = 0;
    std::shared_ptr<const bitmap::Palette> runtime_palette_override_;
    int palette_ref_cast_lib_ = -1;
    int palette_ref_member_num_ = -1;
    int palette_version_ = 0;
    std::optional<std::string> palette_ref_system_name_;
    std::optional<int> runtime_reg_x_;
    std::optional<int> runtime_reg_y_;
    std::shared_ptr<const bitmap::Palette> dynamic_palette_;
    int bitmap_alpha_threshold_ = 0;
    bool reg_point_pinned_to_member_ = true;
    std::optional<std::string> dynamic_text_;
    std::uint64_t text_revision_ = 0;
    std::string text_font_ = "Arial";
    int text_font_size_ = 12;
    std::string text_font_style_ = "plain";
    std::string text_alignment_ = "left";
    int text_color_ = static_cast<int>(0xFF000000U);
    int text_bg_color_ = static_cast<int>(0xFFFFFFFFU);
    bool text_word_wrap_ = false;
    bool text_antialias_ = false;
    int text_box_type_ = 0;
    int text_rect_left_ = 0;
    int text_rect_top_ = 0;
    int text_rect_right_ = 480;
    int text_rect_bottom_ = 480;
    int text_fixed_line_space_ = 0;
    int text_top_spacing_ = 0;
    int text_gutter_size_ = 2;
    bool editable_ = false;
};

} // namespace libreshockwave::cast
