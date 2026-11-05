#pragma once
#include "ModelImporter.h"
#include "Renderable.h"
#include "SceneObject.h"
#include "SurfaceMaterial.h"
#include <DirectXMath.h>
#include <memory>
#include <string>
#include <vector>

// A model loaded from a file (glTF, FBX, OBJ...), shown in the scene. Takes the importer's CPU data,
// uploads each part to the GPU and draws it with the material the file asked for.
class ModelObject final : public SceneObject {
public:
    ModelObject(ID3D11Device* device, ImportedModel&& imported);
    ~ModelObject() override;

    const char* GetEditorName() const override { return displayName_.c_str(); }
    const char* GetTypeName() const override { return "Model"; }
    DirectX::XMFLOAT3 GetSortPosition() const override { return position_; }
    bool GetEditorTransform(DirectX::XMFLOAT3& position, DirectX::XMFLOAT3& rotation, DirectX::XMFLOAT3& scale) const override;
    bool SetEditorTransform(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation,
                            const DirectX::XMFLOAT3& scale) override;
    bool CastsShadow() const override { return true; }
    bool GetOrientedBounds(DirectX::XMFLOAT4X4& world, DirectX::XMFLOAT3& boundsMin, DirectX::XMFLOAT3& boundsMax) const override;
    bool IsUserContent() const override { return true; }
    bool CanClone() const noexcept override { return !sourcePath_.empty(); }
    // Reads the file again (the CPU data was not kept) and copies this object's transform and material settings.
    std::unique_ptr<SceneObject> Clone(ID3D11Device* device) const override;
    bool GetBoundingSphere(DirectX::XMFLOAT3& center, float& radius) const override;
    void RenderShadow(ID3D11DeviceContext* ctx, const Shader& shader, const RenderView& view) const override;
    void Update(float) override {}
    void Render(ID3D11DeviceContext* ctx, const RenderView& view) const override;

    bool IsValid() const { return !parts_.empty(); }
    // Scales the model uniformly so its longest side is `size` world units. (Files come in any unit:
    // a castle may be 0.01 or 500 units wide.)
    void FitToSize(float size);

    // For the Inspector.
    const std::string& GetSourcePath() const { return sourcePath_; }
    size_t GetPartCount() const { return parts_.size(); }
    size_t GetMaterialCount() const { return materials_.size(); }
    SurfaceMaterial& GetMaterial(size_t index) { return *materials_[index]; }
    const std::string& GetMaterialName(size_t index) const { return materialNames_[index]; }
    std::uint32_t GetTriangleCount() const { return triangleCount_; }

private:
    struct Part {
        std::unique_ptr<Renderable> renderable;
        int material = 0;
        bool doubleSided = false;
    };

    void UpdateWorldTransform();
    ID3D11RasterizerState* GetCullNone() const;

    ID3D11Device* device_ = nullptr;
    std::string displayName_;
    std::string sourcePath_;
    std::vector<Part> parts_;                                // sorted by material: fewer texture switches
    std::vector<std::unique_ptr<SurfaceMaterial>> materials_;
    std::vector<std::string> materialNames_;
    std::uint32_t triangleCount_ = 0;
    mutable ID3D11RasterizerState* cullNone_ = nullptr;      // for double-sided materials; created on first use

    // Model space: the box around everything, and the pivot (bottom centre) that scale/rotation act around.
    DirectX::XMFLOAT3 boundsMin_ = {}, boundsMax_ = {};
    DirectX::XMFLOAT3 pivot_ = {};
    DirectX::XMFLOAT3 position_ = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 rotation_ = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 scale_ = { 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT4X4 world_;
};
