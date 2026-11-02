#pragma once
#include "Camera.h"
#include "SceneObject.h"
#include "Light.h"
#include "Terrain.h"
#include <d3d11.h>
#include <vector>
#include <memory>
#include <array>

// Input state passed from Window to Scene
struct InputState {
    std::array<bool, 256> keys = {};  // Keyboard state for all keys
    int mouseDeltaX = 0;
    int mouseDeltaY = 0;
    bool leftMouseDown = false;
    bool ctrlDown = false;
};

class Scene {
public:
    Scene();
    ~Scene();

    bool Initialize(ID3D11Device* device);
    const Camera& GetCamera() const { return camera_; }
    Camera& GetCamera() { return camera_; }
    Terrain* GetTerrain() const { return terrain_; }
    bool RegenerateTerrain(const TerrainSettings& settings);
    const std::vector<SceneObject*>& GetObjects() const { return objects_; }
    LightSystem& GetLightSystem() { return *lights_; }
    const LightSystem& GetLightSystem() const { return *lights_; }
    void AddObject(SceneObject* obj);      // Scene owns the objects
    void ProcessInput(const InputState& input, float dt);  // Handle input and update camera
    void Update(float dt);  // Update scene objects
    void Render(ID3D11DeviceContext* ctx) const;

private:
    void UpdateCamera(const InputState& input, float dt);

    Camera camera_;
    std::unique_ptr<LightSystem> lights_;
    std::vector<SceneObject*> objects_;
    Terrain* terrain_ = nullptr;
    float smoothedMouseDeltaX_ = 0.0f;
    float smoothedMouseDeltaY_ = 0.0f;
};

