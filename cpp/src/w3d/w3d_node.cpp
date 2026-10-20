#include "libreshockwave/w3d/w3d_node.hpp"

#include "libreshockwave/io/binary_reader.hpp"

namespace libreshockwave::w3d {

float W3DNode::posX() const {
    return transform.has_value() ? (*transform)[12] : 0.0F;
}

float W3DNode::posY() const {
    return transform.has_value() ? (*transform)[13] : 0.0F;
}

float W3DNode::posZ() const {
    return transform.has_value() ? (*transform)[14] : 0.0F;
}

W3DNode W3DNode::parse(const std::vector<std::uint8_t>& data) {
    if (data.size() < 4) {
        return W3DNode{"", "", 0, std::nullopt, "", "", ""};
    }

    io::BinaryReader reader(data, io::ByteOrder::LittleEndian);
    std::string name = reader.readPString16();
    std::string parent_name = reader.bytesLeft() >= 2 ? reader.readPString16() : "";
    const int flags = reader.bytesLeft() >= 4 ? reader.readI32() : 0;

    std::optional<std::array<float, 16>> transform;
    if (reader.bytesLeft() >= 64) {
        std::array<float, 16> values{};
        for (float& value : values) {
            value = reader.readF32();
        }
        transform = values;
    }

    std::string resource_name = reader.bytesLeft() >= 2 ? reader.readPString16() : "";
    std::string ref_name = reader.bytesLeft() >= 2 ? reader.readPString16() : "";
    std::string shader_name = reader.bytesLeft() >= 2 ? reader.readPString16() : "";

    return W3DNode{
        std::move(name),
        std::move(parent_name),
        flags,
        transform,
        std::move(resource_name),
        std::move(ref_name),
        std::move(shader_name)
    };
}

} // namespace libreshockwave::w3d
