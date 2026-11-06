#include "Scene.h"
#include "Cube.h"
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
    device_ = device;
    if (!lights_->Initialize(device)) return false;
    if (!frame_.Initialize(device)) return false;
    // Shadows are a bonus, not a requirement: if the map or its shader fails, keep rendering.
    if (!shadow_->Initialize(device, ShadowSettings{})) LOG_WARNING("Shadow map unavailable; shadows are disabled");
    return true;
}

SceneObject* Scene::Adopt(std::unique_ptr<SceneObject> object)
{
    if (!object) return nullptr;

    object->id_ = nextObjectId_++;
    object->name_ = std::string(object->GetTypeName()) + " " + std::to_string(object->id_);

    SceneObject* result = object.get();
    objects_.push_back(std::move(object));
    return result;
}

SceneObject* Scene::Find(SceneObjectId id)
{
    if (id == 0) return nullptr;
    for (auto& object : objects_) {
        if (object && object->GetId() == id) return object.get();
    }
    return nullptr;
}

const SceneObject* Scene::Find(SceneObjectId id) const
{
    if (id == 0) return nullptr;
    for (const auto& object : objects_) {
        if (object && object->GetId() == id) return object.get();
    }
    return nullptr;
}

bool Scene::Remove(SceneObjectId id)
{
    const auto it = std::find_if(objects_.begin(), objects_.end(),
        [id](const std::unique_ptr<SceneObject>& object) {
            return object && object->GetId() == id;
        });
    if (it == objects_.end()) return false;
    objects_.erase(it);
    return true;
}

SceneObject* Scene::Duplicate(SceneObjectId id)
{
    SceneObject* original = Find(id);
    if (!original || !original->CanClone() || !device_) return nullptr;

    const std::string duplicateName = original->GetName() + " Copy";
    std::unique_ptr<SceneObject> copy = original->Clone(device_);
    if (!copy) return nullptr;

    // Give spatial objects a small offset so the duplicate is immediately visible.
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 rotation;
    DirectX::XMFLOAT3 scale;
    if (original->GetEditorTransform(position, rotation, scale)) {
        position.x += 2.0f;
        copy->SetEditorTransform(position, rotation, scale);
    }

    SceneObject* result = Adopt(std::move(copy));
    if (result) result->SetName(duplicateName);
    return result;
}

void Scene::ClearObjects()
{
    objects_.clear();
    environmentLighting_.Release();   // it was baked from the old skybox
}

SceneObject* Scene::CreateCube()
{
    return device_ ? Add<Cube>(device_) : nullptr;
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
    using namespace DirectX;
    if (shadow_) shadow_->Invalidate();   // until Begin() runs, the main pass must not sample the map
    if (!ctx || !shadow_ || !shadow_->IsEnabled()) return;
    DirectX::XMFLOAT3 center;
    float radius = 0.0f;
    if (!ComputeShadowBounds(center, radius)) return;

    shadow_->Begin(ctx, lights_->Directional().direction, center, radius);

    // Reuse the per-frame buffer, but the "camera" is the sun.
    FrameData sunFrame;
    sunFrame.view = shadow_->GetView();
    sunFrame.proj = shadow_->GetProj();
    sunFrame.cameraPos = shadow_->GetLightPosition();
    sunFrame.time = time_;
    frame_.Apply(ctx, sunFrame);

    // The sun's view volume is the orthographic box fitted around everything (lesson 2), so
    // culling against it rarely removes anything - but it is correct, and it would start to matter
    // the moment the box is fitted more tightly (cascades).
    RenderView sunView;
    sunView.kind = ViewKind::Shadow;
    XMFLOAT4X4 sunViewProj;
    XMStoreFloat4x4(&sunViewProj, XMLoadFloat4x4(&sunFrame.view) * XMLoadFloat4x4(&sunFrame.proj));
    sunView.frustum = Frustum::FromViewProj(sunViewProj);
    sunView.lodOrigin = camera_.GetPosition();

    for (const auto& object : objects_) {
        if (object && object->CastsShadow()) object->RenderShadow(ctx, shadow_->GetShader(), sunView);
    }
    shadow_->End(ctx);
}

void Scene::PrepareEnvironment(ID3D11DeviceContext* ctx) const
{
    const Skybox* skybox = GetSkybox();
    if (!ctx || !skybox || !skybox->IsValid()) return;
    ID3D11ShaderResourceView* sky = skybox->GetCubemapView();
    if (environmentLighting_.IsBaked() && environmentLighting_.GetSource() == sky) return;   // up to date

    ID3D11Device* device = nullptr;
    ctx->GetDevice(&device);
    if (device) {
        environmentLighting_.Bake(device, ctx, sky);
        device->Release();   // GetDevice added a reference
    }
}

void Scene::FillEnvironmentFrameData(FrameData& frame) const
{
    if (!environmentLighting_.IsBaked()) return;   // stays zeros: shaders use the flat ambient colour
    const Skybox* skybox = GetSkybox();
    const float skyIntensity = skybox ? skybox->GetIntensity() : 1.0f;   // dimming the sky dims its light, too
    if (environment_.enabled) {
        frame.iblParams = DirectX::XMFLOAT4(1.0f, environment_.diffuseIntensity * skyIntensity,
                                            environment_.specularIntensity * skyIntensity, EnvironmentLighting::MaxPrefilterMip());
    }
    frame.iblDebug = DirectX::XMFLOAT4(static_cast<float>(environment_.skyView), environment_.debugMip, 0.0f, 0.0f);
}

void Scene::BeginFrame(ID3D11DeviceContext* ctx) const
{
    // Resources that stay the same for every pass this frame, however many views we draw.
    shadow_->Bind(ctx);                  // shadow map -> t2, comparison sampler -> s1
    const Skybox* skybox = GetSkybox();
    ID3D11ShaderResourceView* environment =
        (skybox && skybox->IsValid()) ? skybox->GetCubemapView() : nullptr;
    ctx->PSSetShaderResources(Slots::kSrvEnvironment, 1, &environment);
    environmentLighting_.Bind(ctx);      // irradiance -> t11, prefiltered sky -> t12, BRDF table -> t13
}

void Scene::SetMainView(ID3D11DeviceContext* ctx, UINT viewportWidth, UINT viewportHeight,
                        bool reflectionAvailable) const
{
    FrameData frame = FrameData::FromCamera(camera_, time_);
    frame.viewportWidth = static_cast<float>(viewportWidth);
    frame.viewportHeight = static_cast<float>(viewportHeight);
    frame.reflectionAvailable = reflectionAvailable;
    shadow_->FillFrameData(frame);       // the sun's matrix + shadow parameters
    FillEnvironmentFrameData(frame);     // image-based lighting settings
    frame_.Apply(ctx, frame);
}

RenderView Scene::GetMainView() const
{
    using namespace DirectX;
    RenderView view;
    view.kind = ViewKind::Main;
    const XMFLOAT4X4 cameraView = camera_.GetViewMatrix();
    const XMFLOAT4X4 cameraProj = camera_.GetProjectionMatrix();
    XMFLOAT4X4 viewProj;
    XMStoreFloat4x4(&viewProj, XMLoadFloat4x4(&cameraView) * XMLoadFloat4x4(&cameraProj));
    view.frustum = Frustum::FromViewProj(viewProj);
    view.lodOrigin = camera_.GetPosition();
    return view;
}

bool Scene::GetPlanarReflection(float& planeHeight, float& resolutionScale) const
{
    const Water* water = GetWater();
    if (!water) return false;
    const WaterSettings& settings = water->GetSettings();
    if (settings.reflectionMode == ReflectionMode::Sky) return false;
    planeHeight = settings.height;
    // Under the surface the water is culled and there is nothing to reflect.
    if (camera_.GetPosition().y <= planeHeight + 0.01f) return false;
    resolutionScale = settings.reflectionMode == ReflectionMode::PlanarHalf ? 0.5f : 1.0f;
    return true;
}

void Scene::RenderReflection(ID3D11DeviceContext* ctx, float planeHeight,
                             UINT targetWidth, UINT targetHeight) const
{
    using namespace DirectX;

    // A MIRROR shows the world as seen from the camera's mirror image. So: move the camera to
    // the other side of the water plane (y -> 2h - y) and flip its look direction and up vector.
    // The scene is NOT mirrored, so triangle winding and culling keep working as normal.
    const XMFLOAT3 position = camera_.GetPosition();
    const XMFLOAT3 target = camera_.GetTarget();
    const XMFLOAT3 up = camera_.GetUp();
    const XMFLOAT3 mirroredPosition(position.x, 2.0f * planeHeight - position.y, position.z);
    const XMFLOAT3 mirroredTarget(target.x, 2.0f * planeHeight - target.y, target.z);
    const XMFLOAT3 mirroredUp(up.x, -up.y, up.z);

    FrameData frame;
    XMStoreFloat4x4(&frame.view, XMMatrixLookAtLH(XMLoadFloat3(&mirroredPosition),
                                                  XMLoadFloat3(&mirroredTarget),
                                                  XMLoadFloat3(&mirroredUp)));
    frame.proj = camera_.GetProjectionMatrix();
    frame.cameraPos = mirroredPosition;
    frame.time = time_;
    frame.viewportWidth = static_cast<float>(targetWidth);
    frame.viewportHeight = static_cast<float>(targetHeight);
    // Keep only what is ABOVE the water: dot((0,1,0), p) - h >= 0.
    frame.clipPlane = XMFLOAT4(0.0f, 1.0f, 0.0f, -planeHeight);
    shadow_->FillFrameData(frame);
    FillEnvironmentFrameData(frame);   // the mirrored world is lit by the same sky
    frame_.Apply(ctx, frame);

    RenderView reflectionView;
    reflectionView.kind = ViewKind::Reflection;
    XMFLOAT4X4 mirroredViewProj;
    XMStoreFloat4x4(&mirroredViewProj, XMLoadFloat4x4(&frame.view) * XMLoadFloat4x4(&frame.proj));
    reflectionView.frustum = Frustum::FromViewProj(mirroredViewProj);
    reflectionView.lodOrigin = camera_.GetPosition();   // detail follows the REAL camera, not the mirror

    // Sky + solid objects only. The water itself (transparent) must not appear in its own mirror.
    RenderPhase(ctx, ScenePhase::Opaque, reflectionView);
}

void Scene::RenderPhase(ID3D11DeviceContext* ctx, ScenePhase phase, const RenderView& view) const
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
        object->Render(ctx, view);
    }
}

