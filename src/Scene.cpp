#include "Scene.h"

Scene::Scene() : lights_(std::make_unique<LightSystem>()) {}

Scene::~Scene()
{
    for (SceneObject* obj : objects_) delete obj;
}

bool Scene::Initialize(ID3D11Device* device)
{
    if (!device || !lights_) return false;
    return lights_->Initialize(device);
}

void Scene::AddObject(SceneObject* obj)
{
    if (obj) objects_.push_back(obj);
}

void Scene::Update(float dt)
{
    for (SceneObject* obj : objects_) if (obj) obj->Update(dt);
}

void Scene::Render(ID3D11DeviceContext* ctx) const
{
    for (const SceneObject* obj : objects_) if (obj) obj->Render(ctx, camera_);
}

