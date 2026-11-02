#pragma once
#include "Camera.h"
#include "SceneObject.h"
#include "Light.h"
#include <d3d11.h>
#include <vector>
#include <memory>

class Scene {
public:
    Scene();
    ~Scene();

    bool Initialize(ID3D11Device* device);
    const Camera& GetCamera() const { return camera_; }
    LightSystem& GetLightSystem() { return *lights_; }
    const LightSystem& GetLightSystem() const { return *lights_; }
    void AddObject(SceneObject* obj);      // Scene owns the objects
    void Update(float dt);
    void Render(ID3D11DeviceContext* ctx) const;

private:
    Camera camera_;
    std::unique_ptr<LightSystem> lights_;
    std::vector<SceneObject*> objects_;
};

