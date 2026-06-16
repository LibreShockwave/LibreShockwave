#include "libreshockwave/w3d/W3DMeshResource.hpp"

#include <algorithm>

#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::w3d {

bool W3DMeshResource::hasDecodedGeometry() const {
    return !vertices.empty();
}

bool W3DMeshResource::hasTextureCoordinates() const {
    return !texCoords.empty() && texCoords.size() == vertices.size();
}

std::array<W3DVertex, 2> W3DMeshResource::bounds() const {
    if (vertices.empty()) {
        return {W3DVertex{}, W3DVertex{}};
    }

    W3DVertex min = vertices.front();
    W3DVertex max = vertices.front();
    for (const auto& vertex : vertices) {
        min.x = std::min(min.x, vertex.x);
        min.y = std::min(min.y, vertex.y);
        min.z = std::min(min.z, vertex.z);
        max.x = std::max(max.x, vertex.x);
        max.y = std::max(max.y, vertex.y);
        max.z = std::max(max.z, vertex.z);
    }
    return {min, max};
}

W3DMeshResource W3DMeshResource::parse(const std::vector<std::uint8_t>& data) {
    if (data.size() < 4) {
        return W3DMeshResource{};
    }

    io::BinaryReader reader(data, io::ByteOrder::LittleEndian);
    std::string name = reader.readPString16();
    const int vertexCount = reader.bytesLeft() >= 4 ? reader.readI32() : 0;
    const int faceCount = reader.bytesLeft() >= 4 ? reader.readI32() : 0;

    std::vector<std::uint8_t> geometryData;
    if (reader.bytesLeft() > 0) {
        geometryData = reader.readBytes(reader.bytesLeft());
    }

    std::vector<W3DVertex> vertices;
    std::vector<W3DFace> faces;
    io::BinaryReader geometryReader(geometryData, io::ByteOrder::LittleEndian);
    if (vertexCount > 0 &&
            geometryReader.bytesLeft() >= static_cast<std::size_t>(vertexCount) * 12) {
        vertices.reserve(static_cast<std::size_t>(vertexCount));
        for (int index = 0; index < vertexCount; ++index) {
            vertices.push_back(W3DVertex{
                geometryReader.readF32(),
                geometryReader.readF32(),
                geometryReader.readF32()
            });
        }
    }

    std::vector<W3DTexCoord> texCoords;
    if (faceCount > 0) {
        const auto faceCountSize = static_cast<std::size_t>(faceCount);
        const auto remaining = geometryReader.bytesLeft();
        const auto uvBytes = vertices.size() * 8;
        if (!vertices.empty() && remaining >= uvBytes) {
            const auto bytesAfterUv = remaining - uvBytes;
            if (bytesAfterUv == faceCountSize * 6 || bytesAfterUv == faceCountSize * 12) {
                texCoords.reserve(vertices.size());
                for (std::size_t index = 0; index < vertices.size(); ++index) {
                    texCoords.push_back(W3DTexCoord{
                        geometryReader.readF32(),
                        geometryReader.readF32()
                    });
                }
            }
        }

        if (geometryReader.bytesLeft() == faceCountSize * 6) {
            faces.reserve(faceCountSize);
            for (int index = 0; index < faceCount; ++index) {
                faces.push_back(W3DFace{
                    static_cast<int>(geometryReader.readU16()),
                    static_cast<int>(geometryReader.readU16()),
                    static_cast<int>(geometryReader.readU16())
                });
            }
        } else if (geometryReader.bytesLeft() == faceCountSize * 12) {
            faces.reserve(faceCountSize);
            for (int index = 0; index < faceCount; ++index) {
                faces.push_back(W3DFace{
                    geometryReader.readI32(),
                    geometryReader.readI32(),
                    geometryReader.readI32()
                });
            }
        }
    }

    return W3DMeshResource{
        std::move(name),
        vertexCount,
        faceCount,
        std::move(vertices),
        std::move(texCoords),
        std::move(faces),
        std::move(geometryData)
    };
}

} // namespace libreshockwave::w3d
