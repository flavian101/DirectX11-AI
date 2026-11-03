#include "Scene.h"
#include "Logger.h"
#include "ShaderSlots.h"
#include "Skybox.h"
#include "Water.h"
#include <algorithm>
#include <cmath>
#include <vector>

Scene::Scene() : lights_(std::make_unique<LightSystem>()), shadow_(std::make_unique<ShadowMap>()) {}

// Objects are unique_ptrs now. The owner must still destroy the Scene BEFORE the D3D device.
Scene::~Scene() = default;

bool Scene::Initialize(ID3D11Device* device)
{
    if (!device || !lights_) return false;
    if (!lights_->Initialize(device)) return false;
    if (!frame_.Initialize(device)) return false;
    // Shadows are a bonus, not a requirement: if the map or its shader fails, keep rendering.
    if (!shadow_->Initialize(device, ShadowSettings{})) LOG_WARNING("Shadow map unavailable; shadows are disabled");
    return true;
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
    if (input.middleMouseButtonDown) {
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
    for (auto& obj : objects_) if (obj) obj->Update(dt);
}

// Smallest sphere containing both input spheres.
static void MergeSpheres(DirectX::XMFLOAT3& centerA, float& radiusA,
                         const DirectX::XMFLOAT3& centerB, float radiusB)
{
    using namespace DirectX;
    const XMVECTOR a = XMLoadFloat3(&centerA);
    const XMVECTOR b = XMLoadFloat3(&centerB);
    const float distance = XMVectorGetX(XMVector3Length(b - a));
    if (distance + radiusB <= radiusA) return;                       // B is inside A
    if (distance + radiusA <= radiusB) { centerA = centerB; radiusA = radiusB; return; }
    const float radius = 0.5f * (distance + radiusA + radiusB);
    XMStoreFloat3(&centerA, a + (b - a) * ((radius - radiusA) / distance));
    radiusA = radius;
}

bool Scene::ComputeShadowBounds(DirectX::XMFLOAT3& center, float& radius) const
{
    bool any = false;
    for (const auto& object : objects_) {
        DirectX::XMFLOAT3 objectCenter;
        float objectRadius = 0.0f;
        if (!object || !object->CastsShadow() || !object->GetBoundingSphere(objectCenter, objectRadius)) continue;
        if (!any) { center = objectCenter; radius = objectRadius; any = true; }
        else MergeSpheres(center, radius, objectCenter, objectRadius);
    }
    return any && radius > 0.0f;
}

void Scene::RenderShadows(ID3D11DeviceContext* ctx) const
{
    if (shadow_) shadow_->Invalidate();   // until Begin() runs, the main pass must not sample the map
    if (!ctx || !shadow_ || !shadow_->IsEnabled()) return;
    DirectX::XMFLOAT3 center;
    float radius = 0.0f;
    if (!ComputeShadowBounds(center, radius)) return;

    shadow_->Begin(ctx, lights_->Directional().direction, center, radius);

    // Reuse the per-frame buffer, but the "camera" is the sun.
    FrameData sunView;
    sunView.view = shadow_->GetView();
    sunView.proj = shadow_->GetProj();
    sunView.cameraPos = shadow_->GetLightPosition();
    sunView.time = time_;
    frame_.Apply(ctx, sunView);

    for (const auto& object : objects_) {
        if (object && object->CastsShadow()) object->RenderShadow(ctx, shadow_->GetShader());
    }
    shadow_->End(ctx);
}

void Scene::BeginFrame(ID3D11DeviceContext* ctx, UINT viewportWidth, UINT viewportHeight) const
{
    // Per-frame state: bound once here, then every object just draws.
    FrameData frame = FrameData::FromCamera(camera_, time_);
    frame.viewportWidth = static_cast<float>(viewportWidth);
    frame.viewportHeight = static_cast<float>(viewportHeight);
    shadow_->FillFrameData(frame);       // the sun's matrix + shadow parameters
    frame_.Apply(ctx, frame);
    shadow_->Bind(ctx);                  // shadow map -> t2, comparison sampler -> s1
    ID3D11ShaderResourceView* environment =
        (skybox_ && skybox_->IsValid()) ? skybox_->GetCubemapView() : nullptr;
    ctx->PSSetShaderResources(Slots::kSrvEnvironment, 1, &environment);
}

void Scene::RenderPhase(ID3D11DeviceContext* ctx, ScenePhase phase) const
{
    std::vector<const SceneObject*> sorted;
    sorted.reserve(objects_.size());
    for (const auto& object : objects_) {
        if (!object) continue;
        const bool transparent = object->GetRenderCategory() == SceneObject::RenderCategory::Transparent;
        if (transparent == (phase == ScenePhase::Transparent)) sorted.push_back(object.get());
    }

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

