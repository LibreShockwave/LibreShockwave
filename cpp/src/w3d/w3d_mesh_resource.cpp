#include "libreshockwave/w3d/w3d_mesh_resource.hpp"

#include <algorithm>

#include "libreshockwave/io/binary_reader.hpp"

namespace libreshockwave::w3d {

bool W3DMeshResource::hasDecodedGeometry() const {
    return !vertices.empty();
}

bool W3DMeshResource::hasTextureCoordinates() const {
    return !tex_coords.empty() && tex_coords.size() == vertices.size();
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
    const int vertex_count = reader.bytesLeft() >= 4 ? reader.readI32() : 0;
    const int face_count = reader.bytesLeft() >= 4 ? reader.readI32() : 0;

    std::vector<std::uint8_t> geometry_data;
    if (reader.bytesLeft() > 0) {
        geometry_data = reader.readBytes(reader.bytesLeft());
    }

    std::vector<W3DVertex> vertices;
    std::vector<W3DFace> faces;
    io::BinaryReader geometry_reader(geometry_data, io::ByteOrder::LittleEndian);
    if (vertex_count > 0 &&
            geometry_reader.bytesLeft() >= static_cast<std::size_t>(vertex_count) * 12) {
        vertices.reserve(static_cast<std::size_t>(vertex_count));
        for (int index = 0; index < vertex_count; ++index) {
            vertices.push_back(W3DVertex{
                geometry_reader.readF32(),
                geometry_reader.readF32(),
                geometry_reader.readF32()
            });
        }
    }

    std::vector<W3DTexCoord> tex_coords;
    if (face_count > 0) {
        const auto face_count_size = static_cast<std::size_t>(face_count);
        const auto remaining = geometry_reader.bytesLeft();
        const auto uv_bytes = vertices.size() * 8;
        if (!vertices.empty() && remaining >= uv_bytes) {
            const auto bytes_after_uv = remaining - uv_bytes;
            if (bytes_after_uv == face_count_size * 6 || bytes_after_uv == face_count_size * 12) {
                tex_coords.reserve(vertices.size());
                for (std::size_t index = 0; index < vertices.size(); ++index) {
                    tex_coords.push_back(W3DTexCoord{
                        geometry_reader.readF32(),
                        geometry_reader.readF32()
                    });
                }
            }
        }

        if (geometry_reader.bytesLeft() == face_count_size * 6) {
            faces.reserve(face_count_size);
            for (int index = 0; index < face_count; ++index) {
                faces.push_back(W3DFace{
                    static_cast<int>(geometry_reader.readU16()),
                    static_cast<int>(geometry_reader.readU16()),
                    static_cast<int>(geometry_reader.readU16())
                });
            }
        } else if (geometry_reader.bytesLeft() == face_count_size * 12) {
            faces.reserve(face_count_size);
            for (int index = 0; index < face_count; ++index) {
                faces.push_back(W3DFace{
                    geometry_reader.readI32(),
                    geometry_reader.readI32(),
                    geometry_reader.readI32()
                });
            }
        }
    }

    return W3DMeshResource{
        std::move(name),
        vertex_count,
        face_count,
        std::move(vertices),
        std::move(tex_coords),
        std::move(faces),
        std::move(geometry_data)
    };
}

} // namespace libreshockwave::w3d
