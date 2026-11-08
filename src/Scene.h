#pragma once
#include "Camera.h"
#include "SceneObject.h"
#include "Light.h"
#include "Terrain.h"
#include "Water.h"
#include "Skybox.h"
#include "EnvironmentLighting.h"
#include "FrameConstants.h"
#include "PostSettings.h"
#include "ShadowMap.h"
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

enum class ScenePhase { Opaque, Transparent };

class Scene {
public:
    Scene();
    ~Scene();

    // Engine-level setup only (lights + per-frame buffer). What goes INTO the scene
    // (terrain, water, camera placement...) is decided by whoever builds it - see DemoScene.cpp.
    bool Initialize(ID3D11Device* device);
    const Camera& GetCamera() const { return camera_; }
    Camera& GetCamera() { return camera_; }
    Terrain* GetTerrain() { return FindFirst<Terrain>(); }
    const Terrain* GetTerrain() const { return FindFirst<Terrain>(); }
    // The FIRST water body (scenes can have several: loop over GetObjects(), or use FindWaterAt for a position).
    Water* GetWater() { return FindFirst<Water>(); }
    const Water* GetWater() const { return FindFirst<Water>(); }
    Skybox* GetSkybox() { return FindFirst<Skybox>(); }
    const Skybox* GetSkybox() const { return FindFirst<Skybox>(); }
    const std::vector<std::unique_ptr<SceneObject>>& GetObjects() const { return objects_; }
    LightSystem& GetLightSystem() { return *lights_; }
    const LightSystem& GetLightSystem() const { return *lights_; }

    SceneObject* Find(SceneObjectId id);
    const SceneObject* Find(SceneObjectId id) const;
    bool Remove(SceneObjectId id);
    // Take an object OUT of the scene without destroying it, and put it back later with the SAME id and name.
    // Undo of "delete" needs this: the object (and its GPU resources) must stay alive while it is not in the scene.
    // `index` is where it stood in the object list, so that Attach can restore the order.
    std::unique_ptr<SceneObject> Detach(SceneObjectId id, size_t* index = nullptr);
    SceneObject* Attach(std::unique_ptr<SceneObject> object, size_t index);    SceneObject* Duplicate(SceneObjectId id);
    SceneObject* CreateCube();
    // A new small water body (a pool). The caller places it (the editor puts it in front of the camera).
    // `type` picks the starting size and look: a pond (default), a lake, a river or an ocean.
    Water* CreateWater(WaterBodyType type = WaterBodyType::Pond);
    // The water body at/below a world position, for gameplay: among bodies whose outline contains (x, z), the one whose
    // surface is highest while still at or below `y` (an object floating in a pool above a lake belongs to the pool);
    // if `y` is below all of them, the one nearest above (the point is under water). Null if no body covers (x, z).
    const Water* FindWaterAt(float x, float y, float z) const;
    // Removes every object (and the baked sky lighting that belonged to the old sky). Lights, camera and settings stay.
    void ClearObjects();

    template <class T>
    T* FindFirst()
    {
        static_assert(std::is_base_of_v<SceneObject, T>, "FindFirst<T>: T must derive from SceneObject");
        for (auto& object : objects_) {
            if (auto* typed = dynamic_cast<T*>(object.get())) return typed;
        }
        return nullptr;
    }

    template <class T>
    const T* FindFirst() const
    {
        static_assert(std::is_base_of_v<SceneObject, T>, "FindFirst<T>: T must derive from SceneObject");
        for (const auto& object : objects_) {
            if (auto* typed = dynamic_cast<const T*>(object.get())) return typed;
        }
        return nullptr;
    }

    // Constructs a T in place, takes ownership and returns a non-owning pointer:
    //     Terrain* terrain = scene.Add<Terrain>(device);
    // Scene assigns identity and a default instance name when it adopts the object.
    template <class T, class... Args>
    T* Add(Args&&... args)
    {
        static_assert(std::is_base_of_v<SceneObject, T>, "Scene::Add<T>: T must derive from SceneObject");
        return static_cast<T*>(Adopt(std::make_unique<T>(std::forward<Args>(args)...)));
    }
    void ProcessInput(const InputState& input, float dt);  // Handle input and update camera
    void Update(float dt);  // Update scene objects
    // Pass 1: draw every shadow caster's depth from the sun into the shadow map.
    // Pass 2 (Render) is the normal camera view. The renderer calls them in that order.
    void RenderShadows(ID3D11DeviceContext* ctx) const;
    // Pass 2 is split in two so the renderer can do something between the halves:
        //   RenderPhase     - Opaque (sky + solid objects, write depth), then Transparent (water)
    //   BeginFrame      - bind the shared per-frame resources (shadow map, environment map)
    //   SetMainView     - upload the camera's matrices for the pass that draws the final image
    //   RenderReflection- the scene seen from a camera mirrored under the water (optional pass)
    void BeginFrame(ID3D11DeviceContext* ctx) const;
    void SetMainView(ID3D11DeviceContext* ctx, UINT viewportWidth, UINT viewportHeight,
                     bool reflectionAvailable, bool refractionAvailable = false) const;
    // Decides, for every water body, what it needs this frame: a mirror image (shared between bodies at the same level,
    // at most WaterFramePlan::kMaxReflections of them, nearest first) and whether the opaque scene must be copied for
    // refraction. Bodies that are off screen, or that the camera is under, need nothing. Call once per frame, before drawing.
    void PlanWater(WaterFramePlan& plan) const;
    // The scene clock, in seconds: the same time the shaders animate with. Pass it to Water::SampleHeight.
    float GetTime() const { return time_; }
    void RenderReflection(ID3D11DeviceContext* ctx, float planeHeight, UINT targetWidth, UINT targetHeight) const;
    void RenderPhase(ID3D11DeviceContext* ctx, ScenePhase phase, const RenderView& view) const;
    RenderView GetMainView() const;   // the camera's view: its frustum and position
    ShadowMap& GetShadowMap() { return *shadow_; }
    const ShadowMap& GetShadowMap() const { return *shadow_; }
    // Image-based lighting: the sky baked into small textures (lesson 19). The first frame bakes it;
    // InvalidateEnvironment() makes the next frame bake again.
    EnvironmentSettings& GetEnvironment() { return environment_; }
    const EnvironmentSettings& GetEnvironment() const { return environment_; }
    const EnvironmentLighting& GetEnvironmentLighting() const { return environmentLighting_; }
    void InvalidateEnvironment() { environmentLighting_.Release(); }
    // Bakes the sky if it has not been (or has changed). Called by the renderer before it draws.
    void PrepareEnvironment(ID3D11DeviceContext* ctx) const;
    PostSettings& GetPostSettings() { return post_; }
    const PostSettings& GetPostSettings() const { return post_; }

private:
    void UpdateCamera(const InputState& input, float dt);
    void FillEnvironmentFrameData(FrameData& frame) const;   // the IBL numbers every lit pixel needs
    bool ComputeShadowBounds(DirectX::XMFLOAT3& center, float& radius) const;
    SceneObject* Adopt(std::unique_ptr<SceneObject> object);

    Camera camera_;
    FrameConstants frame_;   // view/proj/camera/time, uploaded once per frame
    PostSettings post_;      // how the HDR image is converted for display (a property of the "camera")
    EnvironmentSettings environment_;
    mutable EnvironmentLighting environmentLighting_;   // a cache of the baked sky: rebuilt on demand, so "mutable" even in const methods
    float time_ = 0.0f;      // scene clock shared by every animated material
    std::unique_ptr<LightSystem> lights_;
    std::unique_ptr<ShadowMap> shadow_;
    ID3D11Device* device_ = nullptr; // non-owning; Scene is destroyed before the D3D device
    SceneObjectId nextObjectId_ = 1;
    std::vector<std::unique_ptr<SceneObject>> objects_;   // owning
    float smoothedMouseDeltaX_ = 0.0f;
    float smoothedMouseDeltaY_ = 0.0f;
};

