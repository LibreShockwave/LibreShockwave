#include "libreshockwave/player/render/pipeline/sprite_baker.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "libreshockwave/moviesource/movie_source.hpp"
#include "libreshockwave/cast/bitmap_info.hpp"
#include "libreshockwave/cast/cast_member.hpp"
#include "libreshockwave/cast/film_loop_info.hpp"
#include "libreshockwave/cast/shape_info.hpp"
#include "libreshockwave/cast/text_info.hpp"
#include "libreshockwave/chunks/cast_chunk.hpp"
#include "libreshockwave/chunks/cast_list_chunk.hpp"
#include "libreshockwave/chunks/config_chunk.hpp"
#include "libreshockwave/chunks/score_chunk.hpp"
#include "libreshockwave/chunks/text_chunk.hpp"
#include "libreshockwave/id/ids.hpp"
#include "libreshockwave/player/cast/font_registry.hpp"
#include "libreshockwave/player/render/output/simple_text_renderer.hpp"
#include "libreshockwave/player/render/output/text_renderer.hpp"
#include "libreshockwave/player/render/pipeline/ink_processor.hpp"

namespace libreshockwave::player::render::pipeline {
namespace {

std::uint32_t opaqueArgb(int rgb) {
    return 0xFF000000U | static_cast<std::uint32_t>(rgb & 0x00FFFFFF);
}

const ::libreshockwave::cast::ShapeInfo* dynamicShapeInfo(const RenderSprite& sprite) {
    const auto dynamic = sprite.dynamicMember();
    if (dynamic == nullptr || !dynamic->shapeInfo().has_value()) {
        return nullptr;
    }
    return &dynamic->shapeInfo().value();
}

bool shouldNeutralizeOpaqueWhiteForScriptCanvas(const RenderSprite& sprite, const bitmap::Bitmap& bitmap) {
    if (bitmap.bitDepth() != 32 || bitmap.isNativeAlpha() || !bitmap.isScriptModified()) {
        return false;
    }
    return sprite.inkMode() == id::InkMode::DARKEN || sprite.inkMode() == id::InkMode::LIGHTEN;
}

bool hasOpaqueWhiteEdgeAndContent(const bitmap::Bitmap& bitmap) {
    const int width = bitmap.width();
    const int height = bitmap.height();
    if (width <= 0 || height <= 0) {
        return false;
    }

    bool has_white_edge = false;
    bool has_non_white_content = false;
    const auto visit = [&](int x, int y, bool edge) {
        const auto pixel = bitmap.getPixel(x, y);
        if (((pixel >> 24U) & 0xFFU) == 0) {
            return;
        }
        if ((pixel & 0x00FFFFFFU) == 0x00FFFFFFU) {
            has_white_edge = has_white_edge || edge;
        } else {
            has_non_white_content = true;
        }
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            visit(x, y, x == 0 || y == 0 || x == width - 1 || y == height - 1);
        }
    }
    return has_white_edge && has_non_white_content;
}

bool shouldNeutralizeOpaqueWhiteForAuthoredTint(const RenderSprite& sprite, const bitmap::Bitmap& bitmap) {
    if (bitmap.isNativeAlpha() || bitmap.isRectangularMedia()) {
        return false;
    }
    if (sprite.inkMode() != id::InkMode::DARKEN && sprite.inkMode() != id::InkMode::LIGHTEN) {
        return false;
    }
    return hasOpaqueWhiteEdgeAndContent(bitmap);
}

bool shouldPreserveOutlinedWhiteBodyForScriptCanvas(const RenderSprite& sprite, const bitmap::Bitmap& bitmap) {
    if (sprite.inkMode() != id::InkMode::MATTE ||
        bitmap.bitDepth() != 32 ||
        !bitmap.isScriptModified() ||
        bitmap.isNativeAlpha()) {
        return false;
    }

    const auto member_name = sprite.memberName();
    return member_name.has_value() &&
           (member_name->starts_with("chat_item_background_") ||
            member_name->starts_with("chat_item_sing_background_"));
}

bool shouldPreserveScriptFillBacking(const RenderSprite& sprite, const bitmap::Bitmap& bitmap) {
    if (sprite.inkMode() != id::InkMode::BACKGROUND_TRANSPARENT ||
        !bitmap.isScriptModified() ||
        bitmap.isNativeAlpha() ||
        bitmap.isTextRendered()) {
        return false;
    }
    if (!bitmap.preservesScriptFillBacking()) {
        return false;
    }

    // A script-created white canvas is the key colour for Director's
    // background-transparent ink.  It must still be keyed at the sprite
    // boundary; only an authored non-white backing (for example the
    // catalogue panel grey) should survive as visible compositing state.
    const auto backing_color = bitmap.scriptFillBackingColor();
    return !backing_color.has_value() ||
           ((*backing_color & 0x00FFFFFFU) != 0x00FFFFFFU);
}

int shapeBorderStrokeCount(const RenderSprite& sprite, const ::libreshockwave::cast::ShapeInfo& shape_info) {
    if (const auto line_size = sprite.shapeLineSize()) {
        return std::max(0, *line_size);
    }
    if (const auto dynamic = sprite.dynamicMember();
        dynamic != nullptr && dynamic->isShape() && dynamic->hasRuntimeShapeLineSize()) {
        return std::max(0, dynamic->shapeLineSize());
    }
    return std::max(0, shape_info.line_thickness - 1);
}

int shapeLineStrokeCount(const RenderSprite& sprite, const ::libreshockwave::cast::ShapeInfo& shape_info) {
    if (const auto line_size = sprite.shapeLineSize()) {
        return std::max(0, *line_size);
    }
    if (const auto dynamic = sprite.dynamicMember();
        dynamic != nullptr && dynamic->isShape() && dynamic->hasRuntimeShapeLineSize()) {
        return std::max(0, dynamic->shapeLineSize());
    }
    return std::max(1, shape_info.line_thickness);
}

int normalizedShapePattern(const RenderSprite& sprite) {
    int pattern = sprite.shapePattern().value_or(1);
    if (!sprite.shapePattern().has_value()) {
        if (const auto dynamic = sprite.dynamicMember(); dynamic != nullptr && dynamic->isShape()) {
            pattern = dynamic->shapePattern();
        }
    }
    if (pattern <= 1) {
        return 1;
    }
    return ((pattern - 1) % 8) + 1;
}

bool shapePatternOn(int pattern, int x, int y) {
    switch (pattern) {
        case 1: return true;
        case 2: return ((x + y) & 1) == 0;
        case 3: return (y & 1) == 0;
        case 4: return (x & 1) == 0;
        case 5: return ((x + y) & 3) == 0;
        case 6: return ((x - y) & 3) == 0;
        case 7: return (x & 3) == 0 || (y & 3) == 0;
        case 8: return ((x + y) & 2) == 0;
        default: return true;
    }
}

std::uint32_t shapePaintArgb(const RenderSprite& sprite, int x, int y) {
    const int pattern = normalizedShapePattern(sprite);
    if (pattern <= 1 || shapePatternOn(pattern, x, y)) {
        return opaqueArgb(sprite.foreColor());
    }
    return sprite.hasBackColor() ? opaqueArgb(sprite.backColor()) : 0;
}

void paintShapePixel(bitmap::Bitmap& bitmap, const RenderSprite& sprite, int x, int y) {
    const auto argb = shapePaintArgb(sprite, x, y);
    if ((argb >> 24U) == 0) {
        return;
    }
    bitmap.setPixel(x, y, argb);
}

int channel(std::uint32_t argb, int shift) {
    return static_cast<int>((argb >> shift) & 0xFFU);
}

std::uint32_t alphaComposite(std::uint32_t dst, std::uint32_t src) {
    const int src_a = channel(src, 24);
    if (src_a <= 0) {
        return dst;
    }
    if (src_a >= 255) {
        return src | 0xFF000000U;
    }

    const int dst_a = channel(dst, 24);
    const int inv_a = 255 - src_a;
    const int out_a = src_a + (dst_a * inv_a / 255);
    if (out_a <= 0) {
        return 0;
    }

    const int out_r = (channel(src, 16) * src_a + channel(dst, 16) * dst_a * inv_a / 255) / out_a;
    const int out_g = (channel(src, 8) * src_a + channel(dst, 8) * dst_a * inv_a / 255) / out_a;
    const int out_b = (channel(src, 0) * src_a + channel(dst, 0) * dst_a * inv_a / 255) / out_a;
    return (static_cast<std::uint32_t>(out_a & 0xFF) << 24) |
           (static_cast<std::uint32_t>(out_r & 0xFF) << 16) |
           (static_cast<std::uint32_t>(out_g & 0xFF) << 8) |
           static_cast<std::uint32_t>(out_b & 0xFF);
}

void blitOnto(bitmap::Bitmap& dst, const bitmap::Bitmap& src, int offset_x, int offset_y) {
    for (int y = 0; y < src.height(); ++y) {
        const int dy = offset_y + y;
        if (dy < 0 || dy >= dst.height()) {
            continue;
        }
        for (int x = 0; x < src.width(); ++x) {
            const int dx = offset_x + x;
            if (dx < 0 || dx >= dst.width()) {
                continue;
            }
            const auto src_pixel = src.getPixel(x, y);
            if (channel(src_pixel, 24) == 0) {
                continue;
            }
            dst.setPixel(dx, dy, alphaComposite(dst.getPixel(dx, dy), src_pixel));
        }
    }
}

moviesource::MovieSource* mutableFileFor(const std::shared_ptr<const chunks::CastMemberChunk>& member) {
    if (member == nullptr || member->file() == nullptr) {
        return nullptr;
    }
    return const_cast<moviesource::MovieSource*>(member->file());
}

std::shared_ptr<chunks::CastMemberChunk> resolveFilmLoopMember(moviesource::MovieSource& file, int cast_lib, int member_number) {
    auto member = file.castMemberChunkByNumber(cast_lib, member_number);
    if (member == nullptr && (cast_lib == 0xFFFF || cast_lib == 0)) {
        member = file.castMemberChunkByNumber(1, member_number);
    }
    if (member == nullptr) {
        member = file.castMemberChunkByIndex(cast_lib, member_number);
        if (member == nullptr && (cast_lib == 0xFFFF || cast_lib == 0)) {
            member = file.castMemberChunkByIndex(1, member_number);
        }
    }
    return member;
}

int directorVersionFor(const moviesource::MovieSource& file) {
    return file.config() != nullptr ? file.config()->director_version : 1200;
}

std::string defaultStxtFontName(const moviesource::MovieSource& file) {
    const int version = directorVersionFor(file);
    return version > 0 && version <= 1600 ? "Geneva" : "Arial";
}

bool usesLegacyEmbeddedTextFont(const moviesource::MovieSource& file, int font_id, int font_style) {
    const int version = directorVersionFor(file);
    return font_id == 0 && (font_style & 0x80) != 0 && version > 0 && version <= 1600;
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool usesDirectorPixelStyledFont(const ::libreshockwave::cast::XmedStyledText& styled) {
    const auto font_name = lowerAscii(styled.font_name);
    if (font_name.find("bold") != std::string::npos) {
        return false;
    }
    if (font_name.find("volter") != std::string::npos) {
        return true;
    }
    const auto preferred = ::libreshockwave::player::cast::FontRegistry::getPreferredDirectorPixelFont();
    return preferred.has_value() && !preferred->empty() &&
           font_name.find(lowerAscii(*preferred)) != std::string::npos;
}

std::string textAlignment(int text_align) {
    switch (text_align) {
        case 1: return "center";
        case -1: return "right";
        default: return "left";
    }
}

std::string fontStyleString(int font_style, bool force_bold = false) {
    std::string style;
    if ((font_style & 1) != 0 || force_bold) {
        style += "bold";
    }
    if ((font_style & 2) != 0) {
        if (!style.empty()) style += ",";
        style += "italic";
    }
    if ((font_style & 4) != 0) {
        if (!style.empty()) style += ",";
        style += "underline";
    }
    return style;
}

int argbRgb(int rgb) {
    return static_cast<int>(0xFF000000U | (static_cast<std::uint32_t>(rgb) & 0x00FFFFFFU));
}

int runTextColor(int r, int g, int b) {
    if (r >= 0 && g >= 0 && b >= 0) {
        return argbRgb((r << 16) | (g << 8) | b);
    }
    return argbRgb(0);
}

bool shouldUseSpriteForeColorForFileText(const RenderSprite& sprite, int run_color) {
    if (!sprite.hasForeColor()) {
        return false;
    }
    const int sprite_color = argbRgb(sprite.foreColor());
    if (sprite.inkMode() == id::InkMode::BACKGROUND_TRANSPARENT &&
        (sprite_color & 0x00FFFFFF) == 0 &&
        (run_color & 0x00FFFFFF) == 0x00FFFFFF) {
        return false;
    }
    if ((sprite_color & 0x00FFFFFF) != 0x00FFFFFF) {
        return true;
    }
    return (run_color & 0x00FFFFFF) == 0x00FFFFFF;
}

bool shouldUseSpriteForeColorForStyledText(const RenderSprite& sprite,
                                           const ::libreshockwave::cast::XmedStyledText& styled) {
    if (sprite.inkMode() != id::InkMode::BACKGROUND_TRANSPARENT) {
        return false;
    }
    const std::uint32_t styled_text_color = styled.textColorARGB();
    if ((styled_text_color & 0x00FFFFFFU) != 0x00FFFFFFU) {
        return false;
    }
    const int sprite_color = argbRgb(sprite.foreColor());
    if ((sprite_color & 0x00FFFFFF) == 0) {
        return usesDirectorPixelStyledFont(styled);
    }
    if (!sprite.hasForeColor()) {
        return false;
    }
    return (sprite_color & 0x00FFFFFF) != 0x00FFFFFF;
}

bool isTransparentTextInk(const RenderSprite& sprite) {
    return sprite.inkMode() == id::InkMode::BACKGROUND_TRANSPARENT ||
           sprite.inkMode() == id::InkMode::MATTE;
}

std::shared_ptr<bitmap::Bitmap> insetTextBitmap(std::shared_ptr<bitmap::Bitmap> source,
                                                int width,
                                                int height,
                                                int inset_x,
                                                int bg_color) {
    if (source == nullptr || inset_x <= 0 || source->width() == width) {
        return source;
    }

    bitmap::Bitmap bitmap(width, height, source->bitDepth());
    bitmap.fill(static_cast<std::uint32_t>(bg_color));
    const int copy_width = std::min(source->width(), std::max(0, width - inset_x));
    const int copy_height = std::min(source->height(), height);
    for (int y = 0; y < copy_height; ++y) {
        for (int x = 0; x < copy_width; ++x) {
            bitmap.setPixel(x + inset_x, y, source->getPixel(x, y));
        }
    }
    bitmap.markScriptModified();
    if (source->isNativeAlpha()) {
        bitmap.setNativeAlpha(true);
    }
    return std::make_shared<bitmap::Bitmap>(std::move(bitmap));
}

int bottomTransparentRows(const bitmap::Bitmap& source) {
    int rows = 0;
    for (int y = source.height() - 1; y >= 0; --y) {
        bool row_transparent = true;
        for (int x = 0; x < source.width(); ++x) {
            if (((source.getPixel(x, y) >> 24) & 0xFFU) != 0) {
                row_transparent = false;
                break;
            }
        }
        if (!row_transparent) {
            break;
        }
        ++rows;
    }
    return rows;
}

std::shared_ptr<bitmap::Bitmap> shiftBitmapDown(std::shared_ptr<bitmap::Bitmap> source, int dy, int bg_color) {
    if (source == nullptr || dy <= 0) {
        return source;
    }
    const int safe_dy = std::min(dy, bottomTransparentRows(*source));
    if (safe_dy <= 0) {
        return source;
    }

    bitmap::Bitmap shifted(source->width(), source->height(), source->bitDepth());
    shifted.fill(static_cast<std::uint32_t>(bg_color));
    for (int y = 0; y < source->height() - safe_dy; ++y) {
        for (int x = 0; x < source->width(); ++x) {
            shifted.setPixel(x, y + safe_dy, source->getPixel(x, y));
        }
    }
    shifted.markScriptModified();
    if (source->isNativeAlpha()) {
        shifted.setNativeAlpha(true);
    }
    return std::make_shared<bitmap::Bitmap>(std::move(shifted));
}

struct ProjectedW3DVertex {
    float x = 0.0F;
    float y = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
    bool has_tex_coord = false;
};

struct W3DProjection {
    float min_x = 0.0F;
    float min_y = 0.0F;
    float max_x = 0.0F;
    float max_y = 0.0F;
    float scale = 1.0F;
    float offset_x = 0.0F;
    float offset_y = 0.0F;
    int margin = 0;
    bool valid = false;
};

::libreshockwave::w3d::W3DVertex transformW3DVertex(
    const ::libreshockwave::w3d::W3DVertex& vertex,
    const std::array<float, 16>& transform) {
    return ::libreshockwave::w3d::W3DVertex{
        transform[0] * vertex.x + transform[4] * vertex.y + transform[8] * vertex.z + transform[12],
        transform[1] * vertex.x + transform[5] * vertex.y + transform[9] * vertex.z + transform[13],
        transform[2] * vertex.x + transform[6] * vertex.y + transform[10] * vertex.z + transform[14]
    };
}

W3DProjection buildW3DProjection(const std::vector<W3DRenderableMesh>& meshes, int width, int height) {
    W3DProjection projection;
    if (width <= 0 || height <= 0) {
        return projection;
    }

    projection.min_x = std::numeric_limits<float>::max();
    projection.min_y = std::numeric_limits<float>::max();
    projection.max_x = std::numeric_limits<float>::lowest();
    projection.max_y = std::numeric_limits<float>::lowest();

    bool saw_vertex = false;
    for (const auto& mesh : meshes) {
        for (const auto& vertex : mesh.mesh.vertices) {
            const auto world = transformW3DVertex(vertex, mesh.world_transform);
            projection.min_x = std::min(projection.min_x, world.x);
            projection.min_y = std::min(projection.min_y, world.y);
            projection.max_x = std::max(projection.max_x, world.x);
            projection.max_y = std::max(projection.max_y, world.y);
            saw_vertex = true;
        }
    }
    if (!saw_vertex) {
        return projection;
    }

    if (std::fabs(projection.max_x - projection.min_x) < 0.0001F) {
        projection.min_x -= 0.5F;
        projection.max_x += 0.5F;
    }
    if (std::fabs(projection.max_y - projection.min_y) < 0.0001F) {
        projection.min_y -= 0.5F;
        projection.max_y += 0.5F;
    }

    projection.margin = width > 8 && height > 8 ? 2 : 0;
    const float drawable_width = static_cast<float>(std::max(1, width - 1 - projection.margin * 2));
    const float drawable_height = static_cast<float>(std::max(1, height - 1 - projection.margin * 2));
    const float range_x = projection.max_x - projection.min_x;
    const float range_y = projection.max_y - projection.min_y;
    projection.scale = std::min(drawable_width / range_x, drawable_height / range_y);
    projection.offset_x = (drawable_width - range_x * projection.scale) * 0.5F;
    projection.offset_y = (drawable_height - range_y * projection.scale) * 0.5F;
    projection.valid = projection.scale > 0.0F;
    return projection;
}

ProjectedW3DVertex projectW3DVertex(const ::libreshockwave::w3d::W3DVertex& vertex,
                                    const W3DProjection& projection) {
    return ProjectedW3DVertex{
        static_cast<float>(projection.margin) + projection.offset_x + (vertex.x - projection.min_x) * projection.scale,
        static_cast<float>(projection.margin) + projection.offset_y + (projection.max_y - vertex.y) * projection.scale
    };
}

int diffuseChannel(float value) {
    return std::clamp(static_cast<int>(std::round(value * 255.0F)), 0, 255);
}

std::uint32_t w3dPaintColor(const W3DRenderableMesh& mesh, const RenderSprite& sprite) {
    if (mesh.material.has_value() && mesh.material->diffuse_color.has_value()) {
        const auto& diffuse = *mesh.material->diffuse_color;
        return (static_cast<std::uint32_t>(diffuseChannel(diffuse[3])) << 24U) |
               (static_cast<std::uint32_t>(diffuseChannel(diffuse[0])) << 16U) |
               (static_cast<std::uint32_t>(diffuseChannel(diffuse[1])) << 8U) |
               static_cast<std::uint32_t>(diffuseChannel(diffuse[2]));
    }
    const int rgb = sprite.hasForeColor() ? sprite.foreColor() : 0xB0B0B0;
    return opaqueArgb(rgb);
}

std::uint32_t multiplyArgbChannels(std::uint32_t left, std::uint32_t right) {
    const auto channel = [](std::uint32_t a, std::uint32_t b, std::uint32_t shift) {
        return (((a >> shift) & 0xFFU) * ((b >> shift) & 0xFFU)) / 0xFFU;
    };
    return (channel(left, right, 24U) << 24U) |
           (channel(left, right, 16U) << 16U) |
           (channel(left, right, 8U) << 8U) |
           channel(left, right, 0U);
}

std::uint32_t sampleW3DTexture(const bitmap::Bitmap& texture, float u, float v) {
    if (texture.width() <= 0 || texture.height() <= 0) {
        return 0xFFFFFFFFU;
    }
    const float clamped_u = std::clamp(u, 0.0F, 1.0F);
    const float clamped_v = std::clamp(v, 0.0F, 1.0F);
    const int x = std::clamp(static_cast<int>(std::round(clamped_u * static_cast<float>(texture.width() - 1))),
                             0,
                             texture.width() - 1);
    const int y = std::clamp(static_cast<int>(std::round(clamped_v * static_cast<float>(texture.height() - 1))),
                             0,
                             texture.height() - 1);
    return texture.getPixel(x, y);
}

float averageW3DWorldZ(const W3DRenderableMesh& mesh) {
    if (mesh.mesh.vertices.empty()) {
        return 0.0F;
    }

    float sum = 0.0F;
    for (const auto& vertex : mesh.mesh.vertices) {
        sum += transformW3DVertex(vertex, mesh.world_transform).z;
    }
    return sum / static_cast<float>(mesh.mesh.vertices.size());
}

float triangleEdge(const ProjectedW3DVertex& a,
                   const ProjectedW3DVertex& b,
                   float x,
                   float y) {
    return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
}

bool drawProjectedLine(bitmap::Bitmap& bitmap,
                       ProjectedW3DVertex a,
                       ProjectedW3DVertex b,
                       std::uint32_t color) {
    int x0 = static_cast<int>(std::round(a.x));
    int y0 = static_cast<int>(std::round(a.y));
    const int x1 = static_cast<int>(std::round(b.x));
    const int y1 = static_cast<int>(std::round(b.y));
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    bool painted = false;

    while (true) {
        if (x0 >= 0 && x0 < bitmap.width() && y0 >= 0 && y0 < bitmap.height()) {
            bitmap.setPixel(x0, y0, alphaComposite(bitmap.getPixel(x0, y0), color));
            painted = true;
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
    return painted;
}

bool fillProjectedTriangle(bitmap::Bitmap& bitmap,
                           ProjectedW3DVertex a,
                           ProjectedW3DVertex b,
                           ProjectedW3DVertex c,
                           std::uint32_t color,
                           const bitmap::Bitmap* texture = nullptr) {
    const float area = triangleEdge(a, b, c.x, c.y);
    if (std::fabs(area) < 0.0001F) {
        bool painted = drawProjectedLine(bitmap, a, b, color);
        painted = drawProjectedLine(bitmap, b, c, color) || painted;
        painted = drawProjectedLine(bitmap, c, a, color) || painted;
        return painted;
    }

    const int min_x = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const int max_x = std::min(bitmap.width() - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const int min_y = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const int max_y = std::min(bitmap.height() - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    if (min_x > max_x || min_y > max_y) {
        return false;
    }

    bool painted = false;
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const float px = static_cast<float>(x) + 0.5F;
            const float py = static_cast<float>(y) + 0.5F;
            const float w0 = triangleEdge(b, c, px, py);
            const float w1 = triangleEdge(c, a, px, py);
            const float w2 = triangleEdge(a, b, px, py);
            if ((w0 >= 0.0F && w1 >= 0.0F && w2 >= 0.0F) ||
                (w0 <= 0.0F && w1 <= 0.0F && w2 <= 0.0F)) {
                std::uint32_t paint = color;
                if (texture != nullptr && a.has_tex_coord && b.has_tex_coord && c.has_tex_coord) {
                    const float alpha = w0 / area;
                    const float beta = w1 / area;
                    const float gamma = w2 / area;
                    const float u = alpha * a.u + beta * b.u + gamma * c.u;
                    const float v = alpha * a.v + beta * b.v + gamma * c.v;
                    paint = multiplyArgbChannels(color, sampleW3DTexture(*texture, u, v));
                }
                bitmap.setPixel(x, y, alphaComposite(bitmap.getPixel(x, y), paint));
                painted = true;
            }
        }
    }
    return painted;
}

} // namespace

SpriteBaker::SpriteBaker(BitmapCache* bitmap_cache)
    : bitmap_cache_(bitmap_cache != nullptr ? bitmap_cache : &owned_bitmap_cache_) {
    registerDefaultSteps();
}

int SpriteBaker::tickCounter() const {
    return tick_counter_;
}

void SpriteBaker::setTickCounter(int tick) {
    tick_counter_ = tick;
}

RenderSprite SpriteBaker::bake(const RenderSprite& sprite) {
    std::shared_ptr<const bitmap::Bitmap> baked;
    for (const auto& step : bake_steps_) {
        if (step.supports && step.bake && step.supports(sprite)) {
            baked = step.bake(sprite);
            break;
        }
    }

    if (baked != nullptr &&
        (sprite.type() == SpriteType::Text || sprite.type() == SpriteType::Button) &&
        (baked->width() != sprite.width() || baked->height() != sprite.height())) {
        const int width = baked->width();
        const int height = baked->height();
        return sprite.withBakedBitmapAndSize(std::move(baked), width, height);
    }

    if (baked != nullptr &&
        sprite.type() == SpriteType::Bitmap &&
        sprite.dynamicMember() != nullptr &&
        sprite.width() <= 1 && sprite.height() <= 1 &&
        (baked->width() > 1 || baked->height() > 1)) {
        const int width = baked->width();
        const int height = baked->height();
        return sprite.withBakedBitmapAndSize(std::move(baked), width, height);
    }

    return sprite.withBakedBitmap(std::move(baked));
}

std::vector<RenderSprite> SpriteBaker::bakeSprites(const std::vector<RenderSprite>& sprites) {
    ++tick_counter_;
    std::vector<RenderSprite> result;
    result.reserve(sprites.size());
    for (const auto& sprite : sprites) {
        result.push_back(bake(sprite));
    }
    return result;
}

void SpriteBaker::registerBakeStep(SpriteBakeStep step) {
    if (!bake_steps_.empty() && bake_steps_.back().name == "unsupported") {
        bake_steps_.insert(std::prev(bake_steps_.end()), std::move(step));
        return;
    }
    bake_steps_.push_back(std::move(step));
}

const std::vector<SpriteBaker::SpriteBakeStep>& SpriteBaker::bakeSteps() const {
    return bake_steps_;
}

int SpriteBaker::bakeStepCount() const {
    return static_cast<int>(bake_steps_.size());
}

void SpriteBaker::setBitmapDecodeProvider(BitmapDecodeProvider provider) {
    bitmap_decode_provider_ = std::move(provider);
}

void SpriteBaker::setLiveBitmapProvider(LiveBitmapProvider provider) {
    live_bitmap_provider_ = std::move(provider);
}

void SpriteBaker::setPaletteVersionProvider(PaletteVersionProvider provider) {
    palette_version_provider_ = std::move(provider);
}

void SpriteBaker::setTextBakeProvider(TextBakeProvider provider) {
    text_bake_provider_ = std::move(provider);
}

void SpriteBaker::setTextRenderer(output::TextRenderer* renderer) {
    text_renderer_ = renderer;
}

void SpriteBaker::setFilmLoopBakeProvider(FilmLoopBakeProvider provider) {
    film_loop_bake_provider_ = std::move(provider);
}

void SpriteBaker::setW3DTextureDecodeProvider(W3DTextureDecodeProvider provider) {
    w3d_texture_decode_provider_ = std::move(provider);
}

BitmapCache& SpriteBaker::bitmapCache() {
    return *bitmap_cache_;
}

const BitmapCache& SpriteBaker::bitmapCache() const {
    return *bitmap_cache_;
}

void SpriteBaker::registerDefaultSteps() {
    registerBakeStep(SpriteBakeStep{
        "bitmap",
        [](const RenderSprite& sprite) { return sprite.type() == SpriteType::Bitmap; },
        [this](const RenderSprite& sprite) { return bakeBitmap(sprite); }
    });
    registerBakeStep(SpriteBakeStep{
        "text",
        [](const RenderSprite& sprite) {
            return sprite.type() == SpriteType::Text || sprite.type() == SpriteType::Button;
        },
        [this](const RenderSprite& sprite) { return bakeText(sprite); }
    });
    registerBakeStep(SpriteBakeStep{
        "shape",
        [](const RenderSprite& sprite) { return sprite.type() == SpriteType::Shape; },
        [this](const RenderSprite& sprite) { return bakeShape(sprite); }
    });
    registerBakeStep(SpriteBakeStep{
        "film-loop",
        [](const RenderSprite& sprite) { return sprite.type() == SpriteType::FilmLoop; },
        [this](const RenderSprite& sprite) { return bakeFilmLoop(sprite); }
    });
    registerBakeStep(SpriteBakeStep{
        "shockwave3d",
        [](const RenderSprite& sprite) { return sprite.type() == SpriteType::Shockwave3D; },
        [this](const RenderSprite& sprite) { return bakeShockwave3D(sprite); }
    });
    registerBakeStep(SpriteBakeStep{
        "unsupported",
        [](const RenderSprite&) { return true; },
        [](const RenderSprite&) -> std::shared_ptr<const bitmap::Bitmap> { return nullptr; }
    });
}

std::shared_ptr<const bitmap::Bitmap> SpriteBaker::bakeBitmap(const RenderSprite& sprite) {
    if (live_bitmap_provider_) {
        auto live = live_bitmap_provider_(sprite);
        const bool has_runtime_dynamic_image =
            sprite.dynamicMember() != nullptr && live != nullptr && (live->width() > 1 || live->height() > 1);
        if (live != nullptr && (live->isScriptModified() || has_runtime_dynamic_image)) {
            return std::make_shared<bitmap::Bitmap>(processLiveBitmap(*live, sprite));
        }
    }

    if (auto cached = cachedBitmap(sprite)) {
        return cached;
    }

    auto member = sprite.castMember();
    if (member == nullptr || !bitmap_decode_provider_) {
        return nullptr;
    }

    auto raw = bitmap_decode_provider_(*member, nullptr);
    if (raw == nullptr) {
        bitmap_cache_->markDecodeFailed(*member);
        return nullptr;
    }

    auto processed = std::make_shared<bitmap::Bitmap>(processDecodedBitmap(*raw, sprite));
    cacheBitmap(sprite, processed);
    return processed;
}

std::shared_ptr<const bitmap::Bitmap> SpriteBaker::bakeText(const RenderSprite& sprite) {
    if (live_bitmap_provider_) {
        auto live = live_bitmap_provider_(sprite);
        if (live != nullptr) {
            return std::make_shared<bitmap::Bitmap>(processLiveBitmap(*live, sprite));
        }
    }

    if (text_bake_provider_) {
        if (auto baked = text_bake_provider_(sprite)) {
            return baked;
        }
    }
    if (auto dynamic = bakeDynamicText(sprite)) {
        return dynamic;
    }
    return bakeFileBackedText(sprite);
}

std::shared_ptr<const bitmap::Bitmap> SpriteBaker::bakeShape(const RenderSprite& sprite) {
    const ::libreshockwave::cast::ShapeInfo* shape_info = nullptr;
    auto member = sprite.castMember();
    std::optional<::libreshockwave::cast::ShapeInfo> parsed_shape;
    if (auto dynamic_info = dynamicShapeInfo(sprite)) {
        shape_info = dynamic_info;
    } else if (member != nullptr && member->memberType() == ::libreshockwave::cast::MemberType::Shape) {
        parsed_shape = ::libreshockwave::cast::ShapeInfo::parse(member->specificData());
        shape_info = &parsed_shape.value();
    }

    auto shape = std::make_shared<bitmap::Bitmap>(drawShapeBitmap(sprite, shape_info));
    return shape;
}

std::shared_ptr<const bitmap::Bitmap> SpriteBaker::bakeFilmLoop(const RenderSprite& sprite) {
    if (auto file_backed = bakeFileBackedFilmLoop(sprite)) {
        return file_backed;
    }

    if (!film_loop_bake_provider_) {
        return nullptr;
    }

    auto loop = film_loop_bake_provider_(sprite, tick_counter_);
    if (loop == nullptr || !InkProcessor::shouldProcessInk(sprite.inkMode())) {
        return loop;
    }

    return std::make_shared<bitmap::Bitmap>(
        InkProcessor::applyInk(*loop, sprite.inkMode(), sprite.backColor(), false, loop->imagePalette().get()));
}

std::shared_ptr<const bitmap::Bitmap> SpriteBaker::bakeShockwave3D(const RenderSprite& sprite) {
    const auto member = sprite.castMember();
    auto* file = mutableFileFor(member);
    if (member == nullptr || file == nullptr) {
        return nullptr;
    }

    auto w3d = file->w3dForMember(file->castMemberAssetForChunk(std::const_pointer_cast<chunks::CastMemberChunk>(member)));
    if (!w3d.has_value()) {
        return nullptr;
    }

    auto meshes = w3d->renderableMeshes();
    if (meshes.empty()) {
        return nullptr;
    }

    const int width = sprite.width() > 0 ? sprite.width() : 64;
    const int height = sprite.height() > 0 ? sprite.height() : 64;
    if (width <= 0 || height <= 0) {
        return nullptr;
    }

    const auto projection = buildW3DProjection(meshes, width, height);
    if (!projection.valid) {
        return nullptr;
    }
    std::stable_sort(meshes.begin(), meshes.end(), [](const auto& left, const auto& right) {
        return averageW3DWorldZ(left) < averageW3DWorldZ(right);
    });

    bitmap::Bitmap bitmap(width, height, 32);
    bitmap.setNativeAlpha(true);

    bool painted = false;
    for (const auto& mesh : meshes) {
        if (mesh.mesh.vertices.empty()) {
            continue;
        }
        std::vector<ProjectedW3DVertex> projected;
        projected.reserve(mesh.mesh.vertices.size());
        const bool has_tex_coords = mesh.mesh.hasTextureCoordinates();
        for (std::size_t index = 0; index < mesh.mesh.vertices.size(); ++index) {
            auto projected_vertex = projectW3DVertex(
                transformW3DVertex(mesh.mesh.vertices[index], mesh.world_transform),
                projection);
            if (has_tex_coords) {
                projected_vertex.u = mesh.mesh.tex_coords[index].u;
                projected_vertex.v = mesh.mesh.tex_coords[index].v;
                projected_vertex.has_tex_coord = true;
            }
            projected.push_back(projected_vertex);
        }

        const auto color = w3dPaintColor(mesh, sprite);
        std::shared_ptr<const bitmap::Bitmap> texture_bitmap;
        if (has_tex_coords && mesh.texture.has_value() && w3d_texture_decode_provider_) {
            texture_bitmap = w3d_texture_decode_provider_(*mesh.texture);
            if (texture_bitmap != nullptr &&
                (texture_bitmap->width() <= 0 || texture_bitmap->height() <= 0)) {
                texture_bitmap.reset();
            }
        }
        for (const auto& face : mesh.mesh.faces) {
            if (face.a < 0 || face.b < 0 || face.c < 0 ||
                face.a >= static_cast<int>(projected.size()) ||
                face.b >= static_cast<int>(projected.size()) ||
                face.c >= static_cast<int>(projected.size())) {
                continue;
            }
            painted = fillProjectedTriangle(bitmap,
                                            projected[static_cast<std::size_t>(face.a)],
                                            projected[static_cast<std::size_t>(face.b)],
                                            projected[static_cast<std::size_t>(face.c)],
                                            color,
                                            texture_bitmap.get()) || painted;
        }
    }

    if (!painted) {
        return nullptr;
    }

    if (InkProcessor::shouldProcessInk(sprite.inkMode())) {
        bitmap = InkProcessor::applyInk(bitmap,
                                        sprite.inkMode(),
                                        sprite.backColor(),
                                        true,
                                        bitmap.imagePalette().get());
    }
    return std::make_shared<bitmap::Bitmap>(std::move(bitmap));
}

std::shared_ptr<const bitmap::Bitmap> SpriteBaker::bakeDynamicText(const RenderSprite& sprite) {
    const auto member = sprite.dynamicMember();
    if (member == nullptr || !member->hasDynamicText() || text_renderer_ == nullptr) {
        return nullptr;
    }

    const int width = sprite.width() > 0 ? sprite.width() : 200;
    const int height = sprite.height() > 0 ? sprite.height() : 20;
    const int bg_color = isTransparentTextInk(sprite) ? 0 : argbRgb(sprite.backColor());
    const int text_color = member->textColor();

    auto text_image = text_renderer_->renderText(member->textContent(),
                                               width,
                                               height,
                                               member->textFont(),
                                               member->textFontSize(),
                                               member->textFontStyle(),
                                               member->textAlignment(),
                                               text_color,
                                               bg_color,
                                               member->textWordWrap(),
                                               member->textAntialias(),
                                               member->textFixedLineSpace(),
                                               member->textTopSpacing());
    if (text_image == nullptr) {
        return nullptr;
    }

    if (((static_cast<std::uint32_t>(bg_color) >> 24U) & 0xFFU) < 0xFFU || text_image->hasTransparentPixels()) {
        text_image->setNativeAlpha(true);
    }
    if (isTransparentTextInk(sprite)) {
        return text_image;
    }
    if (InkProcessor::shouldProcessInk(sprite.inkMode())) {
        return std::make_shared<bitmap::Bitmap>(
            InkProcessor::applyInk(*text_image, sprite.inkMode(), sprite.backColor(), false, nullptr));
    }
    return text_image;
}

std::shared_ptr<const bitmap::Bitmap> SpriteBaker::bakeFileBackedText(const RenderSprite& sprite) {
    const auto member = sprite.castMember();
    auto* file = mutableFileFor(member);
    if (member == nullptr || file == nullptr || text_renderer_ == nullptr) {
        return nullptr;
    }

    if (member->isTextXtra()) {
        auto styled = file->xmedStyledTextForMember(file->castMemberAssetForChunk(std::const_pointer_cast<chunks::CastMemberChunk>(member)));
        if (!styled.has_value() || styled->text.empty()) {
            return nullptr;
        }
        const int width = styled->width > 0 ? styled->width : (sprite.width() > 0 ? sprite.width() : 200);
        const int height = styled->height > 0 ? styled->height : (sprite.height() > 0 ? sprite.height() : 20);
        const auto styled_text_color = styled->textColorARGB();
        int text_color = shouldUseSpriteForeColorForStyledText(sprite, *styled)
            ? argbRgb(sprite.foreColor())
            : static_cast<int>(styled_text_color);
        const int bg_color = isTransparentTextInk(sprite) ? 0 : argbRgb(sprite.backColor());
        auto text_image = text_renderer_->renderXmedText(&*styled, width, height, text_color, bg_color);
        if (text_image == nullptr || isTransparentTextInk(sprite)) {
            return text_image;
        }
        if (InkProcessor::shouldProcessInk(sprite.inkMode())) {
            return std::make_shared<bitmap::Bitmap>(
                InkProcessor::applyInk(*text_image, sprite.inkMode(), sprite.backColor(), false, nullptr));
        }
        return text_image;
    }

    auto text_chunk = file->textForMember(file->castMemberAssetForChunk(std::const_pointer_cast<chunks::CastMemberChunk>(member)));
    if (text_chunk == nullptr) {
        return nullptr;
    }

    const auto text_info = ::libreshockwave::cast::TextInfo::parse(member->specificData());
    std::string font_name = defaultStxtFontName(*file);
    int font_size = 12;
    int font_style = 0;
    bool legacy_embedded_text_font = false;
    int run_r = -1;
    int run_g = -1;
    int run_b = -1;
    if (!text_chunk->runs.empty()) {
        const auto& run = text_chunk->runs.front();
        if (auto mapped = file->fontNameForId(run.font_id); mapped.has_value() && !mapped->empty()) {
            font_name = *mapped;
        } else if (usesLegacyEmbeddedTextFont(*file, run.font_id, run.font_style)) {
            if (auto embedded = ::libreshockwave::player::cast::FontRegistry::getPreferredDirectorPixelFont();
                embedded.has_value() && !embedded->empty()) {
                font_name = *embedded;
                legacy_embedded_text_font = true;
            }
        }
        font_size = run.font_size;
        font_style = run.font_style;
        run_r = run.color_r;
        run_g = run.color_g;
        run_b = run.color_b;
    }

    const int width = text_info.width > 0 ? text_info.width : (sprite.width() > 0 ? sprite.width() : 200);
    const int height = text_info.height > 0 ? text_info.height : (sprite.height() > 0 ? sprite.height() : 20);
    const int run_color = runTextColor(run_r, run_g, run_b);
    int text_color = shouldUseSpriteForeColorForFileText(sprite, run_color) ? argbRgb(sprite.foreColor()) : run_color;
    const int bg_color = isTransparentTextInk(sprite)
        ? 0
        : argbRgb((text_info.bg_red << 16) | (text_info.bg_green << 8) | text_info.bg_blue);
    const int horizontal_inset = std::min(text_info.gutter_size, std::max(0, width - 1));
    const int render_width = std::max(1, width - horizontal_inset * 2);
    const int fixed_line_space = text_info.text_height > font_size ? font_size : 0;
    const int top_spacing = text_info.text_height > font_size ? text_info.text_height - font_size : 0;

    const auto style = fontStyleString(font_style, legacy_embedded_text_font);
    std::shared_ptr<bitmap::Bitmap> rendered;
    if (legacy_embedded_text_font) {
        if (auto* simple_renderer = dynamic_cast<output::SimpleTextRenderer*>(text_renderer_)) {
            rendered = simple_renderer->renderLegacyStxtText(text_chunk->text,
                                                            render_width,
                                                            height,
                                                            font_name,
                                                            font_size,
                                                            style,
                                                            textAlignment(text_info.text_align),
                                                            text_color,
                                                            bg_color,
                                                            text_info.is_word_wrap,
                                                            false,
                                                            fixed_line_space,
                                                            top_spacing);
        }
    }
    if (rendered == nullptr) {
        rendered = text_renderer_->renderText(text_chunk->text,
                                             render_width,
                                             height,
                                             font_name,
                                             font_size,
                                             style,
                                             textAlignment(text_info.text_align),
                                             text_color,
                                             bg_color,
                                             text_info.is_word_wrap,
                                             false,
                                             fixed_line_space,
                                             top_spacing);
    }
    auto text_image = insetTextBitmap(std::move(rendered), width, height, horizontal_inset, bg_color);
    if (text_image != nullptr &&
        (((static_cast<std::uint32_t>(bg_color) >> 24U) & 0xFFU) < 0xFFU || text_image->hasTransparentPixels())) {
        text_image->setNativeAlpha(true);
    }
    if (text_image == nullptr || isTransparentTextInk(sprite)) {
        return text_image;
    }
    if (InkProcessor::shouldProcessInk(sprite.inkMode())) {
        return std::make_shared<bitmap::Bitmap>(
            InkProcessor::applyInk(*text_image, sprite.inkMode(), sprite.backColor(), false, nullptr));
    }
    return text_image;
}

std::shared_ptr<const bitmap::Bitmap> SpriteBaker::bakeFileBackedFilmLoop(const RenderSprite& sprite) {
    const auto member = sprite.castMember();
    auto* file = mutableFileFor(member);
    if (member == nullptr || file == nullptr) {
        return nullptr;
    }

    auto score = file->scoreForMember(file->castMemberAssetForChunk(std::const_pointer_cast<chunks::CastMemberChunk>(member)));
    if (score == nullptr || score->frame_data.frame_channel_data.empty()) {
        return nullptr;
    }

    const auto info = ::libreshockwave::cast::FilmLoopInfo::parse(member->specificData());
    const int loop_width = info.width() > 0 ? info.width() : sprite.width();
    const int loop_height = info.height() > 0 ? info.height() : sprite.height();
    if (loop_width <= 0 || loop_height <= 0) {
        return nullptr;
    }

    const int frame_count = score->frame_data.header.frame_count;
    const int target_frame = frame_count > 0 ? tick_counter_ % frame_count : 0;
    bitmap::Bitmap composite(loop_width, loop_height, 32);

    std::vector<moviesource::ScoreAsset::FrameChannelEntry> sub_sprites;
    for (const auto& entry : score->frame_data.frame_channel_data) {
        if (entry.frame_index.value() == target_frame && !entry.data.isEmpty()) {
            sub_sprites.push_back(entry);
        }
    }
    std::sort(sub_sprites.begin(), sub_sprites.end(), [](const auto& left, const auto& right) {
        return left.channel_index.value() < right.channel_index.value();
    });

    for (const auto& entry : sub_sprites) {
        const auto& data = entry.data;
        if (data.sprite_type == 0 || data.cast_member <= 0) {
            continue;
        }

        auto sub_member = resolveFilmLoopMember(*file, data.cast_lib, data.cast_member);
        if (sub_member == nullptr) {
            continue;
        }

        auto sub_bitmap = bakeFilmLoopMemberBitmap(sub_member, data);
        if (sub_bitmap == nullptr || sub_bitmap->width() <= 0 || sub_bitmap->height() <= 0) {
            continue;
        }

        int x = data.pos_x;
        int y = data.pos_y;
        if (sub_member->isBitmap() && sub_member->specificData().size() >= 10) {
            const auto bitmap_info = ::libreshockwave::cast::BitmapInfo::parse(
                sub_member->specificData(),
                directorVersionFor(*file));
            x -= bitmap_info.regXLocal();
            y -= bitmap_info.regYLocal();
        } else {
            x -= sub_member->regPointX();
            y -= sub_member->regPointY();
        }

        blitOnto(composite, *sub_bitmap, x - info.rect_left, y - info.rect_top);
    }

    if (InkProcessor::shouldProcessInk(sprite.inkMode())) {
        composite = InkProcessor::applyInk(composite,
                                           sprite.inkMode(),
                                           sprite.backColor(),
                                           false,
                                           composite.imagePalette().get());
    }
    return std::make_shared<bitmap::Bitmap>(std::move(composite));
}

std::shared_ptr<const bitmap::Bitmap> SpriteBaker::bakeFilmLoopMemberBitmap(
    const std::shared_ptr<chunks::CastMemberChunk>& member,
    const moviesource::ScoreAsset::ChannelData& data) {
    if (member == nullptr) {
        return nullptr;
    }

    RenderSprite sub_sprite(0,
                           0,
                           0,
                           data.width,
                           data.height,
                           0,
                           true,
                           SpriteType::Bitmap,
                           member,
                           nullptr,
                           0,
                           data.resolvedBackColor(),
                           false,
                           false,
                           data.ink,
                           100,
                           data.isFlipH(),
                           data.isFlipV(),
                           nullptr,
                           false);
    if (auto cached = cachedBitmap(sub_sprite)) {
        return cached;
    }
    if (!bitmap_decode_provider_) {
        return nullptr;
    }

    auto raw = bitmap_decode_provider_(*member, nullptr);
    if (raw == nullptr) {
        bitmap_cache_->markDecodeFailed(*member);
        return nullptr;
    }

    auto processed = std::make_shared<bitmap::Bitmap>(processDecodedBitmap(*raw, sub_sprite));
    cacheBitmap(sub_sprite, processed);
    return processed;
}

bitmap::Bitmap SpriteBaker::processDecodedBitmap(const bitmap::Bitmap& raw, const RenderSprite& sprite) const {
    // Director's 32-bit bitmap records can carry RGB data with an all-zero
    // alpha byte even when the member did not opt into native alpha.  That
    // byte is padding in that case, not transparency.  Normalize it before
    // applying ink so the decoded pixels are composited with their authored
    // RGB values instead of the stage background.
    const auto normalized = BitmapCache::coerceNonNativeAlphaToOpaque(raw, raw.isNativeAlpha());
    bitmap::Bitmap processed = shouldNeutralizeOpaqueWhiteForAuthoredTint(sprite, normalized)
        ? InkProcessor::convertOpaqueWhiteToTransparent(normalized)
        : normalized;
    if (processed.bitDepth() <= 1 && sprite.hasForeColor() && InkProcessor::allowsColorize(sprite.inkMode())) {
        processed = InkProcessor::applyForeColorRemap(processed,
                                                      static_cast<std::uint32_t>(sprite.foreColor()),
                                                      static_cast<std::uint32_t>(sprite.backColor()));
    }
    if (InkProcessor::shouldProcessInk(sprite.inkMode())) {
        if (sprite.inkMode() == id::InkMode::MASK) {
            if (const auto mask = decodeMaskBitmap(sprite)) {
                processed = InkProcessor::applyMask(processed, *mask);
            } else {
                processed = InkProcessor::applyInk(processed,
                                                   sprite.inkMode(),
                                                   sprite.backColor(),
                                                   processed.isNativeAlpha(),
                                                   raw.imagePalette().get());
            }
        } else {
            processed = InkProcessor::applyInk(processed,
                                               sprite.inkMode(),
                                               sprite.backColor(),
                                               processed.isNativeAlpha(),
                                               raw.imagePalette().get());
        }
    }

    return BitmapCache::applyIndexedMatteColorRemapIfNeeded(&raw,
                                                            processed,
                                                            sprite.ink(),
                                                            sprite.foreColor(),
                                                            sprite.backColor(),
                                                            sprite.hasForeColor(),
                                                            sprite.hasBackColor(),
                                                            raw.imagePalette().get());
}

bitmap::Bitmap SpriteBaker::processLiveBitmap(const bitmap::Bitmap& live, const RenderSprite& sprite) const {
    bitmap::Bitmap source = live.copy();
    if (source.bitDepth() <= 1 && sprite.hasForeColor()) {
        source = InkProcessor::applyForeColorRemap(source,
                                                   static_cast<std::uint32_t>(sprite.foreColor()),
                                                   static_cast<std::uint32_t>(sprite.backColor()));
    }

    bitmap::Bitmap processed = source.copy();
    if (InkProcessor::shouldProcessInk(sprite.inkMode()) &&
        !shouldPreserveScriptFillBacking(sprite, source)) {
        bitmap::Bitmap ink_source =
            (shouldNeutralizeOpaqueWhiteForScriptCanvas(sprite, source) ||
             shouldNeutralizeOpaqueWhiteForAuthoredTint(sprite, source))
            ? InkProcessor::convertOpaqueWhiteToTransparent(source)
            : source.copy();
        const bool has_native_alpha = ink_source.bitDepth() == 32 && ink_source.isNativeAlpha();
        if (sprite.inkMode() == id::InkMode::MASK) {
            if (const auto mask = decodeMaskBitmap(sprite)) {
                processed = InkProcessor::applyMask(ink_source, *mask);
            } else {
                processed = InkProcessor::applyInk(ink_source,
                                                   sprite.inkMode(),
                                                   sprite.backColor(),
                                                   has_native_alpha,
                                                   ink_source.imagePalette().get());
            }
        } else {
            processed = shouldPreserveOutlinedWhiteBodyForScriptCanvas(sprite, ink_source)
                ? InkProcessor::applyInkPreservingOutlinedWhiteBody(ink_source,
                                                                    sprite.inkMode(),
                                                                    sprite.backColor(),
                                                                    has_native_alpha,
                                                                    ink_source.imagePalette().get())
                : InkProcessor::applyInk(ink_source,
                                         sprite.inkMode(),
                                         sprite.backColor(),
                                         has_native_alpha,
                                         ink_source.imagePalette().get());
        }
    }

    bitmap::Bitmap result = BitmapCache::applyIndexedMatteColorRemapIfNeeded(&source,
                                                                              processed,
                                                                              sprite.ink(),
                                                                              sprite.foreColor(),
                                                                              sprite.backColor(),
                                                                              sprite.hasForeColor(),
                                                                              sprite.hasBackColor(),
                                                                              source.imagePalette().get());
    if (sprite.hasBackColor() &&
        InkProcessor::allowsColorize(sprite.inkMode()) &&
        (sprite.backColor() & 0x00FFFFFF) != 0x00FFFFFF) {
        result = InkProcessor::remapExactColor(result, 0x00FFFFFFU, static_cast<std::uint32_t>(sprite.backColor()));
    }
    return result;
}

std::shared_ptr<const chunks::CastMemberChunk> SpriteBaker::resolveMaskMember(const RenderSprite& sprite) const {
    const auto dynamic_member = sprite.dynamicMember();
    if (dynamic_member != nullptr && dynamic_member->rawChunk() != nullptr) {
        auto* file = mutableFileFor(dynamic_member->rawChunk());
        if (file != nullptr) {
            auto mask = file->castMemberChunkByNumber(dynamic_member->castLib(), dynamic_member->memberNum() + 1);
            if (mask == nullptr && dynamic_member->castLib() != 1) {
                mask = file->castMemberChunkByNumber(1, dynamic_member->memberNum() + 1);
            }
            if (mask != nullptr && mask->isBitmap()) {
                return mask;
            }
        }
    }

    const auto source_member = sprite.castMember();
    if (source_member == nullptr || source_member->file() == nullptr) {
        return nullptr;
    }

    auto* file = mutableFileFor(source_member);
    const auto cast_list = file->castList();
    for (int cast_lib = 1; cast_lib <= static_cast<int>(file->castChunks().size()); ++cast_lib) {
        const auto cast = file->mappedCastChunk(cast_lib);
        if (cast == nullptr) {
            continue;
        }

        const auto& member_ids = cast->memberIds();
        const auto source_id = source_member->id().value();
        const auto source_it = std::find(member_ids.begin(), member_ids.end(), source_id);
        if (source_it == member_ids.end()) {
            continue;
        }

        const auto source_index = static_cast<int>(std::distance(member_ids.begin(), source_it));
        if (source_index + 1 >= static_cast<int>(member_ids.size())) {
            return nullptr;
        }

        int min_member = file->config() != nullptr ? file->config()->min_member : 1;
        if (cast_list != nullptr && cast_lib - 1 < static_cast<int>(cast_list->entries.size())) {
            min_member = cast_list->entries[static_cast<std::size_t>(cast_lib - 1)].min_member;
        }
        auto mask = file->castMemberChunkByNumber(cast_lib, min_member + source_index + 1);
        if (mask != nullptr && mask->isBitmap()) {
            return mask;
        }
        return nullptr;
    }
    return nullptr;
}

std::shared_ptr<const bitmap::Bitmap> SpriteBaker::decodeMaskBitmap(const RenderSprite& sprite) const {
    if (!bitmap_decode_provider_) {
        return nullptr;
    }
    const auto mask_member = resolveMaskMember(sprite);
    if (mask_member == nullptr) {
        return nullptr;
    }
    return bitmap_decode_provider_(*mask_member, nullptr);
}

std::shared_ptr<const bitmap::Bitmap> SpriteBaker::cachedBitmap(const RenderSprite& sprite) const {
    auto member = sprite.castMember();
    if (member == nullptr) {
        return nullptr;
    }
    if (palette_version_provider_) {
        if (const auto version = palette_version_provider_(sprite)) {
            (void)bitmap_cache_->invalidateIfPaletteChanged(*member, *version);
        }
    }
    return bitmap_cache_->getCachedProcessed(*member,
                                            sprite.ink(),
                                            sprite.backColor(),
                                            sprite.foreColor(),
                                            sprite.hasForeColor(),
                                            sprite.hasBackColor());
}

void SpriteBaker::cacheBitmap(const RenderSprite& sprite, std::shared_ptr<const bitmap::Bitmap> bitmap) {
    auto member = sprite.castMember();
    if (member == nullptr || bitmap == nullptr) {
        return;
    }
    bitmap_cache_->putProcessed(*member,
                               sprite.ink(),
                               sprite.backColor(),
                               sprite.foreColor(),
                               sprite.hasForeColor(),
                               sprite.hasBackColor(),
                               std::move(bitmap));
}

bitmap::Bitmap SpriteBaker::drawShapeBitmap(const RenderSprite& sprite,
                                            const ::libreshockwave::cast::ShapeInfo* shape_info) {
    const int width = sprite.width() > 0 ? sprite.width() : 50;
    const int height = sprite.height() > 0 ? sprite.height() : 50;
    bitmap::Bitmap bitmap(width, height, 32);

    if (shape_info == nullptr) {
        fillSolidShape(bitmap, sprite);
    } else {
        drawAuthoredShape(bitmap, sprite, *shape_info);
    }

    if (InkProcessor::shouldProcessInk(sprite.inkMode())) {
        return InkProcessor::applyInk(bitmap, sprite.inkMode(), sprite.backColor(), false, nullptr);
    }
    return bitmap;
}

void SpriteBaker::drawAuthoredShape(bitmap::Bitmap& bitmap,
                                    const RenderSprite& sprite,
                                    const ::libreshockwave::cast::ShapeInfo& shape_info) {
    const int border_strokes = shapeBorderStrokeCount(sprite, shape_info);
    if (!shape_info.isFilled() &&
        shape_info.shape_type != ::libreshockwave::cast::ShapeType::Line &&
        border_strokes <= 0) {
        return;
    }

    switch (shape_info.shape_type) {
        case ::libreshockwave::cast::ShapeType::Rect:
        case ::libreshockwave::cast::ShapeType::OvalRect:
            if (shape_info.isFilled()) {
                fillSolidShape(bitmap, sprite);
            } else {
                for (int i = 0; i < border_strokes; ++i) {
                    drawRect(bitmap, i, i, bitmap.width() - (i * 2), bitmap.height() - (i * 2), sprite);
                }
            }
            break;
        case ::libreshockwave::cast::ShapeType::Oval:
            if (shape_info.isFilled()) {
                drawOval(bitmap,
                         bitmap.width() / 2,
                         bitmap.height() / 2,
                         std::max(0, bitmap.width() / 2),
                         std::max(0, bitmap.height() / 2),
                         sprite,
                         true);
            } else {
                for (int i = 0; i < border_strokes; ++i) {
                    drawOval(bitmap,
                             bitmap.width() / 2,
                             bitmap.height() / 2,
                             std::max(0, bitmap.width() / 2 - i),
                             std::max(0, bitmap.height() / 2 - i),
                             sprite,
                             false);
                }
            }
            break;
        case ::libreshockwave::cast::ShapeType::Line: {
            const int strokes = shapeLineStrokeCount(sprite, shape_info);
            if (strokes <= 0) {
                return;
            }
            const bool bottom_to_top = shape_info.line_direction == 6;
            const int start_y = bottom_to_top ? bitmap.height() - 1 : 0;
            const int end_y = bottom_to_top ? 0 : bitmap.height() - 1;
            for (int i = 0; i < strokes; ++i) {
                drawLine(bitmap,
                         0,
                         std::clamp(start_y - i, 0, std::max(0, bitmap.height() - 1)),
                        std::max(0, bitmap.width() - 1),
                        std::clamp(end_y - i, 0, std::max(0, bitmap.height() - 1)),
                         sprite);
            }
            break;
        }
        case ::libreshockwave::cast::ShapeType::Unknown:
            fillSolidShape(bitmap, sprite);
            break;
    }
}

void SpriteBaker::fillSolidShape(bitmap::Bitmap& bitmap, const RenderSprite& sprite) {
    if (normalizedShapePattern(sprite) <= 1) {
        bitmap.fill(opaqueArgb(sprite.foreColor()));
        return;
    }
    for (int y = 0; y < bitmap.height(); ++y) {
        for (int x = 0; x < bitmap.width(); ++x) {
            paintShapePixel(bitmap, sprite, x, y);
        }
    }
}

void SpriteBaker::drawRect(bitmap::Bitmap& bitmap, int x, int y, int width, int height, const RenderSprite& sprite) {
    if (width <= 0 || height <= 0) {
        return;
    }
    for (int px = x; px < x + width; ++px) {
        paintShapePixel(bitmap, sprite, px, y);
        paintShapePixel(bitmap, sprite, px, y + height - 1);
    }
    for (int py = y; py < y + height; ++py) {
        paintShapePixel(bitmap, sprite, x, py);
        paintShapePixel(bitmap, sprite, x + width - 1, py);
    }
}

void SpriteBaker::drawOval(bitmap::Bitmap& bitmap,
                           int cx,
                           int cy,
                           int rx,
                           int ry,
                           const RenderSprite& sprite,
                           bool filled) {
    if (rx <= 0 || ry <= 0) {
        return;
    }

    for (int y = 0; y < bitmap.height(); ++y) {
        for (int x = 0; x < bitmap.width(); ++x) {
            const double nx = (static_cast<double>(x) + 0.5 - static_cast<double>(cx)) / static_cast<double>(rx);
            const double ny = (static_cast<double>(y) + 0.5 - static_cast<double>(cy)) / static_cast<double>(ry);
            const double value = (nx * nx) + (ny * ny);
            if ((filled && value <= 1.0) || (!filled && value >= 0.75 && value <= 1.15)) {
                paintShapePixel(bitmap, sprite, x, y);
            }
        }
    }
}

void SpriteBaker::drawLine(bitmap::Bitmap& bitmap, int x0, int y0, int x1, int y1, const RenderSprite& sprite) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (true) {
        paintShapePixel(bitmap, sprite, x0, y0);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * error;
        if (e2 >= dy) {
            error += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

} // namespace libreshockwave::player::render::pipeline
