#include "libreshockwave/player/render/output/SoftwareFrameRenderer.hpp"

#include <algorithm>
#include <stdexcept>

namespace libreshockwave::player::render::output {
namespace {

int channel(std::uint32_t argb, int shift) {
    return static_cast<int>((argb >> shift) & 0xFFU);
}

std::uint32_t opaqueRgb(int r, int g, int b) {
    return 0xFF000000U |
           (static_cast<std::uint32_t>(r & 0xFF) << 16) |
           (static_cast<std::uint32_t>(g & 0xFF) << 8) |
           static_cast<std::uint32_t>(b & 0xFF);
}

} // namespace

bitmap::Bitmap SoftwareFrameRenderer::renderFrame(const pipeline::FrameSnapshot& snapshot,
                                                  int stageWidth,
                                                  int stageHeight) {
    if (stageWidth < 0 || stageHeight < 0) {
        throw std::invalid_argument("Stage dimensions must be non-negative");
    }

    const auto pixelCount = static_cast<std::size_t>(stageWidth) * static_cast<std::size_t>(stageHeight);
    std::vector<std::uint32_t> argb(pixelCount, 0);

    if (snapshot.stageImage) {
        const auto& srcPixels = snapshot.stageImage->pixels();
        const int srcW = snapshot.stageImage->width();
        const int srcH = snapshot.stageImage->height();
        if (!srcPixels.empty()) {
            for (int y = 0; y < std::min(srcH, stageHeight); ++y) {
                for (int x = 0; x < std::min(srcW, stageWidth); ++x) {
                    argb[static_cast<std::size_t>(y * stageWidth + x)] =
                        srcPixels[static_cast<std::size_t>(y * srcW + x)];
                }
            }
        }
    } else {
        const std::uint32_t bg = (snapshot.backgroundColor & 0x00FFFFFFU) | 0xFF000000U;
        std::fill(argb.begin(), argb.end(), bg);
    }

    for (const auto& sprite : snapshot.sprites) {
        if (!sprite.isVisible()) {
            continue;
        }

        auto baked = sprite.bakedBitmap();
        if (!baked || baked->width() <= 0 || baked->height() <= 0 || baked->pixels().empty()) {
            continue;
        }

        const int sx = sprite.x();
        const int sy = sprite.y();
        const int sw = sprite.width() > 0 ? sprite.width() : baked->width();
        const int sh = sprite.height() > 0 ? sprite.height() : baked->height();
        const int blend = sprite.blend();
        const id::InkMode ink = sprite.inkMode();
        const bool flipH = sprite.isFlipH() ^ sprite.hasDirectorHorizontalMirror();
        const bool flipV = sprite.isFlipV();

        if (sw == baked->width() && sh == baked->height()) {
            blitBitmap(argb, stageWidth, stageHeight, baked->pixels(), baked->width(), baked->height(),
                       sx, sy, blend, ink, flipH, flipV);
        } else {
            blitBitmapScaled(argb, stageWidth, stageHeight, baked->pixels(), baked->width(), baked->height(),
                             sx, sy, sw, sh, blend, ink, flipH, flipV);
        }
    }

    return bitmap::Bitmap(stageWidth, stageHeight, 32, std::move(argb));
}

void SoftwareFrameRenderer::blitBitmap(std::vector<std::uint32_t>& argb,
                                       int stageWidth,
                                       int stageHeight,
                                       const std::vector<std::uint32_t>& srcPixels,
                                       int srcW,
                                       int srcH,
                                       int dstX,
                                       int dstY,
                                       int blend,
                                       id::InkMode ink,
                                       bool flipH,
                                       bool flipV) {
    if (srcW <= 0 || srcH <= 0 || srcPixels.size() < static_cast<std::size_t>(srcW * srcH)) {
        return;
    }

    const int sx0 = std::max(0, -dstX);
    const int sy0 = std::max(0, -dstY);
    const int sx1 = std::min(srcW, stageWidth - dstX);
    const int sy1 = std::min(srcH, stageHeight - dstY);
    if (sx0 >= sx1 || sy0 >= sy1) {
        return;
    }

    const int argbLen = static_cast<int>(argb.size());
    const bool useSpecialInk = isSpecialCompositingInk(ink);

    for (int sy = sy0; sy < sy1; ++sy) {
        const int fetchY = flipV ? (srcH - 1 - sy) : sy;
        for (int sx = sx0; sx < sx1; ++sx) {
            const int fetchX = flipH ? (srcW - 1 - sx) : sx;
            const int srcIdx = fetchY * srcW + fetchX;
            const std::uint32_t src = srcPixels[static_cast<std::size_t>(srcIdx)];
            int srcA = channel(src, 24);
            if (srcA == 0) {
                continue;
            }

            const int dstIdx = (dstY + sy) * stageWidth + (dstX + sx);
            if (dstIdx < 0 || dstIdx >= argbLen) {
                continue;
            }

            if (useSpecialInk) {
                if (blend < 100) {
                    srcA = (srcA * blend) / 100;
                    if (srcA == 0) {
                        continue;
                    }
                }
                compositeSpecialInk(argb, dstIdx, src, srcA, ink);
            } else if (blend < 100) {
                alphaCompositePercent(argb, dstIdx, src, srcA, blend);
            } else if (srcA >= 255) {
                argb[static_cast<std::size_t>(dstIdx)] = src | 0xFF000000U;
            } else {
                alphaComposite(argb, dstIdx, src, srcA);
            }
        }
    }
}

void SoftwareFrameRenderer::blitBitmapScaled(std::vector<std::uint32_t>& argb,
                                             int stageWidth,
                                             int stageHeight,
                                             const std::vector<std::uint32_t>& srcPixels,
                                             int srcW,
                                             int srcH,
                                             int dstX,
                                             int dstY,
                                             int dstW,
                                             int dstH,
                                             int blend,
                                             id::InkMode ink,
                                             bool flipH,
                                             bool flipV) {
    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0 ||
        srcPixels.size() < static_cast<std::size_t>(srcW * srcH)) {
        return;
    }

    const int dx0 = std::max(0, dstX);
    const int dy0 = std::max(0, dstY);
    const int dx1 = std::min(stageWidth, dstX + dstW);
    const int dy1 = std::min(stageHeight, dstY + dstH);
    if (dx0 >= dx1 || dy0 >= dy1) {
        return;
    }

    const int srcLen = static_cast<int>(srcPixels.size());
    const int argbLen = static_cast<int>(argb.size());
    const bool useSpecialInk = isSpecialCompositingInk(ink);

    for (int dy = dy0; dy < dy1; ++dy) {
        int srcY = ((dy - dstY) * srcH) / dstH;
        if (flipV) {
            srcY = srcH - 1 - srcY;
        }
        if (srcY < 0 || srcY >= srcH) {
            continue;
        }

        for (int dx = dx0; dx < dx1; ++dx) {
            int srcX = ((dx - dstX) * srcW) / dstW;
            if (flipH) {
                srcX = srcW - 1 - srcX;
            }
            if (srcX < 0 || srcX >= srcW) {
                continue;
            }

            const int srcIdx = srcY * srcW + srcX;
            if (srcIdx < 0 || srcIdx >= srcLen) {
                continue;
            }

            const std::uint32_t src = srcPixels[static_cast<std::size_t>(srcIdx)];
            int srcA = channel(src, 24);
            if (srcA == 0) {
                continue;
            }

            const int dstIdx = dy * stageWidth + dx;
            if (dstIdx < 0 || dstIdx >= argbLen) {
                continue;
            }

            if (useSpecialInk) {
                if (blend < 100) {
                    srcA = (srcA * blend) / 100;
                    if (srcA == 0) {
                        continue;
                    }
                }
                compositeSpecialInk(argb, dstIdx, src, srcA, ink);
            } else if (blend < 100) {
                alphaCompositePercent(argb, dstIdx, src, srcA, blend);
            } else if (srcA >= 255) {
                argb[static_cast<std::size_t>(dstIdx)] = src | 0xFF000000U;
            } else {
                alphaComposite(argb, dstIdx, src, srcA);
            }
        }
    }
}

bool SoftwareFrameRenderer::isSpecialCompositingInk(id::InkMode ink) {
    return ink == id::InkMode::ADD_PIN || ink == id::InkMode::ADD ||
           ink == id::InkMode::SUBTRACT_PIN || ink == id::InkMode::SUBTRACT ||
           ink == id::InkMode::LIGHTEST || ink == id::InkMode::DARKEST ||
           ink == id::InkMode::LIGHTEN || ink == id::InkMode::REVERSE ||
           ink == id::InkMode::GHOST || ink == id::InkMode::NOT_COPY ||
           ink == id::InkMode::NOT_TRANSPARENT || ink == id::InkMode::NOT_REVERSE ||
           ink == id::InkMode::NOT_GHOST;
}

void SoftwareFrameRenderer::compositeSpecialInk(std::vector<std::uint32_t>& argb,
                                                int dstIdx,
                                                std::uint32_t src,
                                                int srcA,
                                                id::InkMode ink) {
    if (dstIdx < 0 || dstIdx >= static_cast<int>(argb.size())) {
        return;
    }

    const std::uint32_t dst = argb[static_cast<std::size_t>(dstIdx)];
    const int srcR = channel(src, 16);
    const int srcG = channel(src, 8);
    const int srcB = channel(src, 0);
    const int dstR = channel(dst, 16);
    const int dstG = channel(dst, 8);
    const int dstB = channel(dst, 0);

    int outR = 0;
    int outG = 0;
    int outB = 0;

    switch (ink) {
        case id::InkMode::ADD_PIN:
        case id::InkMode::ADD:
            outR = std::min(255, dstR + srcR);
            outG = std::min(255, dstG + srcG);
            outB = std::min(255, dstB + srcB);
            break;
        case id::InkMode::SUBTRACT_PIN:
        case id::InkMode::SUBTRACT:
            outR = std::max(0, dstR - srcR);
            outG = std::max(0, dstG - srcG);
            outB = std::max(0, dstB - srcB);
            break;
        case id::InkMode::DARKEST:
            outR = std::min(dstR, srcR);
            outG = std::min(dstG, srcG);
            outB = std::min(dstB, srcB);
            break;
        case id::InkMode::LIGHTEN:
        case id::InkMode::LIGHTEST:
            outR = std::max(dstR, srcR);
            outG = std::max(dstG, srcG);
            outB = std::max(dstB, srcB);
            break;
        case id::InkMode::REVERSE:
            outR = srcR ^ dstR;
            outG = srcG ^ dstG;
            outB = srcB ^ dstB;
            break;
        case id::InkMode::GHOST:
            outR = (~srcR & 0xFF) & dstR;
            outG = (~srcG & 0xFF) & dstG;
            outB = (~srcB & 0xFF) & dstB;
            break;
        case id::InkMode::NOT_COPY:
            outR = ~srcR & 0xFF;
            outG = ~srcG & 0xFF;
            outB = ~srcB & 0xFF;
            break;
        case id::InkMode::NOT_TRANSPARENT:
            outR = srcR & dstR;
            outG = srcG & dstG;
            outB = srcB & dstB;
            break;
        case id::InkMode::NOT_REVERSE:
            outR = (~srcR & 0xFF) ^ dstR;
            outG = (~srcG & 0xFF) ^ dstG;
            outB = (~srcB & 0xFF) ^ dstB;
            break;
        case id::InkMode::NOT_GHOST:
            outR = (~srcR & 0xFF) | dstR;
            outG = (~srcG & 0xFF) | dstG;
            outB = (~srcB & 0xFF) | dstB;
            break;
        default:
            alphaComposite(argb, dstIdx, src, srcA);
            return;
    }

    if (srcA < 255) {
        const int invA = 255 - srcA;
        outR = (outR * srcA + dstR * invA) / 255;
        outG = (outG * srcA + dstG * invA) / 255;
        outB = (outB * srcA + dstB * invA) / 255;
    }

    argb[static_cast<std::size_t>(dstIdx)] = opaqueRgb(outR, outG, outB);
}

void SoftwareFrameRenderer::alphaComposite(std::vector<std::uint32_t>& argb, int dstIdx, std::uint32_t src, int srcA) {
    if (dstIdx < 0 || dstIdx >= static_cast<int>(argb.size())) {
        return;
    }

    const std::uint32_t dst = argb[static_cast<std::size_t>(dstIdx)];
    const int dstA = channel(dst, 24);
    const int invA = 255 - srcA;
    const int outA = srcA + (dstA * invA / 255);
    if (outA == 0) {
        argb[static_cast<std::size_t>(dstIdx)] = 0;
        return;
    }

    const int srcR = channel(src, 16);
    const int srcG = channel(src, 8);
    const int srcB = channel(src, 0);
    const int dstR = channel(dst, 16);
    const int dstG = channel(dst, 8);
    const int dstB = channel(dst, 0);

    const int outR = (srcR * srcA + dstR * dstA * invA / 255) / outA;
    const int outG = (srcG * srcA + dstG * dstA * invA / 255) / outA;
    const int outB = (srcB * srcA + dstB * dstA * invA / 255) / outA;

    argb[static_cast<std::size_t>(dstIdx)] =
        (static_cast<std::uint32_t>(outA & 0xFF) << 24) |
        (static_cast<std::uint32_t>(outR & 0xFF) << 16) |
        (static_cast<std::uint32_t>(outG & 0xFF) << 8) |
        static_cast<std::uint32_t>(outB & 0xFF);
}

void SoftwareFrameRenderer::alphaCompositePercent(std::vector<std::uint32_t>& argb,
                                                  int dstIdx,
                                                  std::uint32_t src,
                                                  int srcA,
                                                  int blendPercent) {
    if (dstIdx < 0 || dstIdx >= static_cast<int>(argb.size()) || srcA <= 0 || blendPercent <= 0) {
        return;
    }
    if (blendPercent >= 100) {
        alphaComposite(argb, dstIdx, src, srcA);
        return;
    }

    const std::uint32_t dst = argb[static_cast<std::size_t>(dstIdx)];
    const int dstA = channel(dst, 24);
    if (dstA != 255) {
        const int blendedAlpha = (srcA * blendPercent) / 100;
        alphaComposite(argb, dstIdx, src, blendedAlpha);
        return;
    }

    const int opacity = (srcA * blendPercent) / 100;
    const int invOpacity = 256 - opacity;
    const int srcR = channel(src, 16);
    const int srcG = channel(src, 8);
    const int srcB = channel(src, 0);
    const int dstR = channel(dst, 16);
    const int dstG = channel(dst, 8);
    const int dstB = channel(dst, 0);

    const int outR = (srcR * opacity + dstR * invOpacity) >> 8;
    const int outG = (srcG * opacity + dstG * invOpacity) >> 8;
    const int outB = (srcB * opacity + dstB * invOpacity) >> 8;

    argb[static_cast<std::size_t>(dstIdx)] = opaqueRgb(outR, outG, outB);
}

} // namespace libreshockwave::player::render::output
