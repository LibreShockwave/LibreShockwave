#pragma once

namespace libreshockwave::w3d {

enum class W3DEntryType {
    SceneRoot,
    Version,
    BinaryData,
    ResourceRef,
    Material,
    LightData,
    Node,
    MeshResource,
    Shape,
    Unknown
};

[[nodiscard]] int code(W3DEntryType type);
[[nodiscard]] W3DEntryType w3dEntryTypeFromCode(int code);

} // namespace libreshockwave::w3d
