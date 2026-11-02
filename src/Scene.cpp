#include "Scene.h"
#include <cmath>

Scene::Scene() : lights_(std::make_unique<LightSystem>()) {}

Scene::~Scene()
{
    for (SceneObject* obj : objects_) delete obj;
}

bool Scene::Initialize(ID3D11Device* device)
{
    if (!device || !lights_) return false;
    if (!lights_->Initialize(device)) return false;

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
    if (obj) objects_.push_back(obj);
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
    for (SceneObject* obj : objects_) if (obj) obj->Update(dt);
}

void Scene::Render(ID3D11DeviceContext* ctx) const
{
    for (const SceneObject* obj : objects_) if (obj) obj->Render(ctx, camera_);
}

