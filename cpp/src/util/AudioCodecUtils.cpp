#include "libreshockwave/util/AudioCodecUtils.hpp"

#include <algorithm>

namespace libreshockwave::util {

bool containsMp3SyncFrame(const std::vector<std::uint8_t>& data, int searchLimit) {
    if (data.size() < 4) {
        return false;
    }

    const int limit = std::min(static_cast<int>(data.size()) - 4, searchLimit);
    for (int index = 0; index < limit; ++index) {
        if (data[static_cast<std::size_t>(index)] == 0xFF &&
            (data[static_cast<std::size_t>(index + 1)] & 0xE0U) == 0xE0U) {
            const int version = (data[static_cast<std::size_t>(index + 1)] >> 3) & 3;
            const int layer = (data[static_cast<std::size_t>(index + 1)] >> 1) & 3;
            const int bitrateIndex = (data[static_cast<std::size_t>(index + 2)] >> 4) & 0xF;
            const int sampleRateIndex = (data[static_cast<std::size_t>(index + 2)] >> 2) & 3;

            if (version != 1 && layer != 0 && bitrateIndex != 0 && bitrateIndex != 15 && sampleRateIndex != 3) {
                return true;
            }
        }
    }
    return false;
}

} // namespace libreshockwave::util
