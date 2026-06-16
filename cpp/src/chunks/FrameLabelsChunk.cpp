#include "libreshockwave/chunks/FrameLabelsChunk.hpp"

#include <algorithm>
#include <cctype>

#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::chunks {
namespace {

bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
            std::tolower(static_cast<unsigned char>(rhs[index]))) {
            return false;
        }
    }
    return true;
}

} // namespace

FrameLabelsChunk::FrameLabelsChunk(const DirectorFile* file, id::ChunkId id, std::vector<FrameLabel> labels)
    : file_(file), id_(id), labels_(std::move(labels)) {}

const DirectorFile* FrameLabelsChunk::file() const { return file_; }
format::ChunkType FrameLabelsChunk::type() const { return format::ChunkType::VWLB; }
id::ChunkId FrameLabelsChunk::id() const { return id_; }
const std::vector<FrameLabelsChunk::FrameLabel>& FrameLabelsChunk::labels() const { return labels_; }

int FrameLabelsChunk::getFrameByLabel(std::string_view labelName) const {
    for (const auto& label : labels_) {
        if (equalsIgnoreCase(label.label, labelName)) {
            return label.frameNum.value();
        }
    }
    return -1;
}

std::string FrameLabelsChunk::getLabelForFrame(int frameNum) const {
    for (const auto& label : labels_) {
        if (label.frameNum.value() == frameNum) {
            return label.label;
        }
    }
    return "";
}

FrameLabelsChunk FrameLabelsChunk::read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version) {
    (void)version;
    reader.setOrder(io::ByteOrder::BigEndian);
    std::vector<FrameLabel> labels;

    if (reader.bytesLeft() < 2) {
        return FrameLabelsChunk(file, id, labels);
    }

    const int labelCount = reader.readU16();
    std::vector<int> frameNums;
    std::vector<int> labelOffsets;
    for (int index = 0; index < labelCount; ++index) {
        if (reader.bytesLeft() < 4) break;
        frameNums.push_back(reader.readU16());
        labelOffsets.push_back(reader.readU16());
    }

    if (reader.bytesLeft() < 4) {
        return FrameLabelsChunk(file, id, labels);
    }

    const int labelsSize = reader.readI32();
    const auto readSize = std::min<std::size_t>(static_cast<std::size_t>(std::max(0, labelsSize)), reader.bytesLeft());
    const auto stringData = reader.readBytes(readSize);

    std::vector<int> sortedIndices(frameNums.size());
    for (int index = 0; index < static_cast<int>(sortedIndices.size()); ++index) {
        sortedIndices[static_cast<std::size_t>(index)] = index;
    }
    std::sort(sortedIndices.begin(), sortedIndices.end(), [&](int lhs, int rhs) {
        return labelOffsets[static_cast<std::size_t>(lhs)] < labelOffsets[static_cast<std::size_t>(rhs)];
    });

    for (int sortedIndex = 0; sortedIndex < static_cast<int>(sortedIndices.size()); ++sortedIndex) {
        const int index = sortedIndices[static_cast<std::size_t>(sortedIndex)];
        const int labelOffset = labelOffsets[static_cast<std::size_t>(index)];
        const int frameNum = frameNums[static_cast<std::size_t>(index)];
        if (labelOffset < 0 || labelOffset >= static_cast<int>(stringData.size())) continue;

        int labelEnd = static_cast<int>(stringData.size());
        if (sortedIndex < static_cast<int>(sortedIndices.size()) - 1) {
            const int nextIndex = sortedIndices[static_cast<std::size_t>(sortedIndex + 1)];
            labelEnd = std::min(labelOffsets[static_cast<std::size_t>(nextIndex)], static_cast<int>(stringData.size()));
        }
        while (labelEnd > labelOffset && stringData[static_cast<std::size_t>(labelEnd - 1)] == 0) {
            --labelEnd;
        }
        if (labelEnd > labelOffset) {
            labels.push_back(FrameLabel{
                id::FrameId(std::max(1, frameNum)),
                std::string(stringData.begin() + labelOffset, stringData.begin() + labelEnd)
            });
        }
    }

    return FrameLabelsChunk(file, id, std::move(labels));
}

} // namespace libreshockwave::chunks
