#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/bitmap/Bitmap.hpp"

namespace libreshockwave::player::media {

class QueuedJpegDecoder {
public:
    [[nodiscard]] DirectorFile::JpegDecoder decoder();
    void install();

    [[nodiscard]] std::optional<libreshockwave::bitmap::Bitmap> decode(
        const std::vector<std::uint8_t>& jpegData);

    [[nodiscard]] int pendingCount() const;
    [[nodiscard]] int pendingId(int index) const;
    int prepareData(int id);
    [[nodiscard]] const std::vector<std::uint8_t>* currentData() const;
    void reset();
    void deliverDecoded(int id, int width, int height, const std::vector<std::uint8_t>& rgba);

    [[nodiscard]] static int idFor(const std::vector<std::uint8_t>& data);

private:
    void removePending(int id);

    std::map<int, std::vector<std::uint8_t>> pending_;
    std::vector<int> pendingOrder_;
    std::map<int, libreshockwave::bitmap::Bitmap> decoded_;
    std::optional<std::vector<std::uint8_t>> currentData_;
};

} // namespace libreshockwave::player::media
