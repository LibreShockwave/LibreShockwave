#include "libreshockwave/chunks/frame_labels_chunk.hpp"

#include <algorithm>
#include <cctype>

#include "libreshockwave/io/binary_reader.hpp"

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

int FrameLabelsChunk::getFrameByLabel(std::string_view label_name) const {
    for (const auto& label : labels_) {
        if (equalsIgnoreCase(label.label, label_name)) {
            return label.frame_num.value();
        }
    }
    return -1;
}

std::string FrameLabelsChunk::getLabelForFrame(int frame_num) const {
    for (const auto& label : labels_) {
        if (label.frame_num.value() == frame_num) {
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

    const int label_count = reader.readU16();
    std::vector<int> frame_nums;
    std::vector<int> label_offsets;
    for (int index = 0; index < label_count; ++index) {
        if (reader.bytesLeft() < 4) break;
        frame_nums.push_back(reader.readU16());
        label_offsets.push_back(reader.readU16());
    }

    if (reader.bytesLeft() < 4) {
        return FrameLabelsChunk(file, id, labels);
    }

    const int labels_size = reader.readI32();
    const auto read_size = std::min<std::size_t>(static_cast<std::size_t>(std::max(0, labels_size)), reader.bytesLeft());
    const auto string_data = reader.readBytes(read_size);

    std::vector<int> sorted_indices(frame_nums.size());
    for (int index = 0; index < static_cast<int>(sorted_indices.size()); ++index) {
        sorted_indices[static_cast<std::size_t>(index)] = index;
    }
    std::sort(sorted_indices.begin(), sorted_indices.end(), [&](int lhs, int rhs) {
        return label_offsets[static_cast<std::size_t>(lhs)] < label_offsets[static_cast<std::size_t>(rhs)];
    });

    for (int sorted_index = 0; sorted_index < static_cast<int>(sorted_indices.size()); ++sorted_index) {
        const int index = sorted_indices[static_cast<std::size_t>(sorted_index)];
        const int label_offset = label_offsets[static_cast<std::size_t>(index)];
        const int frame_num = frame_nums[static_cast<std::size_t>(index)];
        if (label_offset < 0 || label_offset >= static_cast<int>(string_data.size())) continue;

        int label_end = static_cast<int>(string_data.size());
        if (sorted_index < static_cast<int>(sorted_indices.size()) - 1) {
            const int next_index = sorted_indices[static_cast<std::size_t>(sorted_index + 1)];
            label_end = std::min(label_offsets[static_cast<std::size_t>(next_index)], static_cast<int>(string_data.size()));
        }
        while (label_end > label_offset && string_data[static_cast<std::size_t>(label_end - 1)] == 0) {
            --label_end;
        }
        if (label_end > label_offset) {
            labels.push_back(FrameLabel{
                id::FrameId(std::max(1, frame_num)),
                std::string(string_data.begin() + label_offset, string_data.begin() + label_end)
            });
        }
    }

    return FrameLabelsChunk(file, id, std::move(labels));
}

} // namespace libreshockwave::chunks
