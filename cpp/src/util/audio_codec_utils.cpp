#include "libreshockwave/util/audio_codec_utils.hpp"

#include <algorithm>

namespace libreshockwave::util {

bool containsMp3SyncFrame(const std::vector<std::uint8_t>& data, int search_limit) {
    if (data.size() < 4) {
        return false;
    }

    const int limit = std::min(static_cast<int>(data.size()) - 4, search_limit);
    for (int index = 0; index < limit; ++index) {
        if (data[static_cast<std::size_t>(index)] == 0xFF &&
            (data[static_cast<std::size_t>(index + 1)] & 0xE0U) == 0xE0U) {
            const int version = (data[static_cast<std::size_t>(index + 1)] >> 3) & 3;
            const int layer = (data[static_cast<std::size_t>(index + 1)] >> 1) & 3;
            const int bitrate_index = (data[static_cast<std::size_t>(index + 2)] >> 4) & 0xF;
            const int sample_rate_index = (data[static_cast<std::size_t>(index + 2)] >> 2) & 3;

            if (version != 1 && layer != 0 && bitrate_index != 0 && bitrate_index != 15 && sample_rate_index != 3) {
                return true;
            }
        }
    }
    return false;
}

} // namespace libreshockwave::util
