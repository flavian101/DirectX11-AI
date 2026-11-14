#include "DemoScene.h"
#include "Cube.h"
#include "Logger.h"
#include "Project.h"
#include "SceneSerializer.h"
#include <filesystem>
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
    // The sun is a light OBJECT (select it in the Hierarchy, aim it with the gizmo or the Inspector), and there are no lamps:
    // add point lights where the scene needs them. Only the flat fallback colour, used when the sky is not lighting the scene, stays global.
    scene.GetLightSystem().SetAmbient({ 0.2f, 0.2f, 0.25f });
    LightObject* sun = scene.Add<LightObject>(LightKind::Directional);   // starts as the sun preset: warm, HDR-bright (2.5; the tone mapper brings it back)
    sun->SetName("Sun");
    float sunAzimuth = 0.0f, sunElevation = 0.0f;
    LightAngles::FromTravel({ 0.3f, -0.8f, 0.5f }, sunAzimuth, sunElevation);   // the direction this scene always had
    sun->SetAngles(sunAzimuth, sunElevation);

    // A light haze, so far hills fade into the sky instead of ending at a hard edge. (Weather tab > Fog.)
    FogSettings& fog = scene.GetWeather().fog;
    fog.enabled = true;
    fog.visibility = 1500.0f;
    fog.heightFalloff = 0.01f;

    // --- objects (the scene owns them) ---
    Terrain* terrain = scene.Add<Terrain>(device);
    scene.Add<Cube>(device);
    Water* water = scene.Add<Water>(device);
    scene.Add<Skybox>(device);

    // Cover the whole terrain (plus a margin) whatever its width setting is.
    WaterSettings waterSettings = water->GetSettings();
    const DirectX::XMFLOAT2 extent = terrain->GetWorldExtent();
    waterSettings.size = (std::max)(extent.x, extent.y) * 1.2f;
    waterSettings.sizeZ = waterSettings.size;
    water->SetSettings(waterSettings);
    return true;
}

std::unique_ptr<Scene> BuildStartScene(ID3D11Device* device)
{
    // A project writes assets/startup.json naming its start scene: that is how a shipped game knows where to begin.
    // Without one (or when the scene it names is gone) the old fixed file is used, exactly as before projects existed.
    std::error_code error;
    std::string startFile = kStartSceneFile;
    const std::string startup = ReadStartupScene("assets");
    if (!startup.empty() && std::filesystem::exists("assets/" + startup, error)) startFile = "assets/" + startup;
    if (std::filesystem::exists(startFile, error)) {
        auto scene = std::make_unique<Scene>();
        if (scene->Initialize(device)) {
            std::vector<std::string> warnings;
            std::string message;
            if (LoadScene(*scene, device, startFile, warnings, message)) {
                for (const std::string& warning : warnings) LOG_WARNING(warning);
                return scene;
            }
            LOG_ERROR(std::string("Could not load ") + startFile + ": " + message + " (showing the demo scene instead)");
        }
        // `scene` is dropped here: a half-loaded scene is in an unknown state, so the demo gets a fresh one.
    }
    auto scene = std::make_unique<Scene>();
    return BuildDemoScene(*scene, device) ? std::move(scene) : nullptr;
}
