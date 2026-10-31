#pragma once
#include "Camera.h"
#include "SceneObject.h"
#include <d3d11.h>
#include <vector>

class Scene {
public:
    Scene();
    ~Scene();

    const Camera& GetCamera() const { return camera_; }
    void AddObject(SceneObject* obj);      // Scene owns the objects
    void Update(float dt);
    void Render(ID3D11DeviceContext* ctx) const;

private:
    Camera camera_;
    std::vector<SceneObject*> objects_;
};

