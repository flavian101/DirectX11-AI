#include "Scene.h"
#include "Color.h"
#include "ModelObject.h"
#include "Cube.h"
#include "Logger.h"
#include "ScatterObject.h"
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

std::unique_ptr<SceneObject> Scene::Detach(SceneObjectId id, size_t* index)
{
    const auto it = std::find_if(objects_.begin(), objects_.end(),
        [id](const std::unique_ptr<SceneObject>& object) { return object && object->GetId() == id; });
    if (it == objects_.end()) return nullptr;
    if (index) *index = static_cast<size_t>(it - objects_.begin());
    std::unique_ptr<SceneObject> taken = std::move(*it);
    objects_.erase(it);
    return taken;
}

SceneObject* Scene::Attach(std::unique_ptr<SceneObject> object, size_t index)
{
    if (!object) return nullptr;
    SceneObject* result = object.get();   // keeps the id and name it had: nothing else in the editor refers to it by pointer
    objects_.insert(objects_.begin() + static_cast<std::ptrdiff_t>((std::min)(index, objects_.size())), std::move(object));
    return result;
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
    // The scene's wind goes to every water body that follows it (each rebuilds its waves only if the wind changed).
    weather_.Clamp();
    for (auto& obj : objects_) if (auto* water = dynamic_cast<Water*>(obj.get())) water->SetSceneWind(weather_.wind);
    // Floating models read the water under them (the highest body that covers their spot) and follow its waves.
    for (auto& obj : objects_) {
        auto* model = dynamic_cast<ModelObject*>(obj.get());
        if (!model || !model->GetFloatingSettings().enabled) continue;
        const DirectX::XMFLOAT3 anchor = model->GetFloatAnchor();
        model->StepFloating(FindWaterAt(anchor.x, 1.0e5f, anchor.z), time_, dt);
    }
    // Every water body measures the terrain under it (only when something changed: this is a cheap comparison otherwise).
    const Terrain* terrain = GetTerrain();
    for (auto& obj : objects_) if (auto* water = dynamic_cast<Water*>(obj.get())) water->RefreshShoreDepth(terrain);
    // Scatter follows the ground the same way: it regenerates the cells whose ground changed (within a time budget).
    for (auto& obj : objects_) if (auto* scatter = dynamic_cast<ScatterObject*>(obj.get())) scatter->Refresh(terrain, camera_.GetPosition());
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
    if (lights_->Directional().intensity <= 0.0f) return;   // no sun (or a moonless dark scene): nothing to cast a shadow from
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
    sunView.projScale = camera_.GetProjectionMatrix()._22;   // detail follows the real camera, so does its lens

    for (const auto& object : objects_) {
        if (object && object->CastsShadow()) object->RenderShadow(ctx, shadow_->GetShader(), sunView);
    }
    shadow_->End(ctx);
}

Skybox* Scene::GetSkybox()
{
    Skybox* first = nullptr;
    for (auto& object : objects_) {
        auto* sky = dynamic_cast<Skybox*>(object.get());
        if (!sky) continue;
        if (sky->IsActive()) return sky;
        if (!first) first = sky;
    }
    return first;
}

const Skybox* Scene::GetSkybox() const
{
    return const_cast<Scene*>(this)->GetSkybox();
}

std::vector<Skybox*> Scene::GetSkyboxes()
{
    std::vector<Skybox*> skies;
    for (auto& object : objects_) {
        if (auto* sky = dynamic_cast<Skybox*>(object.get())) skies.push_back(sky);
    }
    return skies;
}

bool Scene::SetActiveSkybox(Skybox* sky)
{
    if (!sky) return false;
    bool found = false;
    for (Skybox* other : GetSkyboxes()) found = found || (other == sky);
    if (!found) return false;
    for (Skybox* other : GetSkyboxes()) other->SetActive(other == sky);
    return true;
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

// How strong a directional light is, for choosing which one is the sun: strength times how bright its colour is.
static float DirectionalStrength(const LightObject& light)
{
    const DirectX::XMFLOAT3 c = SrgbToLinear(light.GetColor());
    return light.GetIntensity() * (0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z);
}

const LightObject* Scene::PrimaryDirectional() const
{
    const LightObject* best = nullptr;
    for (const auto& object : objects_) {
        const auto* light = dynamic_cast<const LightObject*>(object.get());
        if (!light || !light->IsDirectional()) continue;
        if (!best || DirectionalStrength(*light) > DirectionalStrength(*best)) best = light;
    }
    return best;
}

int Scene::DirectionalRank(const LightObject& light) const
{
    if (!light.IsDirectional()) return -1;
    int stronger = 0;
    for (const auto& object : objects_) {
        const auto* other = dynamic_cast<const LightObject*>(object.get());
        if (!other || other == &light || !other->IsDirectional()) continue;
        const float a = DirectionalStrength(*other), b = DirectionalStrength(light);
        // Equal strength: the one earlier in the scene wins, the same way ApplyLights chooses.
        if (a > b || (a == b && other->GetId() < light.GetId())) ++stronger;
    }
    return stronger;
}

std::vector<LightObject*> Scene::GetLights()
{
    std::vector<LightObject*> lights;
    for (auto& object : objects_) {
        if (auto* light = dynamic_cast<LightObject*>(object.get())) lights.push_back(light);
    }
    return lights;
}

void Scene::ApplyLights(ID3D11DeviceContext* ctx) const
{
    if (!lights_) return;
    // The two strongest directional lights: the first is the sun (shadows), the second lights without shadows.
    const LightObject* first = nullptr;
    const LightObject* second = nullptr;
    std::vector<const LightObject*> points;
    for (const auto& object : objects_) {
        const auto* light = dynamic_cast<const LightObject*>(object.get());
        if (!light) continue;
        if (!light->IsDirectional()) { points.push_back(light); continue; }
        if (!first || DirectionalStrength(*light) > DirectionalStrength(*first)) { second = first; first = light; }
        else if (!second || DirectionalStrength(*light) > DirectionalStrength(*second)) second = light;
    }
    auto copyDirectional = [](DirectionalLight& target, const LightObject* source) {
        if (!source) { target.intensity = 0.0f; return; }   // no light of this kind: it contributes nothing
        target.direction = source->GetDirection();
        target.color = source->GetColor();
        target.intensity = source->GetIntensity();
    };
    copyDirectional(lights_->Directional(), first);
    copyDirectional(lights_->Secondary(), second);

    // More lamps than slots: the ones nearest the camera win, so the lamps you can see are the lit ones.
    const DirectX::XMFLOAT3 camera = camera_.GetPosition();
    auto distanceSquared = [&](const LightObject* light) {
        const DirectX::XMFLOAT3& p = light->GetPosition();
        const float dx = p.x - camera.x, dy = p.y - camera.y, dz = p.z - camera.z;
        return dx * dx + dy * dy + dz * dz;
    };
    std::stable_sort(points.begin(), points.end(), [&](const LightObject* a, const LightObject* b) { return distanceSquared(a) < distanceSquared(b); });
    lights_->ClearPointLights();
    for (const LightObject* light : points) {
        if (!lights_->AddPointLight(PointLight{ light->GetPosition(), light->GetColor(), light->GetIntensity(), light->GetRange() })) break;
    }
    lights_->Apply(ctx, camera);
}

void Scene::FillFogFrameData(FrameData& frame) const
{
    const DirectX::XMFLOAT3 camera = camera_.GetPosition();

    // Under water the water body is the medium: thick, coloured by the body's own colours, getting darker with depth.
    const Water* water = FindWaterAt(camera.x, camera.y, camera.z);
    if (water) {
        const float surface = water->SampleHeight(camera.x, camera.z, time_);
        if (camera.y < surface) {
            const WaterSettings& settings = water->GetSettings();
            const float depth = surface - camera.y;
            const float absorption = (std::max)(settings.absorption, 0.02f);
            const DirectX::XMFLOAT3 shallow = SrgbToLinear(settings.shallowColor);
            const DirectX::XMFLOAT3 deep = SrgbToLinear(settings.color);
            const float blend = (std::min)(depth * absorption * 0.15f, 1.0f);   // shallow colour near the surface, deep colour far down
            const LightObject* sun = PrimaryDirectional();
            const float sunLevel = sun ? (std::min)(0.3f + 0.5f * DirectionalStrength(*sun), 1.6f) : 0.3f;   // night under water is dark
            const float dim = sunLevel * std::exp(-depth * absorption * 0.1f);
            frame.fogParams = DirectX::XMFLOAT4(absorption * 0.6f, 0.0f, 0.0f, 0.0f);
            frame.fogColor = DirectX::XMFLOAT4((shallow.x + (deep.x - shallow.x) * blend) * dim, (shallow.y + (deep.y - shallow.y) * blend) * dim,
                                               (shallow.z + (deep.z - shallow.z) * blend) * dim, 1.0f);
            frame.fogExtra = DirectX::XMFLOAT4(0.0f, 0.0f, 1.0f, 0.0f);
            return;
        }
    }

    const FogSettings& fog = weather_.fog;
    if (!fog.enabled) return;   // FrameData's defaults are "no fog"
    const DirectX::XMFLOAT3 color = SrgbToLinear(DirectX::XMFLOAT3(fog.color[0], fog.color[1], fog.color[2]));
    frame.fogParams = DirectX::XMFLOAT4(fog.Density(), fog.startDistance, fog.heightFalloff, fog.baseHeight);
    frame.fogColor = DirectX::XMFLOAT4(color.x * fog.brightness, color.y * fog.brightness, color.z * fog.brightness, fog.maxOpacity);
    frame.fogExtra = DirectX::XMFLOAT4(fog.sunGlow, fog.skyBlend, 0.0f, 0.0f);
}

void Scene::FillEnvironmentFrameData(FrameData& frame) const
{
    FillFogFrameData(frame);
    const Skybox* skybox = GetSkybox();
    // The sky's turn and tint reach every shader that looks the sky up (the water's mirror of it works even before the
    // baked lighting exists). FrameData's defaults are "no turn, no tint".
    if (skybox) {
        const float yaw = DirectX::XMConvertToRadians(skybox->GetYawDegrees());
        frame.skyParams = DirectX::XMFLOAT4(std::cos(yaw), std::sin(yaw), 0.0f, 0.0f);
        const DirectX::XMFLOAT3 tint = SrgbToLinear(skybox->GetTint());
        frame.skyTint = DirectX::XMFLOAT4(tint.x, tint.y, tint.z, skybox->GetIntensity());
    }
    if (!environmentLighting_.IsBaked()) return;   // iblParams stays zeros: shaders use the flat ambient colour
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
                        bool reflectionAvailable, bool refractionAvailable) const
{
    FrameData frame = FrameData::FromCamera(camera_, time_);
    frame.viewportWidth = static_cast<float>(viewportWidth);
    frame.viewportHeight = static_cast<float>(viewportHeight);
    frame.reflectionAvailable = reflectionAvailable;
    frame.refractionAvailable = refractionAvailable;
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
    view.projScale = cameraProj._22;
    return view;
}

void Scene::PlanWater(WaterFramePlan& plan) const
{
    plan = WaterFramePlan{};
    const DirectX::XMFLOAT3 eye = camera_.GetPosition();
    const Frustum frustum = GetMainView().frustum;

    struct Wanting { size_t entry; float height; float resolutionScale; float distance; };
    std::vector<Wanting> wanting;
    for (const auto& object : objects_) {
        const Water* water = dynamic_cast<const Water*>(object.get());
        // Off screen, or only the parts hidden under dry ground are in view: its mirror would never be seen. (A mirror image
        // is a whole extra draw of the scene, so this is the question that decides whether the frame pays for it.)
        if (!water || !water->IsWetVisible(frustum)) continue;
        const WaterSettings& settings = water->GetSettings();
        plan.entries.push_back({ water, -1 });
        // From below the surface the water is culled: nothing to mirror or to bend.
        if (eye.y <= settings.height + 0.01f) continue;
        if (settings.refraction > 0.001f) plan.refraction = true;
        if (settings.reflectionMode == ReflectionMode::Sky) continue;
        float resolutionScale = 1.0f;
        switch (settings.reflectionMode) {
        case ReflectionMode::PlanarQuarter: resolutionScale = 0.25f; break;
        case ReflectionMode::PlanarHalf:    resolutionScale = 0.5f;  break;
        default:                            resolutionScale = 1.0f;  break;
        }
        wanting.push_back({ plan.entries.size() - 1, settings.height, resolutionScale, water->DistanceTo(eye) });
    }

    // Each mirror image is a whole extra scene draw, so there is a budget. The nearest bodies get one first;
    // bodies at the same level share it (the image is the same), the rest keep the cheap sky reflection.
    std::sort(wanting.begin(), wanting.end(), [](const Wanting& a, const Wanting& b) { return a.distance < b.distance; });
    for (const Wanting& want : wanting) {
        int slot = -1;
        for (int i = 0; i < plan.reflectionCount; ++i) {
            if (std::fabs(plan.reflections[i].planeHeight - want.height) < 0.05f) { slot = i; break; }
        }
        if (slot < 0 && plan.reflectionCount < WaterFramePlan::kMaxReflections) {
            slot = plan.reflectionCount++;
            plan.reflections[slot].planeHeight = want.height;
            plan.reflections[slot].resolutionScale = want.resolutionScale;
        }
        if (slot < 0) continue;
        plan.reflections[slot].resolutionScale = (std::max)(plan.reflections[slot].resolutionScale, want.resolutionScale);
        plan.entries[want.entry].reflection = slot;
    }
}

Water* Scene::CreateWater(WaterBodyType type)
{
    if (!device_) return nullptr;
    Water* water = Add<Water>(device_);
    WaterSettings settings = water->GetSettings();
    settings.ApplyBodyType(type);   // look, waves, ripples and foam that suit this kind of water
    switch (type) {   // and a size that suits it: none of these is a second sea (except the ocean)
    case WaterBodyType::Ocean: settings.size = 400.0f; settings.sizeZ = 400.0f; settings.shape = WaterShape::Rectangle; break;
    case WaterBodyType::Lake:  settings.size = 80.0f;  settings.sizeZ = 80.0f;  settings.shape = WaterShape::Ellipse;   break;
    case WaterBodyType::River: settings.size = 14.0f;  settings.sizeZ = 120.0f; settings.shape = WaterShape::Rectangle; break;
    case WaterBodyType::Pond:  settings.size = 20.0f;  settings.sizeZ = 20.0f;  settings.shape = WaterShape::Ellipse;   break;
    case WaterBodyType::Custom: settings.size = 120.0f; settings.sizeZ = 120.0f; settings.shape = WaterShape::Rectangle; break;
    }
    water->SetSceneWind(weather_.wind);   // a new body starts with the scene's wind, not for one frame with a default
    water->SetSettings(settings);
    return water;
}

const Water* Scene::FindWaterAt(float x, float y, float z) const
{
    const Water* below = nullptr;   // highest surface at or under y
    float belowSurface = 0.0f;
    const Water* above = nullptr;   // lowest surface over y (the point is under water)
    float aboveSurface = 0.0f;
    for (const auto& object : objects_) {
        const Water* water = dynamic_cast<const Water*>(object.get());
        if (!water || !water->IsOverWater(x, z)) continue;
        const float surface = water->SampleHeight(x, z, time_);
        if (surface <= y) {
            if (!below || surface > belowSurface) { below = water; belowSurface = surface; }
        } else if (!above || surface < aboveSurface) {
            above = water;
            aboveSurface = surface;
        }
    }
    return below ? below : above;
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
    reflectionView.clipBelow = true;        // the clip plane drops everything under the water: do not even draw wholly-submerged things
    reflectionView.clipBelowY = planeHeight;
    XMFLOAT4X4 mirroredViewProj;
    XMStoreFloat4x4(&mirroredViewProj, XMLoadFloat4x4(&frame.view) * XMLoadFloat4x4(&frame.proj));
    reflectionView.frustum = Frustum::FromViewProj(mirroredViewProj);
    reflectionView.lodOrigin = camera_.GetPosition();   // detail follows the REAL camera, not the mirror
    reflectionView.projScale = camera_.GetProjectionMatrix()._22;

    // Sky + solid objects only. The water itself (transparent) must not appear in its own mirror.
    RenderPhase(ctx, ScenePhase::Opaque, reflectionView);
}

void Scene::RenderHullMasks(ID3D11DeviceContext* ctx, const RenderView& view) const
{
    for (const auto& obj : objects_) if (const auto* model = dynamic_cast<const ModelObject*>(obj.get())) model->RenderHullMask(ctx, view);
}

void Scene::RenderPhase(ID3D11DeviceContext* ctx, ScenePhase phase, const RenderView& view) const
{
    std::vector<const SceneObject*> sorted;
    sorted.reserve(objects_.size());
    const Skybox* activeSky = GetSkybox();
    for (const auto& object : objects_) {
        if (!object) continue;
        // A scene can hold several skies, but only the active one is drawn.
        if (dynamic_cast<const Skybox*>(object.get()) && object.get() != activeSky) continue;
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

