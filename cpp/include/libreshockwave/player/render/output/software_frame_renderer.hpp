#pragma once

#include <cstdint>
#include <vector>

#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/id/ids.hpp"
#include "libreshockwave/player/render/pipeline/frame_snapshot.hpp"

namespace libreshockwave::player::render::output {

class SoftwareFrameRenderer final {
public:
    SoftwareFrameRenderer() = delete;

    [[nodiscard]] static bitmap::Bitmap renderFrame(const pipeline::FrameSnapshot& snapshot,
                                                    int stage_width,
                                                    int stage_height);

private:
    static void blitBitmap(std::vector<std::uint32_t>& argb,
                           int stage_width,
                           int stage_height,
                           const std::vector<std::uint32_t>& src_pixels,
                           int src_w,
                           int src_h,
                           int dst_x,
                           int dst_y,
                           int blend,
                           id::InkMode ink,
                           int back_color,
                           bool flip_h,
                           bool flip_v);

    static void blitBitmapScaled(std::vector<std::uint32_t>& argb,
                                 int stage_width,
                                 int stage_height,
                                 const std::vector<std::uint32_t>& src_pixels,
                                 int src_w,
                                 int src_h,
                                 int dst_x,
                                 int dst_y,
                                 int dst_w,
                                 int dst_h,
                                 int blend,
                                 id::InkMode ink,
                                 int back_color,
                                 bool flip_h,
                                 bool flip_v);

    [[nodiscard]] static bool isSpecialCompositingInk(id::InkMode ink);

    // Composites one source pixel onto the stage buffer. Both blit loops share
    // this so ink arithmetic, blend scaling, and the background colour filter
    // stay in one place.
    static void compositePixel(std::vector<std::uint32_t>& argb,
                               int dst_idx,
                               std::uint32_t src,
                               int src_a,
                               int blend,
                               id::InkMode ink,
                               int back_color,
                               bool use_special_ink);

    static void compositeSpecialInk(std::vector<std::uint32_t>& argb,
                                    int dst_idx,
                                    std::uint32_t src,
                                    int src_a,
                                    id::InkMode ink,
                                    int back_color);
    static void alphaComposite(std::vector<std::uint32_t>& argb, int dst_idx, std::uint32_t src, int src_a);
    static void alphaCompositePercent(std::vector<std::uint32_t>& argb,
                                      int dst_idx,
                                      std::uint32_t src,
                                      int src_a,
                                      int blend_percent);
};

} // namespace libreshockwave::player::render::output
