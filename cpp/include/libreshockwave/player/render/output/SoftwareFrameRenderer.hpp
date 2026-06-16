#pragma once

#include <cstdint>
#include <vector>

#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/id/Ids.hpp"
#include "libreshockwave/player/render/pipeline/FrameSnapshot.hpp"

namespace libreshockwave::player::render::output {

class SoftwareFrameRenderer final {
public:
    SoftwareFrameRenderer() = delete;

    [[nodiscard]] static bitmap::Bitmap renderFrame(const pipeline::FrameSnapshot& snapshot,
                                                    int stageWidth,
                                                    int stageHeight);

private:
    static void blitBitmap(std::vector<std::uint32_t>& argb,
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
                           bool flipV);

    static void blitBitmapScaled(std::vector<std::uint32_t>& argb,
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
                                 bool flipV);

    [[nodiscard]] static bool isSpecialCompositingInk(id::InkMode ink);
    static void compositeSpecialInk(std::vector<std::uint32_t>& argb,
                                    int dstIdx,
                                    std::uint32_t src,
                                    int srcA,
                                    id::InkMode ink);
    static void alphaComposite(std::vector<std::uint32_t>& argb, int dstIdx, std::uint32_t src, int srcA);
    static void alphaCompositePercent(std::vector<std::uint32_t>& argb,
                                      int dstIdx,
                                      std::uint32_t src,
                                      int srcA,
                                      int blendPercent);
};

} // namespace libreshockwave::player::render::output
