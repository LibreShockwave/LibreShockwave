#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace libreshockwave::w3d {

struct W3DVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct W3DTexCoord {
    float u = 0.0F;
    float v = 0.0F;
};

struct W3DFace {
    int a = 0;
    int b = 0;
    int c = 0;
};

struct W3DMeshResource {
    std::string name;
    int vertexCount = 0;
    int faceCount = 0;
    std::vector<W3DVertex> vertices;
    std::vector<W3DTexCoord> texCoords;
    std::vector<W3DFace> faces;
    std::vector<std::uint8_t> geometryData;

    [[nodiscard]] bool hasDecodedGeometry() const;
    [[nodiscard]] bool hasTextureCoordinates() const;
    [[nodiscard]] std::array<W3DVertex, 2> bounds() const;

    [[nodiscard]] static W3DMeshResource parse(const std::vector<std::uint8_t>& data);
};

} // namespace libreshockwave::w3d
