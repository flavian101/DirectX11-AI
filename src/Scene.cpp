#include "Scene.h"
#include "ShaderSlots.h"
#include "Skybox.h"
#include "Water.h"
#include <algorithm>
#include <cmath>
#include <vector>

Scene::Scene() : lights_(std::make_unique<LightSystem>()) {}

Scene::~Scene()
{
    for (SceneObject* obj : objects_) delete obj;
}

bool Scene::Initialize(ID3D11Device* device)
{
    if (!device || !lights_) return false;
    if (!lights_->Initialize(device)) return false;
    if (!frame_.Initialize(device)) return false;

    camera_.SetPosition({ 0.0f, 62.0f, -115.0f });
    camera_.SetTarget({ 0.0f, 12.0f, 0.0f });
    terrain_ = new Terrain(device);
    AddObject(terrain_);
    return true;
}

bool Scene::RegenerateTerrain(const TerrainSettings& settings)
{
    return terrain_ && terrain_->Regenerate(settings);
}

void Scene::AddObject(SceneObject* obj)
{
    if (!obj) return;
    objects_.push_back(obj);
    if (auto* water = dynamic_cast<Water*>(obj)) water_ = water;
    if (auto* skybox = dynamic_cast<Skybox*>(obj)) skybox_ = skybox;
}

void Scene::UpdateCamera(const InputState& input, float dt)
{
    // Handle camera movement with WASD
    bool forward = input.keys['W'];
    bool backward = input.keys['S'];
    bool left = input.keys['A'];
    bool right = input.keys['D'];
    camera_.Move(forward, backward, left, right, dt);

    // Handle first-person camera rotation with Ctrl + Left Mouse Button
    if (input.ctrlDown && input.leftMouseDown) {
        const float smoothing = 1.0f - std::exp(-20.0f * dt);
        smoothedMouseDeltaX_ += (static_cast<float>(input.mouseDeltaX) - smoothedMouseDeltaX_) * smoothing;
        smoothedMouseDeltaY_ += (static_cast<float>(input.mouseDeltaY) - smoothedMouseDeltaY_) * smoothing;
        camera_.Rotate(smoothedMouseDeltaX_, smoothedMouseDeltaY_);
    } else {
        smoothedMouseDeltaX_ = 0.0f;
        smoothedMouseDeltaY_ = 0.0f;
    }
}

void Scene::ProcessInput(const InputState& input, float dt)
{
    UpdateCamera(input, dt);
}

void Scene::Update(float dt)
{
    time_ += dt;
    for (SceneObject* obj : objects_) if (obj) obj->Update(dt);
}

void Scene::Render(ID3D11DeviceContext* ctx) const
{
    // Per-frame state: bound once here, then every object just draws.
    frame_.Apply(ctx, camera_, time_);
    ID3D11ShaderResourceView* environment =
        (skybox_ && skybox_->IsValid()) ? skybox_->GetCubemapView() : nullptr;
    ctx->PSSetShaderResources(Slots::kSrvEnvironment, 1, &environment);

    std::vector<const SceneObject*> sorted;
    sorted.reserve(objects_.size());
    for (const SceneObject* object : objects_) if (object) sorted.push_back(object);

    const DirectX::XMFLOAT3 cameraPosition = camera_.GetPosition();
    auto distanceSquared = [&cameraPosition](const SceneObject* object) {
        const DirectX::XMFLOAT3 position = object->GetSortPosition();
        const float dx = position.x - cameraPosition.x;
        const float dy = position.y - cameraPosition.y;
        const float dz = position.z - cameraPosition.z;
        return dx * dx + dy * dy + dz * dz;
    };
    auto categoryRank = [](SceneObject::RenderCategory category) {
        switch (category) {
        case SceneObject::RenderCategory::Background: return 0;
        case SceneObject::RenderCategory::Opaque: return 1;
        case SceneObject::RenderCategory::Transparent: return 2;
        }
        return 3;
    };

    std::stable_sort(sorted.begin(), sorted.end(), [&](const SceneObject* left, const SceneObject* right) {
        const int leftRank = categoryRank(left->GetRenderCategory());
        const int rightRank = categoryRank(right->GetRenderCategory());
        if (leftRank != rightRank) return leftRank < rightRank;
        if (leftRank == 0) return false;
        const float leftDistance = distanceSquared(left);
        const float rightDistance = distanceSquared(right);
        return leftRank == 2 ? leftDistance > rightDistance : leftDistance < rightDistance;
    });

    // Background is drawn first, opaque geometry front-to-back, and
    // transparent geometry back-to-front. This matches the depth and blend
    // states used by the built-in materials.
    SceneObject::RenderCategory previousCategory = SceneObject::RenderCategory::Background;
    bool first = true;
    for (const SceneObject* object : sorted) {
        const SceneObject::RenderCategory category = object->GetRenderCategory();
        if (first || category != previousCategory) {
            const float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            ctx->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
            ctx->OMSetDepthStencilState(nullptr, 0);
            ctx->RSSetState(nullptr);
            previousCategory = category;
            first = false;
        }
        object->Render(ctx, camera_);
    }
}

