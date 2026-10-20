#include "libreshockwave/w3d/w3d_material.hpp"

#include <array>

#include "libreshockwave/io/binary_reader.hpp"

namespace libreshockwave::w3d {
namespace {

std::array<float, 4> readRgba(io::BinaryReader& reader) {
    return {
        reader.readF32(),
        reader.readF32(),
        reader.readF32(),
        reader.readF32()
    };
}

} // namespace

W3DMaterial W3DMaterial::parse(const std::vector<std::uint8_t>& data) {
    if (data.size() < 4) {
        return W3DMaterial{};
    }

    io::BinaryReader reader(data, io::ByteOrder::LittleEndian);
    std::string name = reader.readPString16();
    std::vector<std::uint8_t> material_data;
    if (reader.bytesLeft() > 0) {
        material_data = reader.readBytes(reader.bytesLeft());
    }

    std::optional<std::array<float, 4>> diffuse_color;
    std::optional<std::array<float, 4>> ambient_color;
    std::optional<std::array<float, 4>> specular_color;
    std::optional<float> shininess;
    std::string texture_name;

    io::BinaryReader material_reader(material_data, io::ByteOrder::LittleEndian);
    if (material_reader.bytesLeft() >= 52) {
        diffuse_color = readRgba(material_reader);
        ambient_color = readRgba(material_reader);
        specular_color = readRgba(material_reader);
        shininess = material_reader.readF32();
        if (material_reader.bytesLeft() >= 2) {
            texture_name = material_reader.readPString16();
        }
    }

    return W3DMaterial{
        std::move(name),
        std::move(material_data),
        diffuse_color,
        ambient_color,
        specular_color,
        shininess,
        std::move(texture_name)
    };
}

} // namespace libreshockwave::w3d
