#include "libreshockwave/w3d/W3DEntryType.hpp"

namespace libreshockwave::w3d {

int code(W3DEntryType type) {
    switch (type) {
        case W3DEntryType::SceneRoot:
            return 0x01;
        case W3DEntryType::Version:
            return 0x02;
        case W3DEntryType::BinaryData:
            return 0x21;
        case W3DEntryType::ResourceRef:
            return 0x48;
        case W3DEntryType::Material:
            return 0x49;
        case W3DEntryType::LightData:
            return 0x71;
        case W3DEntryType::Node:
            return 0x72;
        case W3DEntryType::MeshResource:
            return 0x73;
        case W3DEntryType::Shape:
            return 0x74;
        case W3DEntryType::Unknown:
            return -1;
    }
    return -1;
}

W3DEntryType w3dEntryTypeFromCode(int code) {
    switch (code) {
        case 0x01:
            return W3DEntryType::SceneRoot;
        case 0x02:
            return W3DEntryType::Version;
        case 0x21:
            return W3DEntryType::BinaryData;
        case 0x48:
            return W3DEntryType::ResourceRef;
        case 0x49:
            return W3DEntryType::Material;
        case 0x71:
            return W3DEntryType::LightData;
        case 0x72:
            return W3DEntryType::Node;
        case 0x73:
            return W3DEntryType::MeshResource;
        case 0x74:
            return W3DEntryType::Shape;
        default:
            return W3DEntryType::Unknown;
    }
}

} // namespace libreshockwave::w3d
