#include "libreshockwave/player/media/queued_jpeg_decoder.hpp"

#include <algorithm>
#include <cstddef>

namespace libreshockwave::player::media {
namespace {

constexpr std::uint32_t crc32_polynomial = 0xEDB88320U;

} // namespace

moviesource::MovieSource::JpegDecoder QueuedJpegDecoder::decoder() {
    return [this](const std::vector<std::uint8_t>& data) {
        return decode(data);
    };
}

void QueuedJpegDecoder::install() {
    if (source_ != nullptr) { source_->setJpegDecoder(decoder()); }
}

std::optional<libreshockwave::bitmap::Bitmap> QueuedJpegDecoder::decode(
    const std::vector<std::uint8_t>& jpeg_data) {
    const int id = idFor(jpeg_data);
    const auto decoded = decoded_.find(id);
    if (decoded != decoded_.end()) {
        return decoded->second.copy();
    }

    if (!pending_.contains(id)) {
        pending_[id] = jpeg_data;
        pending_order_.push_back(id);
    }
    if (source_ != nullptr) { source_->markJpegDecodePending(); }
    return std::nullopt;
}

int QueuedJpegDecoder::pendingCount() const {
    return static_cast<int>(pending_order_.size());
}

int QueuedJpegDecoder::pendingId(int index) const {
    if (index < 0 || index >= pendingCount()) {
        return 0;
    }
    return pending_order_[static_cast<std::size_t>(index)];
}

int QueuedJpegDecoder::prepareData(int id) {
    const auto found = pending_.find(id);
    if (found == pending_.end()) {
        current_data_.reset();
        return 0;
    }
    current_data_ = found->second;
    return static_cast<int>(current_data_->size());
}

const std::vector<std::uint8_t>* QueuedJpegDecoder::currentData() const {
    return current_data_.has_value() ? &current_data_.value() : nullptr;
}

void QueuedJpegDecoder::reset() {
    pending_.clear();
    pending_order_.clear();
    decoded_.clear();
    current_data_.reset();
    if (source_ != nullptr) { source_->clearJpegDecodePending(); }
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
    current_data_.reset();
}

int QueuedJpegDecoder::idFor(const std::vector<std::uint8_t>& data) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const auto byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0U ? (crc >> 1U) ^ crc32_polynomial : crc >> 1U;
        }
    }
    return static_cast<int>(static_cast<std::int32_t>(crc ^ 0xFFFFFFFFU));
}

void QueuedJpegDecoder::removePending(int id) {
    pending_.erase(id);
    pending_order_.erase(std::remove(pending_order_.begin(), pending_order_.end(), id), pending_order_.end());
    if (pending_order_.empty()) {
        if (source_ != nullptr) { source_->clearJpegDecodePending(); }
    }
}

} // namespace libreshockwave::player::media
