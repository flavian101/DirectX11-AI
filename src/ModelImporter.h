#pragma once
// Reads a model file (glTF/GLB, FBX, OBJ...) with Assimp and turns it into plain CPU data: vertices,
// indices, and a description of each material. It does NOT touch Direct3D: uploading to the GPU is
// ModelObject's job. Keeping the two apart means the importer can be tested without a graphics device.
#include "Vertex.h"
#include <DirectXMath.h>
#include <cstdint>
#include <string>
#include <vector>

// A decoded image that did not come from a separate file (embedded in a .glb, or a repacked map).
struct ImportedImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;   // width * height * 4
};

// Where one map's pixels come from: a file on disk, or one of ImportedModel::images.
struct ImportedTextureRef {
    std::string path;     // a file that exists (already resolved against the model's folder), or empty
    int embedded = -1;    // or an index into ImportedModel::images
    bool IsSet() const { return embedded >= 0 || !path.empty(); }
};

struct ImportedMaterial {
    std::string name;
    ImportedTextureRef albedo;
    ImportedTextureRef normal;     // tangent-space; OpenGL-style green (glTF and most files)
    ImportedTextureRef orm;        // R = occlusion, G = roughness, B = metallic (repacked from glTF's two maps)
    ImportedTextureRef emissive;
    DirectX::XMFLOAT4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };   // linear factor that multiplies the albedo
    DirectX::XMFLOAT3 emissiveColor = { 0.0f, 0.0f, 0.0f };     // linear
    float roughness = 0.5f;        // factor: multiplies the ORM map's roughness, or IS the roughness if there is no map
    float metallic = 0.0f;         // read now, used by the IBL/PBR patch
    float alphaCutoff = 0.0f;      // 0 = opaque; glTF "MASK" and "BLEND" materials become a cut-out at 0.5
    bool doubleSided = false;
};

// One drawable piece: one Assimp mesh, already placed in model space (the node hierarchy is baked in).
struct ImportedPart {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    int material = 0;   // index into ImportedModel::materials
};

struct ImportedModel {
    std::string name;          // file name without extension
    std::string sourcePath;
    std::vector<ImportedPart> parts;
    std::vector<ImportedMaterial> materials;
    std::vector<ImportedImage> images;
    DirectX::XMFLOAT3 boundsMin = { 0.0f, 0.0f, 0.0f };   // model space
    DirectX::XMFLOAT3 boundsMax = { 0.0f, 0.0f, 0.0f };
    std::uint32_t triangleCount = 0;
};

// Returns false and fills `error` (a sentence for the user) if the file cannot be read or has no geometry.
// The result is in the engine's conventions: left-handed, clockwise front faces, V running downward.
bool ImportModel(const std::string& path, ImportedModel& out, std::string& error);
