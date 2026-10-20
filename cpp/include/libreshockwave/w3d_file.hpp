#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "libreshockwave/w3d/w3d_entry.hpp"
#include "libreshockwave/w3d/w3d_material.hpp"
#include "libreshockwave/w3d/w3d_mesh_resource.hpp"
#include "libreshockwave/w3d/w3d_node.hpp"
#include "libreshockwave/w3d/w3d_resource_ref.hpp"
#include "libreshockwave/w3d/w3d_shape.hpp"
#include "libreshockwave/w3d/w3d_texture.hpp"

namespace libreshockwave {

struct W3DRenderableMesh {
    w3d::W3DNode node;
    w3d::W3DMeshResource mesh;
    std::optional<w3d::W3DMaterial> material;
    std::optional<w3d::W3DTexture> texture;
    std::optional<w3d::W3DResourceRef> resource_ref;
    std::array<float, 16> world_transform{};
    std::array<w3d::W3DVertex, 2> local_bounds{};
    std::array<w3d::W3DVertex, 2> world_bounds{};
};

class W3DFile {
public:
    [[nodiscard]] static W3DFile load(const std::vector<std::uint8_t>& data);
    [[nodiscard]] static W3DFile load(const std::filesystem::path& path);

    [[nodiscard]] int version() const;
    [[nodiscard]] bool hasSceneData() const;
    [[nodiscard]] const std::vector<w3d::W3DEntry>& entries() const;
    [[nodiscard]] const std::vector<w3d::W3DNode>& nodes() const;
    [[nodiscard]] const std::vector<w3d::W3DShape>& shapes() const;
    [[nodiscard]] const std::vector<w3d::W3DMeshResource>& meshResources() const;
    [[nodiscard]] const std::vector<w3d::W3DTexture>& textures() const;
    [[nodiscard]] const std::vector<w3d::W3DMaterial>& materials() const;
    [[nodiscard]] const std::vector<w3d::W3DResourceRef>& resourceRefs() const;

    [[nodiscard]] std::optional<w3d::W3DNode> findNode(std::string_view name) const;
    [[nodiscard]] std::optional<w3d::W3DMeshResource> findMeshResource(std::string_view name) const;
    [[nodiscard]] std::optional<w3d::W3DTexture> findTexture(std::string_view name) const;
    [[nodiscard]] std::optional<w3d::W3DMaterial> findMaterial(std::string_view name) const;
    [[nodiscard]] std::optional<w3d::W3DResourceRef> findResourceRef(std::string_view name) const;
    [[nodiscard]] std::optional<w3d::W3DMeshResource> meshResourceForNode(std::string_view node_name) const;
    [[nodiscard]] std::optional<w3d::W3DMaterial> materialForNode(std::string_view node_name) const;
    [[nodiscard]] std::optional<w3d::W3DTexture> textureForMaterial(std::string_view material_name) const;
    [[nodiscard]] std::optional<w3d::W3DTexture> textureForNode(std::string_view node_name) const;
    [[nodiscard]] std::optional<w3d::W3DResourceRef> resourceRefForNode(std::string_view node_name) const;
    [[nodiscard]] std::optional<W3DRenderableMesh> renderableMeshForNode(std::string_view node_name) const;
    [[nodiscard]] std::vector<W3DRenderableMesh> renderableMeshes() const;
    [[nodiscard]] std::vector<w3d::W3DNode> childNodes(std::string_view parent_name) const;
    [[nodiscard]] std::vector<w3d::W3DShape> childShapes(std::string_view parent_name) const;
    [[nodiscard]] std::optional<std::array<float, 16>> worldTransformForNode(std::string_view name) const;
    [[nodiscard]] std::optional<std::array<float, 16>> worldTransformForShape(std::string_view name) const;

private:
    void parse(const std::vector<std::uint8_t>& data);

    int version_ = 0;
    std::vector<w3d::W3DEntry> entries_;
    std::vector<w3d::W3DNode> nodes_;
    std::vector<w3d::W3DShape> shapes_;
    std::vector<w3d::W3DMeshResource> mesh_resources_;
    std::vector<w3d::W3DTexture> textures_;
    std::vector<w3d::W3DMaterial> materials_;
    std::vector<w3d::W3DResourceRef> resource_refs_;
};

} // namespace libreshockwave
