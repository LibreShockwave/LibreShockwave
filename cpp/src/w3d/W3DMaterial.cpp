#include "libreshockwave/w3d/W3DMaterial.hpp"

#include <array>

#include "libreshockwave/io/BinaryReader.hpp"

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
    std::vector<std::uint8_t> materialData;
    if (reader.bytesLeft() > 0) {
        materialData = reader.readBytes(reader.bytesLeft());
    }

    std::optional<std::array<float, 4>> diffuseColor;
    std::optional<std::array<float, 4>> ambientColor;
    std::optional<std::array<float, 4>> specularColor;
    std::optional<float> shininess;
    std::string textureName;

    io::BinaryReader materialReader(materialData, io::ByteOrder::LittleEndian);
    if (materialReader.bytesLeft() >= 52) {
        diffuseColor = readRgba(materialReader);
        ambientColor = readRgba(materialReader);
        specularColor = readRgba(materialReader);
        shininess = materialReader.readF32();
        if (materialReader.bytesLeft() >= 2) {
            textureName = materialReader.readPString16();
        }
    }

    return W3DMaterial{
        std::move(name),
        std::move(materialData),
        diffuseColor,
        ambientColor,
        specularColor,
        shininess,
        std::move(textureName)
    };
}

} // namespace libreshockwave::w3d
