#pragma once
#include "Camera.h"
#include "SceneObject.h"
#include "Light.h"
#include "Terrain.h"
#include "FrameConstants.h"
#include <d3d11.h>
#include <vector>
#include <memory>
#include <array>

class Water;
class Skybox;

// Input state passed from Window to Scene
struct InputState {
    std::array<bool, 256> keys = {};  // Keyboard state for all keys
    int mouseDeltaX = 0;
    int mouseDeltaY = 0;
    bool middleMouseButtonDown = false;
};

class Scene {
public:
    Scene();
    ~Scene();

    bool Initialize(ID3D11Device* device);
    const Camera& GetCamera() const { return camera_; }
    Camera& GetCamera() { return camera_; }
    Terrain* GetTerrain() const { return terrain_; }
    Water* GetWater() const { return water_; }
    Skybox* GetSkybox() const { return skybox_; }
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
    FrameConstants frame_;   // view/proj/camera/time, uploaded once per frame
    float time_ = 0.0f;      // scene clock shared by every animated material
    std::unique_ptr<LightSystem> lights_;
    std::vector<SceneObject*> objects_;
    Terrain* terrain_ = nullptr;
    Water* water_ = nullptr;
    Skybox* skybox_ = nullptr;
    float smoothedMouseDeltaX_ = 0.0f;
    float smoothedMouseDeltaY_ = 0.0f;
};

