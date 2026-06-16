#include "libreshockwave/player/media/QueuedJpegDecoder.hpp"

#include <algorithm>
#include <cstddef>

namespace libreshockwave::player::media {
namespace {

constexpr std::uint32_t crc32Polynomial = 0xEDB88320U;

} // namespace

DirectorFile::JpegDecoder QueuedJpegDecoder::decoder() {
    return [this](const std::vector<std::uint8_t>& data) {
        return decode(data);
    };
}

void QueuedJpegDecoder::install() {
    DirectorFile::setJpegDecoder(decoder());
}

std::optional<libreshockwave::bitmap::Bitmap> QueuedJpegDecoder::decode(
    const std::vector<std::uint8_t>& jpegData) {
    const int id = idFor(jpegData);
    const auto decoded = decoded_.find(id);
    if (decoded != decoded_.end()) {
        return decoded->second.copy();
    }

    if (!pending_.contains(id)) {
        pending_[id] = jpegData;
        pendingOrder_.push_back(id);
    }
    DirectorFile::markJpegDecodePending();
    return std::nullopt;
}

int QueuedJpegDecoder::pendingCount() const {
    return static_cast<int>(pendingOrder_.size());
}

int QueuedJpegDecoder::pendingId(int index) const {
    if (index < 0 || index >= pendingCount()) {
        return 0;
    }
    return pendingOrder_[static_cast<std::size_t>(index)];
}

int QueuedJpegDecoder::prepareData(int id) {
    const auto found = pending_.find(id);
    if (found == pending_.end()) {
        currentData_.reset();
        return 0;
    }
    currentData_ = found->second;
    return static_cast<int>(currentData_->size());
}

const std::vector<std::uint8_t>* QueuedJpegDecoder::currentData() const {
    return currentData_.has_value() ? &currentData_.value() : nullptr;
}

void QueuedJpegDecoder::reset() {
    pending_.clear();
    pendingOrder_.clear();
    decoded_.clear();
    currentData_.reset();
}

void QueuedJpegDecoder::deliverDecoded(int id,
                                       int width,
                                       int height,
                                       const std::vector<std::uint8_t>& rgba) {
    if (width <= 0 || height <= 0 ||
        rgba.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U) {
        removePending(id);
        return;
    }

    libreshockwave::bitmap::Bitmap bitmap(width, height, 32);
    std::size_t offset = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto r = static_cast<std::uint32_t>(rgba[offset++]);
            const auto g = static_cast<std::uint32_t>(rgba[offset++]);
            const auto b = static_cast<std::uint32_t>(rgba[offset++]);
            const auto a = static_cast<std::uint32_t>(rgba[offset++]);
            bitmap.setPixel(x, y, (a << 24U) | (r << 16U) | (g << 8U) | b);
        }
    }
    bitmap.setNativeAlpha(true);
    decoded_.insert_or_assign(id, std::move(bitmap));
    removePending(id);
    currentData_.reset();
}

int QueuedJpegDecoder::idFor(const std::vector<std::uint8_t>& data) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const auto byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0U ? (crc >> 1U) ^ crc32Polynomial : crc >> 1U;
        }
    }
    return static_cast<int>(static_cast<std::int32_t>(crc ^ 0xFFFFFFFFU));
}

void QueuedJpegDecoder::removePending(int id) {
    pending_.erase(id);
    pendingOrder_.erase(std::remove(pendingOrder_.begin(), pendingOrder_.end(), id), pendingOrder_.end());
}

} // namespace libreshockwave::player::media
