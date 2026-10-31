#include "Model.h"
#include <DirectXMath.h>

using namespace DirectX;

Model::Model(const std::vector<Mesh*>& meshes) : meshes_(meshes)
{
    XMStoreFloat4x4(&world_, XMMatrixIdentity());
}

Model::~Model()
{
    for (Mesh* mesh : meshes_) delete mesh;
}

void Model::Draw(ID3D11DeviceContext* ctx) const
{
    if (!ctx) return;
    for (Mesh* mesh : meshes_) if (mesh) mesh->Draw(ctx);
}
