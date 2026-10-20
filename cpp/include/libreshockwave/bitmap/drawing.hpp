#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "libreshockwave/id/ids.hpp"

namespace libreshockwave::bitmap {

class Bitmap;

namespace drawing {

void fillRect(Bitmap& dest, int x, int y, int width, int height, std::uint32_t argb);
void drawRect(Bitmap& dest, int x, int y, int width, int height, std::uint32_t argb);
void drawLine(Bitmap& dest, int x0, int y0, int x1, int y1, std::uint32_t argb);
void fillEllipse(Bitmap& dest, int cx, int cy, int rx, int ry, std::uint32_t argb);
void drawEllipse(Bitmap& dest, int cx, int cy, int rx, int ry, std::uint32_t argb);
[[nodiscard]] std::shared_ptr<Bitmap> createMatte(const Bitmap& src, int alpha_threshold = 0);
[[nodiscard]] std::shared_ptr<Bitmap> createMask(const Bitmap& src, int alpha_threshold = 0);
[[nodiscard]] Bitmap applyMatteToRegion(const Bitmap& src, int x, int y, int width, int height);
[[nodiscard]] Bitmap applyFloodFillTransparency(const Bitmap& src);
[[nodiscard]] int combineAlpha(int src_alpha, int blend_alpha);
[[nodiscard]] std::uint32_t alphaBlend(std::uint32_t fg, std::uint32_t bg, int alpha);
[[nodiscard]] std::uint32_t applyInk(std::uint32_t src,
                                     std::uint32_t dest,
                                     id::InkMode ink,
                                     int blend,
                                     int background_key_rgb = 0xFFFFFF);
[[nodiscard]] bool maskAllowsPixel(const Bitmap& mask, int x, int y);
[[nodiscard]] std::shared_ptr<Bitmap> preprocessBackgroundTransparent(const Bitmap& src,
                                                                      int background_key_rgb = 0xFFFFFF);
void copyPixels(Bitmap& dest,
                const Bitmap& src,
                int dest_x,
                int dest_y,
                int src_x,
                int src_y,
                int width,
                int height,
                id::InkMode ink,
                int blend,
                const Bitmap* mask = nullptr,
                int background_key_rgb = 0xFFFFFF);
void copyPixels(Bitmap& dest,
                const Bitmap& src,
                int dest_x,
                int dest_y,
                id::InkMode ink,
                int blend);
[[nodiscard]] std::shared_ptr<Bitmap> transformQuadBitmap(const Bitmap& src,
                                                          int src_left,
                                                          int src_top,
                                                          int src_right,
                                                          int src_bottom,
                                                          const std::array<int, 4>& px,
                                                          const std::array<int, 4>& py,
                                                          int min_x,
                                                          int min_y,
                                                          int max_x,
                                                          int max_y);

} // namespace drawing

} // namespace libreshockwave::bitmap
