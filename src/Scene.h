#pragma once
#include "Camera.h"
#include "SceneObject.h"
#include "Light.h"
#include "Terrain.h"
#include "Water.h"
#include "Skybox.h"
#include "FrameConstants.h"
#include <d3d11.h>
#include <vector>
#include <memory>
#include <array>
#include <type_traits>
#include <utility>

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

    // Engine-level setup only (lights + per-frame buffer). What goes INTO the scene
    // (terrain, water, camera placement...) is decided by whoever builds it - see DemoScene.cpp.
    bool Initialize(ID3D11Device* device);
    const Camera& GetCamera() const { return camera_; }
    Camera& GetCamera() { return camera_; }
    Terrain* GetTerrain() const { return terrain_; }
    Water* GetWater() const { return water_; }
    Skybox* GetSkybox() const { return skybox_; }
    bool RegenerateTerrain(const TerrainSettings& settings);
    const std::vector<std::unique_ptr<SceneObject>>& GetObjects() const { return objects_; }
    LightSystem& GetLightSystem() { return *lights_; }
    const LightSystem& GetLightSystem() const { return *lights_; }

    // Constructs a T in place, takes ownership and returns a non-owning pointer:
    //     Terrain* terrain = scene.Add<Terrain>(device);
    // The scene remembers the most recently added Terrain / Water / Skybox so the editor
    // and the renderer can find them (no dynamic_cast needed).
    template <class T, class... Args>
    T* Add(Args&&... args)
    {
        static_assert(std::is_base_of_v<SceneObject, T>, "Scene::Add<T>: T must derive from SceneObject");
        auto object = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = object.get();
        if constexpr (std::is_base_of_v<Terrain, T>) terrain_ = raw;
        if constexpr (std::is_base_of_v<Water, T>) water_ = raw;
        if constexpr (std::is_base_of_v<Skybox, T>) skybox_ = raw;
        objects_.push_back(std::move(object));
        return raw;
    }
    void ProcessInput(const InputState& input, float dt);  // Handle input and update camera
    void Update(float dt);  // Update scene objects
    void Render(ID3D11DeviceContext* ctx) const;

private:
    void UpdateCamera(const InputState& input, float dt);

    Camera camera_;
    FrameConstants frame_;   // view/proj/camera/time, uploaded once per frame
    float time_ = 0.0f;      // scene clock shared by every animated material
    std::unique_ptr<LightSystem> lights_;
    std::vector<std::unique_ptr<SceneObject>> objects_;   // owning
    Terrain* terrain_ = nullptr;
    Water* water_ = nullptr;
    Skybox* skybox_ = nullptr;
    float smoothedMouseDeltaX_ = 0.0f;
    float smoothedMouseDeltaY_ = 0.0f;
};

