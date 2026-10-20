#include "libreshockwave/player/render/output/software_frame_renderer.hpp"

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
                                                  int stage_width,
                                                  int stage_height) {
    if (stage_width < 0 || stage_height < 0) {
        throw std::invalid_argument("Stage dimensions must be non-negative");
    }

    const auto pixel_count = static_cast<std::size_t>(stage_width) * static_cast<std::size_t>(stage_height);
    std::vector<std::uint32_t> argb(pixel_count, 0);

    if (snapshot.stage_image) {
        const auto& src_pixels = snapshot.stage_image->pixels();
        const int src_w = snapshot.stage_image->width();
        const int src_h = snapshot.stage_image->height();
        if (!src_pixels.empty()) {
            for (int y = 0; y < std::min(src_h, stage_height); ++y) {
                for (int x = 0; x < std::min(src_w, stage_width); ++x) {
                    argb[static_cast<std::size_t>(y * stage_width + x)] =
                        src_pixels[static_cast<std::size_t>(y * src_w + x)];
                }
            }
        }
    } else {
        const std::uint32_t bg = (snapshot.background_color & 0x00FFFFFFU) | 0xFF000000U;
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
        const bool flip_h = sprite.isFlipH() ^ sprite.hasDirectorHorizontalMirror();
        const bool flip_v = sprite.isFlipV();

        if (sw == baked->width() && sh == baked->height()) {
            blitBitmap(argb, stage_width, stage_height, baked->pixels(), baked->width(), baked->height(),
                       sx, sy, blend, ink, flip_h, flip_v);
        } else {
            blitBitmapScaled(argb, stage_width, stage_height, baked->pixels(), baked->width(), baked->height(),
                             sx, sy, sw, sh, blend, ink, flip_h, flip_v);
        }
    }

    return bitmap::Bitmap(stage_width, stage_height, 32, std::move(argb));
}

void SoftwareFrameRenderer::blitBitmap(std::vector<std::uint32_t>& argb,
                                       int stage_width,
                                       int stage_height,
                                       const std::vector<std::uint32_t>& src_pixels,
                                       int src_w,
                                       int src_h,
                                       int dst_x,
                                       int dst_y,
                                       int blend,
                                       id::InkMode ink,
                                       bool flip_h,
                                       bool flip_v) {
    if (src_w <= 0 || src_h <= 0 || src_pixels.size() < static_cast<std::size_t>(src_w * src_h)) {
        return;
    }

    const int sx0 = std::max(0, -dst_x);
    const int sy0 = std::max(0, -dst_y);
    const int sx1 = std::min(src_w, stage_width - dst_x);
    const int sy1 = std::min(src_h, stage_height - dst_y);
    if (sx0 >= sx1 || sy0 >= sy1) {
        return;
    }

    const int argb_len = static_cast<int>(argb.size());
    const bool use_special_ink = isSpecialCompositingInk(ink);

    for (int sy = sy0; sy < sy1; ++sy) {
        const int fetch_y = flip_v ? (src_h - 1 - sy) : sy;
        for (int sx = sx0; sx < sx1; ++sx) {
            const int fetch_x = flip_h ? (src_w - 1 - sx) : sx;
            const int src_idx = fetch_y * src_w + fetch_x;
            const std::uint32_t src = src_pixels[static_cast<std::size_t>(src_idx)];
            int src_a = channel(src, 24);
            if (src_a == 0) {
                continue;
            }

            const int dst_idx = (dst_y + sy) * stage_width + (dst_x + sx);
            if (dst_idx < 0 || dst_idx >= argb_len) {
                continue;
            }

            if (use_special_ink) {
                if (blend < 100) {
                    src_a = (src_a * blend) / 100;
                    if (src_a == 0) {
                        continue;
                    }
                }
                compositeSpecialInk(argb, dst_idx, src, src_a, ink);
            } else if (blend < 100) {
                alphaCompositePercent(argb, dst_idx, src, src_a, blend);
            } else if (src_a >= 255) {
                argb[static_cast<std::size_t>(dst_idx)] = src | 0xFF000000U;
            } else {
                alphaComposite(argb, dst_idx, src, src_a);
            }
        }
    }
}

void SoftwareFrameRenderer::blitBitmapScaled(std::vector<std::uint32_t>& argb,
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
                                             bool flip_h,
                                             bool flip_v) {
    if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0 ||
        src_pixels.size() < static_cast<std::size_t>(src_w * src_h)) {
        return;
    }

    const int dx0 = std::max(0, dst_x);
    const int dy0 = std::max(0, dst_y);
    const int dx1 = std::min(stage_width, dst_x + dst_w);
    const int dy1 = std::min(stage_height, dst_y + dst_h);
    if (dx0 >= dx1 || dy0 >= dy1) {
        return;
    }

    const int src_len = static_cast<int>(src_pixels.size());
    const int argb_len = static_cast<int>(argb.size());
    const bool use_special_ink = isSpecialCompositingInk(ink);

    for (int dy = dy0; dy < dy1; ++dy) {
        int src_y = ((dy - dst_y) * src_h) / dst_h;
        if (flip_v) {
            src_y = src_h - 1 - src_y;
        }
        if (src_y < 0 || src_y >= src_h) {
            continue;
        }

        for (int dx = dx0; dx < dx1; ++dx) {
            int src_x = ((dx - dst_x) * src_w) / dst_w;
            if (flip_h) {
                src_x = src_w - 1 - src_x;
            }
            if (src_x < 0 || src_x >= src_w) {
                continue;
            }

            const int src_idx = src_y * src_w + src_x;
            if (src_idx < 0 || src_idx >= src_len) {
                continue;
            }

            const std::uint32_t src = src_pixels[static_cast<std::size_t>(src_idx)];
            int src_a = channel(src, 24);
            if (src_a == 0) {
                continue;
            }

            const int dst_idx = dy * stage_width + dx;
            if (dst_idx < 0 || dst_idx >= argb_len) {
                continue;
            }

            if (use_special_ink) {
                if (blend < 100) {
                    src_a = (src_a * blend) / 100;
                    if (src_a == 0) {
                        continue;
                    }
                }
                compositeSpecialInk(argb, dst_idx, src, src_a, ink);
            } else if (blend < 100) {
                alphaCompositePercent(argb, dst_idx, src, src_a, blend);
            } else if (src_a >= 255) {
                argb[static_cast<std::size_t>(dst_idx)] = src | 0xFF000000U;
            } else {
                alphaComposite(argb, dst_idx, src, src_a);
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
                                                int dst_idx,
                                                std::uint32_t src,
                                                int src_a,
                                                id::InkMode ink) {
    if (dst_idx < 0 || dst_idx >= static_cast<int>(argb.size())) {
        return;
    }

    const std::uint32_t dst = argb[static_cast<std::size_t>(dst_idx)];
    const int src_r = channel(src, 16);
    const int src_g = channel(src, 8);
    const int src_b = channel(src, 0);
    const int dst_r = channel(dst, 16);
    const int dst_g = channel(dst, 8);
    const int dst_b = channel(dst, 0);

    int out_r = 0;
    int out_g = 0;
    int out_b = 0;

    switch (ink) {
        case id::InkMode::ADD_PIN:
        case id::InkMode::ADD:
            out_r = std::min(255, dst_r + src_r);
            out_g = std::min(255, dst_g + src_g);
            out_b = std::min(255, dst_b + src_b);
            break;
        case id::InkMode::SUBTRACT_PIN:
        case id::InkMode::SUBTRACT:
            out_r = std::max(0, dst_r - src_r);
            out_g = std::max(0, dst_g - src_g);
            out_b = std::max(0, dst_b - src_b);
            break;
        case id::InkMode::DARKEST:
            out_r = std::min(dst_r, src_r);
            out_g = std::min(dst_g, src_g);
            out_b = std::min(dst_b, src_b);
            break;
        case id::InkMode::LIGHTEN:
        case id::InkMode::LIGHTEST:
            out_r = std::max(dst_r, src_r);
            out_g = std::max(dst_g, src_g);
            out_b = std::max(dst_b, src_b);
            break;
        case id::InkMode::REVERSE:
            out_r = src_r ^ dst_r;
            out_g = src_g ^ dst_g;
            out_b = src_b ^ dst_b;
            break;
        case id::InkMode::GHOST:
            out_r = (~src_r & 0xFF) & dst_r;
            out_g = (~src_g & 0xFF) & dst_g;
            out_b = (~src_b & 0xFF) & dst_b;
            break;
        case id::InkMode::NOT_COPY:
            out_r = ~src_r & 0xFF;
            out_g = ~src_g & 0xFF;
            out_b = ~src_b & 0xFF;
            break;
        case id::InkMode::NOT_TRANSPARENT:
            out_r = src_r & dst_r;
            out_g = src_g & dst_g;
            out_b = src_b & dst_b;
            break;
        case id::InkMode::NOT_REVERSE:
            out_r = (~src_r & 0xFF) ^ dst_r;
            out_g = (~src_g & 0xFF) ^ dst_g;
            out_b = (~src_b & 0xFF) ^ dst_b;
            break;
        case id::InkMode::NOT_GHOST:
            out_r = (~src_r & 0xFF) | dst_r;
            out_g = (~src_g & 0xFF) | dst_g;
            out_b = (~src_b & 0xFF) | dst_b;
            break;
        default:
            alphaComposite(argb, dst_idx, src, src_a);
            return;
    }

    if (src_a < 255) {
        const int inv_a = 255 - src_a;
        out_r = (out_r * src_a + dst_r * inv_a) / 255;
        out_g = (out_g * src_a + dst_g * inv_a) / 255;
        out_b = (out_b * src_a + dst_b * inv_a) / 255;
    }

    argb[static_cast<std::size_t>(dst_idx)] = opaqueRgb(out_r, out_g, out_b);
}

void SoftwareFrameRenderer::alphaComposite(std::vector<std::uint32_t>& argb, int dst_idx, std::uint32_t src, int src_a) {
    if (dst_idx < 0 || dst_idx >= static_cast<int>(argb.size())) {
        return;
    }

    const std::uint32_t dst = argb[static_cast<std::size_t>(dst_idx)];
    const int dst_a = channel(dst, 24);
    const int inv_a = 255 - src_a;
    const int out_a = src_a + (dst_a * inv_a / 255);
    if (out_a == 0) {
        argb[static_cast<std::size_t>(dst_idx)] = 0;
        return;
    }

    const int src_r = channel(src, 16);
    const int src_g = channel(src, 8);
    const int src_b = channel(src, 0);
    const int dst_r = channel(dst, 16);
    const int dst_g = channel(dst, 8);
    const int dst_b = channel(dst, 0);

    const int out_r = (src_r * src_a + dst_r * dst_a * inv_a / 255) / out_a;
    const int out_g = (src_g * src_a + dst_g * dst_a * inv_a / 255) / out_a;
    const int out_b = (src_b * src_a + dst_b * dst_a * inv_a / 255) / out_a;

    argb[static_cast<std::size_t>(dst_idx)] =
        (static_cast<std::uint32_t>(out_a & 0xFF) << 24) |
        (static_cast<std::uint32_t>(out_r & 0xFF) << 16) |
        (static_cast<std::uint32_t>(out_g & 0xFF) << 8) |
        static_cast<std::uint32_t>(out_b & 0xFF);
}

void SoftwareFrameRenderer::alphaCompositePercent(std::vector<std::uint32_t>& argb,
                                                  int dst_idx,
                                                  std::uint32_t src,
                                                  int src_a,
                                                  int blend_percent) {
    if (dst_idx < 0 || dst_idx >= static_cast<int>(argb.size()) || src_a <= 0 || blend_percent <= 0) {
        return;
    }
    if (blend_percent >= 100) {
        alphaComposite(argb, dst_idx, src, src_a);
        return;
    }

    const std::uint32_t dst = argb[static_cast<std::size_t>(dst_idx)];
    const int dst_a = channel(dst, 24);
    if (dst_a != 255) {
        const int blended_alpha = (src_a * blend_percent) / 100;
        alphaComposite(argb, dst_idx, src, blended_alpha);
        return;
    }

    const int opacity = std::clamp((src_a * blend_percent * 256) / (255 * 100), 0, 256);
    const int inv_opacity = 256 - opacity;
    const int src_r = channel(src, 16);
    const int src_g = channel(src, 8);
    const int src_b = channel(src, 0);
    const int dst_r = channel(dst, 16);
    const int dst_g = channel(dst, 8);
    const int dst_b = channel(dst, 0);

    const int out_r = (src_r * opacity + dst_r * inv_opacity) >> 8;
    const int out_g = (src_g * opacity + dst_g * inv_opacity) >> 8;
    const int out_b = (src_b * opacity + dst_b * inv_opacity) >> 8;

    argb[static_cast<std::size_t>(dst_idx)] = opaqueRgb(out_r, out_g, out_b);
}

} // namespace libreshockwave::player::render::output
