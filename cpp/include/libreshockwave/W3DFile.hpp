#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "libreshockwave/w3d/W3DEntry.hpp"
#include "libreshockwave/w3d/W3DMaterial.hpp"
#include "libreshockwave/w3d/W3DMeshResource.hpp"
#include "libreshockwave/w3d/W3DNode.hpp"
#include "libreshockwave/w3d/W3DResourceRef.hpp"
#include "libreshockwave/w3d/W3DShape.hpp"
#include "libreshockwave/w3d/W3DTexture.hpp"

namespace libreshockwave {

struct W3DRenderableMesh {
    w3d::W3DNode node;
    w3d::W3DMeshResource mesh;
    std::optional<w3d::W3DMaterial> material;
    std::optional<w3d::W3DTexture> texture;
    std::optional<w3d::W3DResourceRef> resourceRef;
    std::array<float, 16> worldTransform{};
    std::array<w3d::W3DVertex, 2> localBounds{};
    std::array<w3d::W3DVertex, 2> worldBounds{};
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
    [[nodiscard]] std::optional<w3d::W3DMeshResource> meshResourceForNode(std::string_view nodeName) const;
    [[nodiscard]] std::optional<w3d::W3DMaterial> materialForNode(std::string_view nodeName) const;
    [[nodiscard]] std::optional<w3d::W3DTexture> textureForMaterial(std::string_view materialName) const;
    [[nodiscard]] std::optional<w3d::W3DTexture> textureForNode(std::string_view nodeName) const;
    [[nodiscard]] std::optional<w3d::W3DResourceRef> resourceRefForNode(std::string_view nodeName) const;
    [[nodiscard]] std::optional<W3DRenderableMesh> renderableMeshForNode(std::string_view nodeName) const;
    [[nodiscard]] std::vector<W3DRenderableMesh> renderableMeshes() const;
    [[nodiscard]] std::vector<w3d::W3DNode> childNodes(std::string_view parentName) const;
    [[nodiscard]] std::vector<w3d::W3DShape> childShapes(std::string_view parentName) const;
    [[nodiscard]] std::optional<std::array<float, 16>> worldTransformForNode(std::string_view name) const;
    [[nodiscard]] std::optional<std::array<float, 16>> worldTransformForShape(std::string_view name) const;

private:
    void parse(const std::vector<std::uint8_t>& data);

    int version_ = 0;
    std::vector<w3d::W3DEntry> entries_;
    std::vector<w3d::W3DNode> nodes_;
    std::vector<w3d::W3DShape> shapes_;
    std::vector<w3d::W3DMeshResource> meshResources_;
    std::vector<w3d::W3DTexture> textures_;
    std::vector<w3d::W3DMaterial> materials_;
    std::vector<w3d::W3DResourceRef> resourceRefs_;
};

} // namespace libreshockwave
