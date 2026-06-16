#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "libreshockwave/cast/BitmapInfo.hpp"
#include "libreshockwave/cast/FilmLoopInfo.hpp"
#include "libreshockwave/cast/MemberType.hpp"
#include "libreshockwave/cast/ScriptType.hpp"
#include "libreshockwave/cast/ShapeInfo.hpp"
#include "libreshockwave/cast/Shockwave3DInfo.hpp"

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
    CastMember(int id, int castLib, int memberNum, std::shared_ptr<chunks::CastMemberChunk> chunk);
    CastMember(int castLib, int memberNum, MemberType memberType);

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
    void setBitmapAlphaThreshold(int alphaThreshold);
    [[nodiscard]] bool regPointPinnedToMember() const;
    void setRegPoint(int x, int y);
    void setRegPointState(int x, int y, bool pinnedToMember);
    [[nodiscard]] bool shapeFilled() const;
    void setShapeFilled(bool filled);
    [[nodiscard]] ShapeType shapeType() const;
    void setShapeType(ShapeType type);
    [[nodiscard]] int shapeLineSize() const;
    [[nodiscard]] bool hasRuntimeShapeLineSize() const;
    void setShapeLineSize(int lineSize);
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
    void setTextWordWrap(bool wordWrap);
    [[nodiscard]] bool textAntialias() const;
    void setTextAntialias(bool antialias);
    [[nodiscard]] int textBoxType() const;
    void setTextBoxType(int boxType);
    [[nodiscard]] int textRectLeft() const;
    [[nodiscard]] int textRectTop() const;
    [[nodiscard]] int textRectRight() const;
    [[nodiscard]] int textRectBottom() const;
    void setTextRect(int left, int top, int right, int bottom);
    void setTextWidth(int width);
    void setTextHeight(int height);
    [[nodiscard]] int textFixedLineSpace() const;
    void setTextFixedLineSpace(int lineSpace);
    [[nodiscard]] int textTopSpacing() const;
    void setTextTopSpacing(int spacing);
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
                                   int paletteRefCastLib,
                                   int paletteRefMemberNum,
                                   std::optional<std::string> paletteRefSystemName,
                                   bool remapDeepBitmapRgb);
    [[nodiscard]] std::shared_ptr<bitmap::Bitmap> runtimeBitmap() const;
    [[nodiscard]] bool shouldRedecodeAuthoredRuntimeBitmap() const;
    void setRuntimeBitmap(const bitmap::Bitmap& bitmap, bool markScriptModified = true);
    void setRuntimeBitmapFromAuthoredSource(const bitmap::Bitmap& bitmap);
    void syncRuntimeBitmapAnchorState();
    void erase();
    void reuseAs(MemberType memberType);
    [[nodiscard]] std::string toString() const;

private:
    void parseSpecificData();
    void resetRuntimePayload();
    void resetTextProperties();
    void ensureRuntimeShapeInfo();
    void setRuntimeBitmapInternal(const bitmap::Bitmap& bitmap, bool markScriptModified, bool authoredSource);

    int id_;
    int castLib_;
    int memberNum_;
    MemberType memberType_;
    std::string name_;
    int scriptId_;
    std::shared_ptr<chunks::CastMemberChunk> rawChunk_;

    std::optional<BitmapInfo> bitmapInfo_;
    std::optional<ShapeInfo> shapeInfo_;
    std::optional<int> runtimeShapeLineSize_;
    int shapePattern_{1};
    std::optional<FilmLoopInfo> filmLoopInfo_;
    std::optional<ScriptType> scriptType_;
    std::optional<Shockwave3DInfo> shockwave3DInfo_;
    std::shared_ptr<chunks::ScriptChunk> runtimeScript_;
    std::shared_ptr<bitmap::Bitmap> runtimeBitmap_;
    bool runtimeBitmapAuthoredSource_ = false;
    int runtimeBitmapPaletteVersion_ = 0;
    std::shared_ptr<const bitmap::Palette> runtimePaletteOverride_;
    int paletteRefCastLib_ = -1;
    int paletteRefMemberNum_ = -1;
    int paletteVersion_ = 0;
    std::optional<std::string> paletteRefSystemName_;
    std::optional<int> runtimeRegX_;
    std::optional<int> runtimeRegY_;
    std::shared_ptr<const bitmap::Palette> dynamicPalette_;
    int bitmapAlphaThreshold_ = 0;
    bool regPointPinnedToMember_ = true;
    std::optional<std::string> dynamicText_;
    std::uint64_t textRevision_ = 0;
    std::string textFont_ = "Arial";
    int textFontSize_ = 12;
    std::string textFontStyle_ = "plain";
    std::string textAlignment_ = "left";
    int textColor_ = static_cast<int>(0xFF000000U);
    int textBgColor_ = static_cast<int>(0xFFFFFFFFU);
    bool textWordWrap_ = false;
    bool textAntialias_ = false;
    int textBoxType_ = 0;
    int textRectLeft_ = 0;
    int textRectTop_ = 0;
    int textRectRight_ = 480;
    int textRectBottom_ = 480;
    int textFixedLineSpace_ = 0;
    int textTopSpacing_ = 0;
    bool editable_ = false;
};

} // namespace libreshockwave::cast
