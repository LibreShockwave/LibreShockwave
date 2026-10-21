#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/chunks/chunk.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

class FrameLabelsChunk final : public Chunk {
public:
    struct FrameLabel {
        id::FrameId frame_num;
        std::string label;
    };

    FrameLabelsChunk(const moviesource::MovieSource* file, id::ChunkId id, std::vector<FrameLabel> labels);

    [[nodiscard]] const moviesource::MovieSource* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] const std::vector<FrameLabel>& labels() const;
    [[nodiscard]] int getFrameByLabel(std::string_view label_name) const;
    [[nodiscard]] std::string getLabelForFrame(int frame_num) const;

    [[nodiscard]] static FrameLabelsChunk read(const moviesource::MovieSource* file, io::BinaryReader& reader, id::ChunkId id, int version);

private:
    const moviesource::MovieSource* file_;
    id::ChunkId id_;
    std::vector<FrameLabel> labels_;
};

} // namespace libreshockwave::chunks
