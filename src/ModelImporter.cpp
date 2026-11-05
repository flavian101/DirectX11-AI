#include "ModelImporter.h"
#include "libs/stb_image.h"
#include <algorithm>
#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>

using namespace DirectX;
namespace fs = std::filesystem;

namespace {

std::string LowerExtension(const fs::path& path)
{
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

// "my%20texture.png" -> "my texture.png". glTF stores texture paths as URIs.
std::string PercentDecode(const std::string& text)
{
    std::string result;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() + 0 && std::isxdigit(static_cast<unsigned char>(text[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            result += static_cast<char>(std::stoi(text.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            result += text[i];
        }
    }
    return result;
}

// Texture paths inside a model file are often wrong for THIS machine (absolute paths from the artist's
// PC, backslashes, URI escapes). Try the path as written, then just the file name next to the model,
// then in a "textures" folder beside it.
std::string ResolveTexturePath(const fs::path& modelDir, std::string raw)
{
    std::replace(raw.begin(), raw.end(), '\\', '/');
    raw = PercentDecode(raw);
    const fs::path asWritten(raw);
    std::error_code error;
    const fs::path candidates[] = {
        asWritten.is_absolute() ? asWritten : modelDir / asWritten,
        modelDir / asWritten.filename(),
        modelDir / "textures" / asWritten.filename(),
        modelDir / "Textures" / asWritten.filename(),
    };
    for (const fs::path& candidate : candidates) {
        if (fs::is_regular_file(candidate, error)) return candidate.generic_string();
    }
    return {};
}

bool DecodeEmbedded(const aiTexture* texture, ImportedImage& image)
{
    if (!texture) return false;
    if (texture->mHeight == 0) {
        // Compressed (a PNG / JPG stored inside the file): mWidth is the byte count.
        int channels = 0;
        unsigned char* pixels = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(texture->pcData),
                                                      static_cast<int>(texture->mWidth), &image.width, &image.height,
                                                      &channels, STBI_rgb_alpha);
        if (!pixels) return false;
        image.rgba.assign(pixels, pixels + static_cast<size_t>(image.width) * image.height * 4);
        stbi_image_free(pixels);
        return true;
    }
    // Raw: width * height texels, each stored b, g, r, a.
    image.width = static_cast<int>(texture->mWidth);
    image.height = static_cast<int>(texture->mHeight);
    image.rgba.resize(static_cast<size_t>(image.width) * image.height * 4);
    for (size_t i = 0; i < static_cast<size_t>(image.width) * image.height; ++i) {
        image.rgba[i * 4 + 0] = texture->pcData[i].r;
        image.rgba[i * 4 + 1] = texture->pcData[i].g;
        image.rgba[i * 4 + 2] = texture->pcData[i].b;
        image.rgba[i * 4 + 3] = texture->pcData[i].a;
    }
    return true;
}

bool LoadRef(const ImportedModel& model, const ImportedTextureRef& ref, ImportedImage& image)
{
    if (ref.embedded >= 0 && ref.embedded < static_cast<int>(model.images.size())) {
        image = model.images[ref.embedded];
        return true;
    }
    if (ref.path.empty()) return false;
    int channels = 0;
    unsigned char* pixels = stbi_load(ref.path.c_str(), &image.width, &image.height, &channels, STBI_rgb_alpha);
    if (!pixels) return false;
    image.rgba.assign(pixels, pixels + static_cast<size_t>(image.width) * image.height * 4);
    stbi_image_free(pixels);
    return true;
}

// Normal maps are mostly (0.5, 0.5, 1.0): a blue picture. OBJ files put BOTH height maps and normal maps
// under the same "bump" keyword, so look at the pixels to tell which one this is.
bool LooksLikeNormalMap(const ImportedImage& image)
{
    if (image.rgba.empty()) return false;
    double r = 0, g = 0, b = 0;
    size_t samples = 0;
    const size_t pixels = static_cast<size_t>(image.width) * image.height;
    const size_t step = (std::max<size_t>)(1, pixels / 4096);
    for (size_t i = 0; i < pixels; i += step, ++samples) {
        r += image.rgba[i * 4 + 0]; g += image.rgba[i * 4 + 1]; b += image.rgba[i * 4 + 2];
    }
    if (samples == 0) return false;
    r /= samples; g /= samples; b /= samples;
    return std::fabs(r - 128.0) < 40.0 && std::fabs(g - 128.0) < 40.0 && b > 190.0;
}

// glTF keeps occlusion and metallic-roughness in two maps (often the same file, often not); our shader
// wants one ORM map. If they are different, build the packed one here. The R channel of a glTF
// metallic-roughness map is undefined, so it must ALWAYS be overwritten (white = no occlusion).
bool PackOrm(ImportedModel& model, const ImportedTextureRef& metalRough, const ImportedTextureRef& occlusion,
             ImportedTextureRef& result)
{
    if (metalRough.IsSet() && occlusion.IsSet() && metalRough.path == occlusion.path && metalRough.embedded == occlusion.embedded) {
        result = metalRough;   // one file already holds both: use it as it is
        return true;
    }
    ImportedImage packed;
    if (metalRough.IsSet()) {
        if (!LoadRef(model, metalRough, packed)) return false;
    } else if (occlusion.IsSet()) {
        // Occlusion only: start from "medium roughness, not metal".
        ImportedImage ao;
        if (!LoadRef(model, occlusion, ao)) return false;
        packed.width = ao.width;
        packed.height = ao.height;
        packed.rgba.assign(ao.rgba.size(), 0);
        for (size_t i = 0; i < packed.rgba.size(); i += 4) { packed.rgba[i + 1] = 255; packed.rgba[i + 3] = 255; }   // roughness factor does the rest
    } else {
        return false;
    }

    ImportedImage ao;
    const bool haveAo = occlusion.IsSet() && LoadRef(model, occlusion, ao);
    for (int y = 0; y < packed.height; ++y) {
        for (int x = 0; x < packed.width; ++x) {
            std::uint8_t occlusionValue = 255;
            if (haveAo) {
                // Nearest-neighbour is plenty for an occlusion map whose size differs from the other.
                const int ax = (std::min)(ao.width - 1, x * ao.width / packed.width);
                const int ay = (std::min)(ao.height - 1, y * ao.height / packed.height);
                occlusionValue = ao.rgba[(static_cast<size_t>(ay) * ao.width + ax) * 4];
            }
            packed.rgba[(static_cast<size_t>(y) * packed.width + x) * 4] = occlusionValue;
        }
    }
    model.images.push_back(std::move(packed));
    result = ImportedTextureRef{};
    result.embedded = static_cast<int>(model.images.size()) - 1;
    return true;
}

struct MaterialConverter {
    const aiScene* scene;
    fs::path modelDir;
    bool isGltf;
    ImportedModel& model;
    std::map<const aiTexture*, int> embeddedIndex;   // an embedded image used twice is decoded once

    ImportedTextureRef FirstTexture(const aiMaterial* material, aiTextureType type)
    {
        ImportedTextureRef ref;
        aiString path;
        if (material->GetTextureCount(type) == 0 || material->GetTexture(type, 0, &path) != AI_SUCCESS) return ref;
        if (const aiTexture* embedded = scene->GetEmbeddedTexture(path.C_Str())) {
            auto found = embeddedIndex.find(embedded);
            if (found == embeddedIndex.end()) {
                ImportedImage image;
                if (!DecodeEmbedded(embedded, image)) return ref;
                model.images.push_back(std::move(image));
                found = embeddedIndex.emplace(embedded, static_cast<int>(model.images.size()) - 1).first;
            }
            ref.embedded = found->second;
        } else {
            ref.path = ResolveTexturePath(modelDir, path.C_Str());
        }
        return ref;
    }

    ImportedMaterial Convert(const aiMaterial* material)
    {
        ImportedMaterial result;
        aiString name;
        if (material->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) result.name = name.C_Str();

        result.albedo = FirstTexture(material, aiTextureType_BASE_COLOR);
        if (!result.albedo.IsSet()) result.albedo = FirstTexture(material, aiTextureType_DIFFUSE);
        result.normal = FirstTexture(material, aiTextureType_NORMALS);
        if (!result.normal.IsSet()) {
            const ImportedTextureRef bump = FirstTexture(material, aiTextureType_HEIGHT);
            ImportedImage image;
            if (bump.IsSet() && LoadRef(model, bump, image) && LooksLikeNormalMap(image)) result.normal = bump;
        }
        result.emissive = FirstTexture(material, aiTextureType_EMISSIVE);

        aiColor4D color;
        if (material->Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS || material->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
            result.baseColor = XMFLOAT4(color.r, color.g, color.b, color.a);
        }
        aiColor3D emissive;
        if (material->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) result.emissiveColor = XMFLOAT3(emissive.r, emissive.g, emissive.b);

        int twoSided = 0;
        if (material->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) result.doubleSided = twoSided != 0;

        if (isGltf) {
            // glTF's metallic-roughness texture sits in the "unknown" slot and occlusion in "lightmap";
            // in other formats those slots mean something else, so only read them for glTF.
            const ImportedTextureRef metalRough = FirstTexture(material, aiTextureType_UNKNOWN);
            const ImportedTextureRef occlusion = FirstTexture(material, aiTextureType_LIGHTMAP);
            if (metalRough.IsSet() || occlusion.IsSet()) PackOrm(model, metalRough, occlusion, result.orm);

            float factor = 0.0f;
            result.roughness = (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, factor) == AI_SUCCESS) ? factor : 1.0f;   // glTF default
            result.metallic = (material->Get(AI_MATKEY_METALLIC_FACTOR, factor) == AI_SUCCESS) ? factor : 1.0f;

            aiString mode;
            if (material->Get(AI_MATKEY_GLTF_ALPHAMODE, mode) == AI_SUCCESS) {
                const std::string alphaMode = mode.C_Str();
                if (alphaMode == "MASK") {
                    float cutoff = 0.5f;
                    material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, cutoff);
                    result.alphaCutoff = (std::max)(cutoff, 0.01f);
                } else if (alphaMode == "BLEND") {
                    result.alphaCutoff = 0.5f;   // true blending needs sorting; a cut-out is the cheap honest approximation
                } else {
                    result.baseColor.w = 1.0f;   // OPAQUE: whatever the alpha says, ignore it
                }
            }
        }
        return result;
    }
};

}  // namespace

bool ImportModel(const std::string& path, ImportedModel& out, std::string& error)
{
    out = ImportedModel{};
    const fs::path file(path);
    std::error_code fsError;
    if (!fs::is_regular_file(file, fsError)) {
        error = "File not found: " + path;
        return false;
    }

    Assimp::Importer importer;
    // Points and lines are not drawable surfaces: drop them instead of crashing on them later.
    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);
    const unsigned int flags =
        aiProcess_Triangulate |             // everything becomes triangles
        aiProcess_JoinIdenticalVertices |   // share vertices between triangles (smaller, and smooth normals work)
        aiProcess_GenSmoothNormals |        // only if the file has no normals
        aiProcess_SortByPType |
        aiProcess_GenUVCoords |
        aiProcess_FindDegenerates |
        aiProcess_FindInvalidData |
        aiProcess_ImproveCacheLocality |
        // Left-handed coordinates, clockwise front faces and V running downward: what Direct3D (and this engine) expects.
        aiProcess_ConvertToLeftHanded;
    const aiScene* scene = importer.ReadFile(path, flags);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode || scene->mNumMeshes == 0) {
        error = scene ? "The file contains no meshes." : std::string("Assimp could not read the file: ") + importer.GetErrorString();
        return false;
    }

    out.name = file.stem().string();
    out.sourcePath = path;
    const std::string ext = LowerExtension(file);

    MaterialConverter converter{ scene, file.parent_path(), ext == ".gltf" || ext == ".glb", out, {} };
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) out.materials.push_back(converter.Convert(scene->mMaterials[i]));
    if (out.materials.empty()) out.materials.emplace_back();   // a mesh must always have a material to point at

    XMFLOAT3 boundsMin(FLT_MAX, FLT_MAX, FLT_MAX), boundsMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    // Walk the node tree, carrying the accumulated transform, and bake it into each mesh's vertices.
    struct Walker {
        const aiScene* scene;
        ImportedModel& out;
        XMFLOAT3& boundsMin;
        XMFLOAT3& boundsMax;

        void Visit(const aiNode* node, const aiMatrix4x4& parent)
        {
            const aiMatrix4x4 world = parent * node->mTransformation;
            for (unsigned int m = 0; m < node->mNumMeshes; ++m) AddMesh(scene->mMeshes[node->mMeshes[m]], world, node->mName.C_Str());
            for (unsigned int c = 0; c < node->mNumChildren; ++c) Visit(node->mChildren[c], world);
        }

        void AddMesh(const aiMesh* mesh, const aiMatrix4x4& world, const char* nodeName)
        {
            if (mesh->mNumVertices == 0 || mesh->mNumFaces == 0) return;
            ImportedPart part;
            part.name = mesh->mName.length > 0 ? mesh->mName.C_Str() : nodeName;
            part.material = (std::min)(static_cast<int>(mesh->mMaterialIndex), static_cast<int>(out.materials.size()) - 1);

            aiMatrix3x3 normalMatrix(world);          // normals use the inverse transpose, or non-uniform scale skews them
            normalMatrix.Inverse().Transpose();
            const bool mirrored = world.Determinant() < 0.0f;   // a mirroring node turns triangles inside out: flip them back

            part.vertices.resize(mesh->mNumVertices);
            for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
                Vertex& vertex = part.vertices[v];
                const aiVector3D position = world * mesh->mVertices[v];
                vertex.position = XMFLOAT3(position.x, position.y, position.z);
                if (mesh->HasNormals()) {
                    aiVector3D normal = normalMatrix * mesh->mNormals[v];
                    normal.Normalize();
                    vertex.normal = XMFLOAT3(normal.x, normal.y, normal.z);
                }
                if (mesh->HasTextureCoords(0)) vertex.texCoord = XMFLOAT2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y);
                boundsMin = XMFLOAT3((std::min)(boundsMin.x, position.x), (std::min)(boundsMin.y, position.y), (std::min)(boundsMin.z, position.z));
                boundsMax = XMFLOAT3((std::max)(boundsMax.x, position.x), (std::max)(boundsMax.y, position.y), (std::max)(boundsMax.z, position.z));
            }

            part.indices.reserve(static_cast<size_t>(mesh->mNumFaces) * 3);
            for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
                const aiFace& face = mesh->mFaces[f];
                if (face.mNumIndices != 3) continue;
                if (mirrored) part.indices.insert(part.indices.end(), { face.mIndices[0], face.mIndices[2], face.mIndices[1] });
                else part.indices.insert(part.indices.end(), { face.mIndices[0], face.mIndices[1], face.mIndices[2] });
            }
            if (part.indices.empty()) return;

            ComputeTangents(part.vertices, part.indices);   // our own, so they agree with the shader's convention (lesson 16)
            out.triangleCount += static_cast<std::uint32_t>(part.indices.size() / 3);
            out.parts.push_back(std::move(part));
        }
    } walker{ scene, out, boundsMin, boundsMax };
    walker.Visit(scene->mRootNode, aiMatrix4x4());

    if (out.parts.empty()) {
        error = "The file has no triangles to draw.";
        return false;
    }
    out.boundsMin = boundsMin;
    out.boundsMax = boundsMax;
    return true;
}
