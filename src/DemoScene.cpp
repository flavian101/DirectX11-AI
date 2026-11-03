#include "DemoScene.h"
#include "Cube.h"
#include "Logger.h"
#include "Skybox.h"
#include "Terrain.h"
#include "Water.h"
#include <algorithm>

bool BuildDemoScene(Scene& scene, ID3D11Device* device)
{
    if (!scene.Initialize(device)) {
        LOG_ERROR("Scene::Initialize failed");
        return false;
    }

    // --- camera ---
    scene.GetCamera().SetPosition({ 0.0f, 10.0f, -20.0f });
    scene.GetCamera().SetTarget({ 0.0f, 12.0f, 0.0f });

    // --- lights ---
    LightSystem& lights = scene.GetLightSystem();
    DirectionalLight& sun = lights.Directional();
    sun.direction = { 0.3f, -0.8f, 0.5f };
    sun.color = { 1.0f, 0.95f, 0.85f };
    // HDR: the sun is far brighter than "1.0 = white". The tone mapper brings it back into range;
    // set the tone mapper to "None" in the Camera panel to see what clipping looks like.
    sun.intensity = 2.5f;
    lights.SetAmbient({ 0.2f, 0.2f, 0.25f });

    PointLight warm;
    warm.position = { 3.0f, 2.0f, 3.0f };
    warm.color = { 1.0f, 0.6f, 0.3f };
    warm.intensity = 1.5f;
    warm.range = 8.0f;
    lights.AddPointLight(warm);

    PointLight cool;
    cool.position = { -3.0f, 1.0f, -3.0f };
    cool.color = { 0.3f, 0.6f, 1.0f };
    cool.intensity = 1.2f;
    cool.range = 7.0f;
    lights.AddPointLight(cool);

    PointLight lime;
    lime.position = { 0.0f, 3.0f, -4.0f };
    lime.color = { 0.8f, 1.0f, 0.4f };
    lime.intensity = 1.0f;
    lime.range = 6.0f;
    lights.AddPointLight(lime);

    // --- objects (the scene owns them) ---
    Terrain* terrain = scene.Add<Terrain>(device);
    scene.Add<Cube>(device);
    Water* water = scene.Add<Water>(device);
    scene.Add<Skybox>(device);

    // Cover the whole terrain (plus a margin) whatever its width setting is.
    WaterSettings waterSettings = water->GetSettings();
    const DirectX::XMFLOAT2 extent = terrain->GetWorldExtent();
    waterSettings.size = (std::max)(extent.x, extent.y) * 1.2f;
    water->SetSettings(waterSettings);
    return true;
}
