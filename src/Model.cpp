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

void Model::Draw(ID3D11DeviceContext* ctx, const std::vector<unsigned char>* visible) const
{
    if (!ctx) return;
    for (size_t i = 0; i < meshes_.size(); ++i) {
        if (!meshes_[i]) continue;
        if (visible && i < visible->size() && !(*visible)[i]) continue;
        meshes_[i]->Draw(ctx);
    }
}
