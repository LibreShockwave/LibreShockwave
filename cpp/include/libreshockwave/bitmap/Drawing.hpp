#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "libreshockwave/id/Ids.hpp"

namespace libreshockwave::bitmap {

class Bitmap;

namespace Drawing {

void fillRect(Bitmap& dest, int x, int y, int width, int height, std::uint32_t argb);
void drawRect(Bitmap& dest, int x, int y, int width, int height, std::uint32_t argb);
void drawLine(Bitmap& dest, int x0, int y0, int x1, int y1, std::uint32_t argb);
void fillEllipse(Bitmap& dest, int cx, int cy, int rx, int ry, std::uint32_t argb);
void drawEllipse(Bitmap& dest, int cx, int cy, int rx, int ry, std::uint32_t argb);
[[nodiscard]] std::shared_ptr<Bitmap> createMatte(const Bitmap& src, int alphaThreshold = 0);
[[nodiscard]] std::shared_ptr<Bitmap> createMask(const Bitmap& src, int alphaThreshold = 0);
[[nodiscard]] Bitmap applyMatteToRegion(const Bitmap& src, int x, int y, int width, int height);
[[nodiscard]] Bitmap applyFloodFillTransparency(const Bitmap& src);
[[nodiscard]] int combineAlpha(int srcAlpha, int blendAlpha);
[[nodiscard]] std::uint32_t alphaBlend(std::uint32_t fg, std::uint32_t bg, int alpha);
[[nodiscard]] std::uint32_t applyInk(std::uint32_t src,
                                     std::uint32_t dest,
                                     id::InkMode ink,
                                     int blend,
                                     int backgroundKeyRgb = 0xFFFFFF);
[[nodiscard]] bool maskAllowsPixel(const Bitmap& mask, int x, int y);
[[nodiscard]] std::shared_ptr<Bitmap> preprocessBackgroundTransparent(const Bitmap& src,
                                                                      int backgroundKeyRgb = 0xFFFFFF);
void copyPixels(Bitmap& dest,
                const Bitmap& src,
                int destX,
                int destY,
                int srcX,
                int srcY,
                int width,
                int height,
                id::InkMode ink,
                int blend,
                const Bitmap* mask = nullptr,
                int backgroundKeyRgb = 0xFFFFFF);
void copyPixels(Bitmap& dest,
                const Bitmap& src,
                int destX,
                int destY,
                id::InkMode ink,
                int blend);
[[nodiscard]] std::shared_ptr<Bitmap> transformQuadBitmap(const Bitmap& src,
                                                          int srcLeft,
                                                          int srcTop,
                                                          int srcRight,
                                                          int srcBottom,
                                                          const std::array<int, 4>& px,
                                                          const std::array<int, 4>& py,
                                                          int minX,
                                                          int minY,
                                                          int maxX,
                                                          int maxY);

} // namespace Drawing

} // namespace libreshockwave::bitmap
