#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/chunks/Chunk.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

class FrameLabelsChunk final : public Chunk {
public:
    struct FrameLabel {
        id::FrameId frameNum;
        std::string label;
    };

    FrameLabelsChunk(const DirectorFile* file, id::ChunkId id, std::vector<FrameLabel> labels);

    [[nodiscard]] const DirectorFile* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] const std::vector<FrameLabel>& labels() const;
    [[nodiscard]] int getFrameByLabel(std::string_view labelName) const;
    [[nodiscard]] std::string getLabelForFrame(int frameNum) const;

    [[nodiscard]] static FrameLabelsChunk read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version);

private:
    const DirectorFile* file_;
    id::ChunkId id_;
    std::vector<FrameLabel> labels_;
};

} // namespace libreshockwave::chunks
