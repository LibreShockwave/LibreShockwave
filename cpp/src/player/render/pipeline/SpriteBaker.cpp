#include "libreshockwave/player/render/pipeline/SpriteBaker.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/cast/BitmapInfo.hpp"
#include "libreshockwave/cast/CastMember.hpp"
#include "libreshockwave/cast/FilmLoopInfo.hpp"
#include "libreshockwave/cast/ShapeInfo.hpp"
#include "libreshockwave/cast/TextInfo.hpp"
#include "libreshockwave/chunks/ConfigChunk.hpp"
#include "libreshockwave/chunks/ScoreChunk.hpp"
#include "libreshockwave/chunks/TextChunk.hpp"
#include "libreshockwave/id/Ids.hpp"
#include "libreshockwave/player/cast/FontRegistry.hpp"
#include "libreshockwave/player/render/output/SimpleTextRenderer.hpp"
#include "libreshockwave/player/render/output/TextRenderer.hpp"
#include "libreshockwave/player/render/pipeline/InkProcessor.hpp"

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

    bool hasWhiteEdge = false;
    bool hasNonWhiteContent = false;
    const auto visit = [&](int x, int y, bool edge) {
        const auto pixel = bitmap.getPixel(x, y);
        if (((pixel >> 24U) & 0xFFU) == 0) {
            return;
        }
        if ((pixel & 0x00FFFFFFU) == 0x00FFFFFFU) {
            hasWhiteEdge = hasWhiteEdge || edge;
        } else {
            hasNonWhiteContent = true;
        }
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            visit(x, y, x == 0 || y == 0 || x == width - 1 || y == height - 1);
        }
    }
    return hasWhiteEdge && hasNonWhiteContent;
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

    const auto memberName = sprite.memberName();
    return memberName.has_value() &&
           (memberName->starts_with("chat_item_background_") ||
            memberName->starts_with("chat_item_sing_background_"));
}

bool shouldPreserveTallRuntimeBacking(const RenderSprite& sprite, const bitmap::Bitmap& bitmap) {
    if (sprite.inkMode() != id::InkMode::BACKGROUND_TRANSPARENT ||
        !bitmap.isScriptModified() ||
        bitmap.isNativeAlpha()) {
        return false;
    }

    const int width = bitmap.width();
    const int height = bitmap.height();
    const int area = width * height;
    if (width <= 0 || height <= 0 || area < 4096 || width < 160 || height < 180) {
        return false;
    }
    if (width > 512 || area > 131072) {
        return false;
    }

    if (bitmap.isRectangularMedia()) {
        return true;
    }

    if (bitmap.hasTransparentPixels()) {
        return false;
    }

    int whitePixels = 0;
    int opaquePixels = 0;
    for (const auto pixel : bitmap.pixels()) {
        if (((pixel >> 24U) & 0xFFU) == 0) {
            continue;
        }
        ++opaquePixels;
        if ((pixel & 0x00FFFFFFU) == 0x00FFFFFFU) {
            ++whitePixels;
        }
    }
    return opaquePixels == area && whitePixels * 4 >= opaquePixels * 3;
}

int shapeBorderStrokeCount(const RenderSprite& sprite, const ::libreshockwave::cast::ShapeInfo& shapeInfo) {
    if (const auto lineSize = sprite.shapeLineSize()) {
        return std::max(0, *lineSize);
    }
    if (const auto dynamic = sprite.dynamicMember();
        dynamic != nullptr && dynamic->isShape() && dynamic->hasRuntimeShapeLineSize()) {
        return std::max(0, dynamic->shapeLineSize());
    }
    return std::max(0, shapeInfo.lineThickness - 1);
}

int shapeLineStrokeCount(const RenderSprite& sprite, const ::libreshockwave::cast::ShapeInfo& shapeInfo) {
    if (const auto lineSize = sprite.shapeLineSize()) {
        return std::max(0, *lineSize);
    }
    if (const auto dynamic = sprite.dynamicMember();
        dynamic != nullptr && dynamic->isShape() && dynamic->hasRuntimeShapeLineSize()) {
        return std::max(0, dynamic->shapeLineSize());
    }
    return std::max(1, shapeInfo.lineThickness);
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
    const int srcA = channel(src, 24);
    if (srcA <= 0) {
        return dst;
    }
    if (srcA >= 255) {
        return src | 0xFF000000U;
    }

    const int dstA = channel(dst, 24);
    const int invA = 255 - srcA;
    const int outA = srcA + (dstA * invA / 255);
    if (outA <= 0) {
        return 0;
    }

    const int outR = (channel(src, 16) * srcA + channel(dst, 16) * dstA * invA / 255) / outA;
    const int outG = (channel(src, 8) * srcA + channel(dst, 8) * dstA * invA / 255) / outA;
    const int outB = (channel(src, 0) * srcA + channel(dst, 0) * dstA * invA / 255) / outA;
    return (static_cast<std::uint32_t>(outA & 0xFF) << 24) |
           (static_cast<std::uint32_t>(outR & 0xFF) << 16) |
           (static_cast<std::uint32_t>(outG & 0xFF) << 8) |
           static_cast<std::uint32_t>(outB & 0xFF);
}

void blitOnto(bitmap::Bitmap& dst, const bitmap::Bitmap& src, int offsetX, int offsetY) {
    for (int y = 0; y < src.height(); ++y) {
        const int dy = offsetY + y;
        if (dy < 0 || dy >= dst.height()) {
            continue;
        }
        for (int x = 0; x < src.width(); ++x) {
            const int dx = offsetX + x;
            if (dx < 0 || dx >= dst.width()) {
                continue;
            }
            const auto srcPixel = src.getPixel(x, y);
            if (channel(srcPixel, 24) == 0) {
                continue;
            }
            dst.setPixel(dx, dy, alphaComposite(dst.getPixel(dx, dy), srcPixel));
        }
    }
}

DirectorFile* mutableFileFor(const std::shared_ptr<const chunks::CastMemberChunk>& member) {
    if (member == nullptr || member->file() == nullptr) {
        return nullptr;
    }
    return const_cast<DirectorFile*>(member->file());
}

std::shared_ptr<chunks::CastMemberChunk> resolveFilmLoopMember(DirectorFile& file, int castLib, int memberNumber) {
    auto member = file.getCastMemberByNumber(castLib, memberNumber);
    if (member == nullptr && (castLib == 0xFFFF || castLib == 0)) {
        member = file.getCastMemberByNumber(1, memberNumber);
    }
    if (member == nullptr) {
        member = file.getCastMemberByIndex(castLib, memberNumber);
        if (member == nullptr && (castLib == 0xFFFF || castLib == 0)) {
            member = file.getCastMemberByIndex(1, memberNumber);
        }
    }
    return member;
}

int directorVersionFor(const DirectorFile& file) {
    return file.config() != nullptr ? file.config()->directorVersion() : 1200;
}

std::string defaultStxtFontName(const DirectorFile& file) {
    const int version = directorVersionFor(file);
    return version > 0 && version <= 1600 ? "Geneva" : "Arial";
}

bool usesLegacyEmbeddedTextFont(const DirectorFile& file, int fontId, int fontStyle) {
    const int version = directorVersionFor(file);
    return fontId == 0 && (fontStyle & 0x80) != 0 && version > 0 && version <= 1600;
}

std::string textAlignment(int textAlign) {
    switch (textAlign) {
        case 1: return "center";
        case -1: return "right";
        default: return "left";
    }
}

std::string fontStyleString(int fontStyle, bool forceBold = false) {
    std::string style;
    if ((fontStyle & 1) != 0 || forceBold) {
        style += "bold";
    }
    if ((fontStyle & 2) != 0) {
        if (!style.empty()) style += ",";
        style += "italic";
    }
    if ((fontStyle & 4) != 0) {
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

bool shouldUseSpriteForeColorForFileText(const RenderSprite& sprite, int runColor) {
    if (!sprite.hasForeColor()) {
        return false;
    }
    const int spriteColor = argbRgb(sprite.foreColor());
    if ((spriteColor & 0x00FFFFFF) != 0x00FFFFFF) {
        return true;
    }
    return (runColor & 0x00FFFFFF) == 0x00FFFFFF;
}

bool shouldUseSpriteForeColorForStyledText(const RenderSprite& sprite, std::uint32_t styledTextColor) {
    if (sprite.inkMode() != id::InkMode::BACKGROUND_TRANSPARENT) {
        return false;
    }
    if ((styledTextColor & 0x00FFFFFFU) != 0x00FFFFFFU) {
        return false;
    }
    if (!sprite.hasForeColor()) {
        return true;
    }
    return (argbRgb(sprite.foreColor()) & 0x00FFFFFF) != 0x00FFFFFF;
}

bool isTransparentTextInk(const RenderSprite& sprite) {
    return sprite.inkMode() == id::InkMode::BACKGROUND_TRANSPARENT ||
           sprite.inkMode() == id::InkMode::MATTE;
}

std::shared_ptr<bitmap::Bitmap> insetTextBitmap(std::shared_ptr<bitmap::Bitmap> source,
                                                int width,
                                                int height,
                                                int insetX,
                                                int bgColor) {
    if (source == nullptr || insetX <= 0 || source->width() == width) {
        return source;
    }

    bitmap::Bitmap bitmap(width, height, source->bitDepth());
    bitmap.fill(static_cast<std::uint32_t>(bgColor));
    const int copyWidth = std::min(source->width(), std::max(0, width - insetX));
    const int copyHeight = std::min(source->height(), height);
    for (int y = 0; y < copyHeight; ++y) {
        for (int x = 0; x < copyWidth; ++x) {
            bitmap.setPixel(x + insetX, y, source->getPixel(x, y));
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
        bool rowTransparent = true;
        for (int x = 0; x < source.width(); ++x) {
            if (((source.getPixel(x, y) >> 24) & 0xFFU) != 0) {
                rowTransparent = false;
                break;
            }
        }
        if (!rowTransparent) {
            break;
        }
        ++rows;
    }
    return rows;
}

std::shared_ptr<bitmap::Bitmap> shiftBitmapDown(std::shared_ptr<bitmap::Bitmap> source, int dy, int bgColor) {
    if (source == nullptr || dy <= 0) {
        return source;
    }
    const int safeDy = std::min(dy, bottomTransparentRows(*source));
    if (safeDy <= 0) {
        return source;
    }

    bitmap::Bitmap shifted(source->width(), source->height(), source->bitDepth());
    shifted.fill(static_cast<std::uint32_t>(bgColor));
    for (int y = 0; y < source->height() - safeDy; ++y) {
        for (int x = 0; x < source->width(); ++x) {
            shifted.setPixel(x, y + safeDy, source->getPixel(x, y));
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
    bool hasTexCoord = false;
};

struct W3DProjection {
    float minX = 0.0F;
    float minY = 0.0F;
    float maxX = 0.0F;
    float maxY = 0.0F;
    float scale = 1.0F;
    float offsetX = 0.0F;
    float offsetY = 0.0F;
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

    projection.minX = std::numeric_limits<float>::max();
    projection.minY = std::numeric_limits<float>::max();
    projection.maxX = std::numeric_limits<float>::lowest();
    projection.maxY = std::numeric_limits<float>::lowest();

    bool sawVertex = false;
    for (const auto& mesh : meshes) {
        for (const auto& vertex : mesh.mesh.vertices) {
            const auto world = transformW3DVertex(vertex, mesh.worldTransform);
            projection.minX = std::min(projection.minX, world.x);
            projection.minY = std::min(projection.minY, world.y);
            projection.maxX = std::max(projection.maxX, world.x);
            projection.maxY = std::max(projection.maxY, world.y);
            sawVertex = true;
        }
    }
    if (!sawVertex) {
        return projection;
    }

    if (std::fabs(projection.maxX - projection.minX) < 0.0001F) {
        projection.minX -= 0.5F;
        projection.maxX += 0.5F;
    }
    if (std::fabs(projection.maxY - projection.minY) < 0.0001F) {
        projection.minY -= 0.5F;
        projection.maxY += 0.5F;
    }

    projection.margin = width > 8 && height > 8 ? 2 : 0;
    const float drawableWidth = static_cast<float>(std::max(1, width - 1 - projection.margin * 2));
    const float drawableHeight = static_cast<float>(std::max(1, height - 1 - projection.margin * 2));
    const float rangeX = projection.maxX - projection.minX;
    const float rangeY = projection.maxY - projection.minY;
    projection.scale = std::min(drawableWidth / rangeX, drawableHeight / rangeY);
    projection.offsetX = (drawableWidth - rangeX * projection.scale) * 0.5F;
    projection.offsetY = (drawableHeight - rangeY * projection.scale) * 0.5F;
    projection.valid = projection.scale > 0.0F;
    return projection;
}

ProjectedW3DVertex projectW3DVertex(const ::libreshockwave::w3d::W3DVertex& vertex,
                                    const W3DProjection& projection) {
    return ProjectedW3DVertex{
        static_cast<float>(projection.margin) + projection.offsetX + (vertex.x - projection.minX) * projection.scale,
        static_cast<float>(projection.margin) + projection.offsetY + (projection.maxY - vertex.y) * projection.scale
    };
}

int diffuseChannel(float value) {
    return std::clamp(static_cast<int>(std::round(value * 255.0F)), 0, 255);
}

std::uint32_t w3dPaintColor(const W3DRenderableMesh& mesh, const RenderSprite& sprite) {
    if (mesh.material.has_value() && mesh.material->diffuseColor.has_value()) {
        const auto& diffuse = *mesh.material->diffuseColor;
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
    const float clampedU = std::clamp(u, 0.0F, 1.0F);
    const float clampedV = std::clamp(v, 0.0F, 1.0F);
    const int x = std::clamp(static_cast<int>(std::round(clampedU * static_cast<float>(texture.width() - 1))),
                             0,
                             texture.width() - 1);
    const int y = std::clamp(static_cast<int>(std::round(clampedV * static_cast<float>(texture.height() - 1))),
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
        sum += transformW3DVertex(vertex, mesh.worldTransform).z;
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

    const int minX = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const int maxX = std::min(bitmap.width() - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const int minY = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const int maxY = std::min(bitmap.height() - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    if (minX > maxX || minY > maxY) {
        return false;
    }

    bool painted = false;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float px = static_cast<float>(x) + 0.5F;
            const float py = static_cast<float>(y) + 0.5F;
            const float w0 = triangleEdge(b, c, px, py);
            const float w1 = triangleEdge(c, a, px, py);
            const float w2 = triangleEdge(a, b, px, py);
            if ((w0 >= 0.0F && w1 >= 0.0F && w2 >= 0.0F) ||
                (w0 <= 0.0F && w1 <= 0.0F && w2 <= 0.0F)) {
                std::uint32_t paint = color;
                if (texture != nullptr && a.hasTexCoord && b.hasTexCoord && c.hasTexCoord) {
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

SpriteBaker::SpriteBaker(BitmapCache* bitmapCache)
    : bitmapCache_(bitmapCache != nullptr ? bitmapCache : &ownedBitmapCache_) {
    registerDefaultSteps();
}

int SpriteBaker::tickCounter() const {
    return tickCounter_;
}

RenderSprite SpriteBaker::bake(const RenderSprite& sprite) {
    std::shared_ptr<const bitmap::Bitmap> baked;
    for (const auto& step : bakeSteps_) {
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

    return sprite.withBakedBitmap(std::move(baked));
}

std::vector<RenderSprite> SpriteBaker::bakeSprites(const std::vector<RenderSprite>& sprites) {
    ++tickCounter_;
    std::vector<RenderSprite> result;
    result.reserve(sprites.size());
    for (const auto& sprite : sprites) {
        result.push_back(bake(sprite));
    }
    return result;
}

void SpriteBaker::registerBakeStep(SpriteBakeStep step) {
    if (!bakeSteps_.empty() && bakeSteps_.back().name == "unsupported") {
        bakeSteps_.insert(std::prev(bakeSteps_.end()), std::move(step));
        return;
    }
    bakeSteps_.push_back(std::move(step));
}

const std::vector<SpriteBaker::SpriteBakeStep>& SpriteBaker::bakeSteps() const {
    return bakeSteps_;
}

int SpriteBaker::bakeStepCount() const {
    return static_cast<int>(bakeSteps_.size());
}

void SpriteBaker::setBitmapDecodeProvider(BitmapDecodeProvider provider) {
    bitmapDecodeProvider_ = std::move(provider);
}

void SpriteBaker::setLiveBitmapProvider(LiveBitmapProvider provider) {
    liveBitmapProvider_ = std::move(provider);
}

void SpriteBaker::setPaletteVersionProvider(PaletteVersionProvider provider) {
    paletteVersionProvider_ = std::move(provider);
}

void SpriteBaker::setTextBakeProvider(TextBakeProvider provider) {
    textBakeProvider_ = std::move(provider);
}

void SpriteBaker::setTextRenderer(output::TextRenderer* renderer) {
    textRenderer_ = renderer;
}

void SpriteBaker::setFilmLoopBakeProvider(FilmLoopBakeProvider provider) {
    filmLoopBakeProvider_ = std::move(provider);
}

void SpriteBaker::setW3DTextureDecodeProvider(W3DTextureDecodeProvider provider) {
    w3dTextureDecodeProvider_ = std::move(provider);
}

BitmapCache& SpriteBaker::bitmapCache() {
    return *bitmapCache_;
}

const BitmapCache& SpriteBaker::bitmapCache() const {
    return *bitmapCache_;
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
    if (liveBitmapProvider_) {
        auto live = liveBitmapProvider_(sprite);
        const bool hasRuntimeDynamicImage =
            sprite.dynamicMember() != nullptr && live != nullptr && (live->width() > 1 || live->height() > 1);
        if (live != nullptr && (live->isScriptModified() || hasRuntimeDynamicImage)) {
            return std::make_shared<bitmap::Bitmap>(processLiveBitmap(*live, sprite));
        }
    }

    if (auto cached = cachedBitmap(sprite)) {
        return cached;
    }

    auto member = sprite.castMember();
    if (member == nullptr || !bitmapDecodeProvider_) {
        return nullptr;
    }

    auto raw = bitmapDecodeProvider_(*member, nullptr);
    if (raw == nullptr) {
        bitmapCache_->markDecodeFailed(*member);
        return nullptr;
    }

    auto processed = std::make_shared<bitmap::Bitmap>(processDecodedBitmap(*raw, sprite));
    cacheBitmap(sprite, processed);
    return processed;
}

std::shared_ptr<const bitmap::Bitmap> SpriteBaker::bakeText(const RenderSprite& sprite) {
    if (liveBitmapProvider_) {
        auto live = liveBitmapProvider_(sprite);
        if (live != nullptr) {
            return std::make_shared<bitmap::Bitmap>(processLiveBitmap(*live, sprite));
        }
    }

    if (textBakeProvider_) {
        if (auto baked = textBakeProvider_(sprite)) {
            return baked;
        }
    }
    if (auto dynamic = bakeDynamicText(sprite)) {
        return dynamic;
    }
    return bakeFileBackedText(sprite);
}

std::shared_ptr<const bitmap::Bitmap> SpriteBaker::bakeShape(const RenderSprite& sprite) {
    const ::libreshockwave::cast::ShapeInfo* shapeInfo = nullptr;
    auto member = sprite.castMember();
    std::optional<::libreshockwave::cast::ShapeInfo> parsedShape;
    if (auto dynamicInfo = dynamicShapeInfo(sprite)) {
        shapeInfo = dynamicInfo;
    } else if (member != nullptr && member->memberType() == ::libreshockwave::cast::MemberType::Shape) {
        parsedShape = ::libreshockwave::cast::ShapeInfo::parse(member->specificData());
        shapeInfo = &parsedShape.value();
    }

    auto shape = std::make_shared<bitmap::Bitmap>(drawShapeBitmap(sprite, shapeInfo));
    return shape;
}

std::shared_ptr<const bitmap::Bitmap> SpriteBaker::bakeFilmLoop(const RenderSprite& sprite) {
    if (auto fileBacked = bakeFileBackedFilmLoop(sprite)) {
        return fileBacked;
    }

    if (!filmLoopBakeProvider_) {
        return nullptr;
    }

    auto loop = filmLoopBakeProvider_(sprite, tickCounter_);
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

    auto w3d = file->getW3DFileForMember(std::const_pointer_cast<chunks::CastMemberChunk>(member));
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
        const bool hasTexCoords = mesh.mesh.hasTextureCoordinates();
        for (std::size_t index = 0; index < mesh.mesh.vertices.size(); ++index) {
            auto projectedVertex = projectW3DVertex(
                transformW3DVertex(mesh.mesh.vertices[index], mesh.worldTransform),
                projection);
            if (hasTexCoords) {
                projectedVertex.u = mesh.mesh.texCoords[index].u;
                projectedVertex.v = mesh.mesh.texCoords[index].v;
                projectedVertex.hasTexCoord = true;
            }
            projected.push_back(projectedVertex);
        }

        const auto color = w3dPaintColor(mesh, sprite);
        std::shared_ptr<const bitmap::Bitmap> textureBitmap;
        if (hasTexCoords && mesh.texture.has_value() && w3dTextureDecodeProvider_) {
            textureBitmap = w3dTextureDecodeProvider_(*mesh.texture);
            if (textureBitmap != nullptr &&
                (textureBitmap->width() <= 0 || textureBitmap->height() <= 0)) {
                textureBitmap.reset();
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
                                            textureBitmap.get()) || painted;
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
    if (member == nullptr || !member->hasDynamicText() || textRenderer_ == nullptr) {
        return nullptr;
    }

    const int width = sprite.width() > 0 ? sprite.width() : 200;
    const int height = sprite.height() > 0 ? sprite.height() : 20;
    const int bgColor = isTransparentTextInk(sprite) ? 0 : argbRgb(sprite.backColor());

    auto textImage = textRenderer_->renderText(member->textContent(),
                                               width,
                                               height,
                                               member->textFont(),
                                               member->textFontSize(),
                                               member->textFontStyle(),
                                               member->textAlignment(),
                                               member->textColor(),
                                               bgColor,
                                               member->textWordWrap(),
                                               member->textAntialias(),
                                               member->textFixedLineSpace(),
                                               member->textTopSpacing());
    if (textImage == nullptr) {
        return nullptr;
    }

    if (((static_cast<std::uint32_t>(bgColor) >> 24U) & 0xFFU) < 0xFFU || textImage->hasTransparentPixels()) {
        textImage->setNativeAlpha(true);
    }
    if (isTransparentTextInk(sprite)) {
        return textImage;
    }
    if (InkProcessor::shouldProcessInk(sprite.inkMode())) {
        return std::make_shared<bitmap::Bitmap>(
            InkProcessor::applyInk(*textImage, sprite.inkMode(), sprite.backColor(), false, nullptr));
    }
    return textImage;
}

std::shared_ptr<const bitmap::Bitmap> SpriteBaker::bakeFileBackedText(const RenderSprite& sprite) {
    const auto member = sprite.castMember();
    auto* file = mutableFileFor(member);
    if (member == nullptr || file == nullptr || textRenderer_ == nullptr) {
        return nullptr;
    }

    if (member->isTextXtra()) {
        auto styled = file->getXmedStyledTextForMember(std::const_pointer_cast<chunks::CastMemberChunk>(member));
        if (!styled.has_value() || styled->text.empty()) {
            return nullptr;
        }
        const int width = styled->width > 0 ? styled->width : (sprite.width() > 0 ? sprite.width() : 200);
        const int height = styled->height > 0 ? styled->height : (sprite.height() > 0 ? sprite.height() : 20);
        const auto styledTextColor = styled->textColorARGB();
        const int textColor = shouldUseSpriteForeColorForStyledText(sprite, styledTextColor)
            ? argbRgb(sprite.foreColor())
            : static_cast<int>(styledTextColor);
        const int bgColor = isTransparentTextInk(sprite) ? 0 : argbRgb(sprite.backColor());
        auto textImage = textRenderer_->renderXmedText(&*styled, width, height, textColor, bgColor);
        if (sprite.inkMode() == id::InkMode::BACKGROUND_TRANSPARENT) {
            textImage = shiftBitmapDown(std::move(textImage), 2, bgColor);
        }
        if (textImage == nullptr || isTransparentTextInk(sprite)) {
            return textImage;
        }
        if (InkProcessor::shouldProcessInk(sprite.inkMode())) {
            return std::make_shared<bitmap::Bitmap>(
                InkProcessor::applyInk(*textImage, sprite.inkMode(), sprite.backColor(), false, nullptr));
        }
        return textImage;
    }

    auto textChunk = file->getTextForMember(std::const_pointer_cast<chunks::CastMemberChunk>(member));
    if (textChunk == nullptr) {
        return nullptr;
    }

    const auto textInfo = ::libreshockwave::cast::TextInfo::parse(member->specificData());
    std::string fontName = defaultStxtFontName(*file);
    int fontSize = 12;
    int fontStyle = 0;
    bool legacyEmbeddedTextFont = false;
    int runR = -1;
    int runG = -1;
    int runB = -1;
    if (!textChunk->runs().empty()) {
        const auto& run = textChunk->runs().front();
        if (auto mapped = file->getFontNameForId(run.fontId); mapped.has_value() && !mapped->empty()) {
            fontName = *mapped;
        } else if (usesLegacyEmbeddedTextFont(*file, run.fontId, run.fontStyle)) {
            if (auto embedded = ::libreshockwave::player::cast::FontRegistry::getPreferredDirectorPixelFont();
                embedded.has_value() && !embedded->empty()) {
                fontName = *embedded;
                legacyEmbeddedTextFont = true;
            }
        }
        fontSize = run.fontSize;
        fontStyle = run.fontStyle;
        runR = run.colorR;
        runG = run.colorG;
        runB = run.colorB;
    }

    const int width = textInfo.width > 0 ? textInfo.width : (sprite.width() > 0 ? sprite.width() : 200);
    const int height = textInfo.height > 0 ? textInfo.height : (sprite.height() > 0 ? sprite.height() : 20);
    const int runColor = runTextColor(runR, runG, runB);
    const int textColor = shouldUseSpriteForeColorForFileText(sprite, runColor) ? argbRgb(sprite.foreColor()) : runColor;
    const int bgColor = isTransparentTextInk(sprite)
        ? 0
        : argbRgb((textInfo.bgRed << 16) | (textInfo.bgGreen << 8) | textInfo.bgBlue);
    const int horizontalInset = std::min(textInfo.gutterSize, std::max(0, width - 1));
    const int renderWidth = std::max(1, width - horizontalInset * 2);
    const int fixedLineSpace = textInfo.textHeight > fontSize ? fontSize : 0;
    const int topSpacing = textInfo.textHeight > fontSize ? textInfo.textHeight - fontSize : 0;

    const auto style = fontStyleString(fontStyle, legacyEmbeddedTextFont);
    std::shared_ptr<bitmap::Bitmap> rendered;
    if (legacyEmbeddedTextFont) {
        if (auto* simpleRenderer = dynamic_cast<output::SimpleTextRenderer*>(textRenderer_)) {
            rendered = simpleRenderer->renderLegacyStxtText(textChunk->text(),
                                                            renderWidth,
                                                            height,
                                                            fontName,
                                                            fontSize,
                                                            style,
                                                            textAlignment(textInfo.textAlign),
                                                            textColor,
                                                            bgColor,
                                                            textInfo.isWordWrap,
                                                            false,
                                                            fixedLineSpace,
                                                            topSpacing);
        }
    }
    if (rendered == nullptr) {
        rendered = textRenderer_->renderText(textChunk->text(),
                                             renderWidth,
                                             height,
                                             fontName,
                                             fontSize,
                                             style,
                                             textAlignment(textInfo.textAlign),
                                             textColor,
                                             bgColor,
                                             textInfo.isWordWrap,
                                             false,
                                             fixedLineSpace,
                                             topSpacing);
    }
    auto textImage = insetTextBitmap(std::move(rendered), width, height, horizontalInset, bgColor);
    if (textImage == nullptr || isTransparentTextInk(sprite)) {
        return textImage;
    }
    if (InkProcessor::shouldProcessInk(sprite.inkMode())) {
        return std::make_shared<bitmap::Bitmap>(
            InkProcessor::applyInk(*textImage, sprite.inkMode(), sprite.backColor(), false, nullptr));
    }
    return textImage;
}

std::shared_ptr<const bitmap::Bitmap> SpriteBaker::bakeFileBackedFilmLoop(const RenderSprite& sprite) {
    const auto member = sprite.castMember();
    auto* file = mutableFileFor(member);
    if (member == nullptr || file == nullptr) {
        return nullptr;
    }

    auto score = file->getScoreForMember(std::const_pointer_cast<chunks::CastMemberChunk>(member));
    if (score == nullptr || score->frameData().frameChannelData.empty()) {
        return nullptr;
    }

    const auto info = ::libreshockwave::cast::FilmLoopInfo::parse(member->specificData());
    const int loopWidth = info.width() > 0 ? info.width() : sprite.width();
    const int loopHeight = info.height() > 0 ? info.height() : sprite.height();
    if (loopWidth <= 0 || loopHeight <= 0) {
        return nullptr;
    }

    const int frameCount = score->frameData().header.frameCount;
    const int targetFrame = frameCount > 0 ? tickCounter_ % frameCount : 0;
    bitmap::Bitmap composite(loopWidth, loopHeight, 32);

    std::vector<chunks::ScoreChunk::FrameChannelEntry> subSprites;
    for (const auto& entry : score->frameData().frameChannelData) {
        if (entry.frameIndex.value() == targetFrame && !entry.data.isEmpty()) {
            subSprites.push_back(entry);
        }
    }
    std::sort(subSprites.begin(), subSprites.end(), [](const auto& left, const auto& right) {
        return left.channelIndex.value() < right.channelIndex.value();
    });

    for (const auto& entry : subSprites) {
        const auto& data = entry.data;
        if (data.spriteType == 0 || data.castMember <= 0) {
            continue;
        }

        auto subMember = resolveFilmLoopMember(*file, data.castLib, data.castMember);
        if (subMember == nullptr) {
            continue;
        }

        auto subBitmap = bakeFilmLoopMemberBitmap(subMember, data);
        if (subBitmap == nullptr || subBitmap->width() <= 0 || subBitmap->height() <= 0) {
            continue;
        }

        int x = data.posX;
        int y = data.posY;
        if (subMember->isBitmap() && subMember->specificData().size() >= 10) {
            const auto bitmapInfo = ::libreshockwave::cast::BitmapInfo::parse(
                subMember->specificData(),
                directorVersionFor(*file));
            x -= bitmapInfo.regXLocal();
            y -= bitmapInfo.regYLocal();
        } else {
            x -= subMember->regPointX();
            y -= subMember->regPointY();
        }

        blitOnto(composite, *subBitmap, x - info.rectLeft, y - info.rectTop);
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
    const chunks::ScoreChunk::ChannelData& data) {
    if (member == nullptr) {
        return nullptr;
    }

    RenderSprite subSprite(0,
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
    if (auto cached = cachedBitmap(subSprite)) {
        return cached;
    }
    if (!bitmapDecodeProvider_) {
        return nullptr;
    }

    auto raw = bitmapDecodeProvider_(*member, nullptr);
    if (raw == nullptr) {
        bitmapCache_->markDecodeFailed(*member);
        return nullptr;
    }

    auto processed = std::make_shared<bitmap::Bitmap>(processDecodedBitmap(*raw, subSprite));
    cacheBitmap(subSprite, processed);
    return processed;
}

bitmap::Bitmap SpriteBaker::processDecodedBitmap(const bitmap::Bitmap& raw, const RenderSprite& sprite) const {
    bitmap::Bitmap processed = shouldNeutralizeOpaqueWhiteForAuthoredTint(sprite, raw)
        ? InkProcessor::convertOpaqueWhiteToTransparent(raw)
        : raw.copy();
    if (processed.bitDepth() <= 1 && sprite.hasForeColor() && InkProcessor::allowsColorize(sprite.inkMode())) {
        processed = InkProcessor::applyForeColorRemap(processed,
                                                      static_cast<std::uint32_t>(sprite.foreColor()),
                                                      static_cast<std::uint32_t>(sprite.backColor()));
    }
    if (InkProcessor::shouldProcessInk(sprite.inkMode())) {
        processed = InkProcessor::applyInk(processed,
                                           sprite.inkMode(),
                                           sprite.backColor(),
                                           processed.isNativeAlpha(),
                                           raw.imagePalette().get());
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
        !shouldPreserveTallRuntimeBacking(sprite, source)) {
        bitmap::Bitmap inkSource =
            (shouldNeutralizeOpaqueWhiteForScriptCanvas(sprite, source) ||
             shouldNeutralizeOpaqueWhiteForAuthoredTint(sprite, source))
            ? InkProcessor::convertOpaqueWhiteToTransparent(source)
            : source.copy();
        const bool hasNativeAlpha = inkSource.bitDepth() == 32 && inkSource.isNativeAlpha();
        processed = shouldPreserveOutlinedWhiteBodyForScriptCanvas(sprite, inkSource)
            ? InkProcessor::applyInkPreservingOutlinedWhiteBody(inkSource,
                                                                sprite.inkMode(),
                                                                sprite.backColor(),
                                                                hasNativeAlpha,
                                                                inkSource.imagePalette().get())
            : InkProcessor::applyInk(inkSource,
                                     sprite.inkMode(),
                                     sprite.backColor(),
                                     hasNativeAlpha,
                                     inkSource.imagePalette().get());
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

std::shared_ptr<const bitmap::Bitmap> SpriteBaker::cachedBitmap(const RenderSprite& sprite) const {
    auto member = sprite.castMember();
    if (member == nullptr) {
        return nullptr;
    }
    if (paletteVersionProvider_) {
        if (const auto version = paletteVersionProvider_(sprite)) {
            (void)bitmapCache_->invalidateIfPaletteChanged(*member, *version);
        }
    }
    return bitmapCache_->getCachedProcessed(*member,
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
    bitmapCache_->putProcessed(*member,
                               sprite.ink(),
                               sprite.backColor(),
                               sprite.foreColor(),
                               sprite.hasForeColor(),
                               sprite.hasBackColor(),
                               std::move(bitmap));
}

bitmap::Bitmap SpriteBaker::drawShapeBitmap(const RenderSprite& sprite,
                                            const ::libreshockwave::cast::ShapeInfo* shapeInfo) {
    const int width = sprite.width() > 0 ? sprite.width() : 50;
    const int height = sprite.height() > 0 ? sprite.height() : 50;
    bitmap::Bitmap bitmap(width, height, 32);

    if (shapeInfo == nullptr) {
        fillSolidShape(bitmap, sprite);
    } else {
        drawAuthoredShape(bitmap, sprite, *shapeInfo);
    }

    if (InkProcessor::shouldProcessInk(sprite.inkMode())) {
        return InkProcessor::applyInk(bitmap, sprite.inkMode(), sprite.backColor(), false, nullptr);
    }
    return bitmap;
}

void SpriteBaker::drawAuthoredShape(bitmap::Bitmap& bitmap,
                                    const RenderSprite& sprite,
                                    const ::libreshockwave::cast::ShapeInfo& shapeInfo) {
    const int borderStrokes = shapeBorderStrokeCount(sprite, shapeInfo);
    if (!shapeInfo.isFilled() &&
        shapeInfo.shapeType != ::libreshockwave::cast::ShapeType::Line &&
        borderStrokes <= 0) {
        return;
    }

    switch (shapeInfo.shapeType) {
        case ::libreshockwave::cast::ShapeType::Rect:
        case ::libreshockwave::cast::ShapeType::OvalRect:
            if (shapeInfo.isFilled()) {
                fillSolidShape(bitmap, sprite);
            } else {
                for (int i = 0; i < borderStrokes; ++i) {
                    drawRect(bitmap, i, i, bitmap.width() - (i * 2), bitmap.height() - (i * 2), sprite);
                }
            }
            break;
        case ::libreshockwave::cast::ShapeType::Oval:
            if (shapeInfo.isFilled()) {
                drawOval(bitmap,
                         bitmap.width() / 2,
                         bitmap.height() / 2,
                         std::max(0, bitmap.width() / 2),
                         std::max(0, bitmap.height() / 2),
                         sprite,
                         true);
            } else {
                for (int i = 0; i < borderStrokes; ++i) {
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
            const int strokes = shapeLineStrokeCount(sprite, shapeInfo);
            if (strokes <= 0) {
                return;
            }
            const bool bottomToTop = shapeInfo.lineDirection == 6;
            const int startY = bottomToTop ? bitmap.height() - 1 : 0;
            const int endY = bottomToTop ? 0 : bitmap.height() - 1;
            for (int i = 0; i < strokes; ++i) {
                drawLine(bitmap,
                         0,
                         std::clamp(startY - i, 0, std::max(0, bitmap.height() - 1)),
                        std::max(0, bitmap.width() - 1),
                        std::clamp(endY - i, 0, std::max(0, bitmap.height() - 1)),
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
