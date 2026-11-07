#include "SceneSerializer.h"
#include "Cube.h"
#include "Logger.h"
#include "ModelImporter.h"
#include "ModelObject.h"
#include <fstream>
#include <filesystem>
#include "SceneJson.h"

namespace fs = std::filesystem;

namespace {

constexpr const char* kFormatName = "d3d11-scene";

json TransformToJson(const SceneObject& object)
{
    DirectX::XMFLOAT3 position, rotation, scale;
    if (!object.GetEditorTransform(position, rotation, scale)) return nullptr;
    return json{ { "position", position }, { "rotation", rotation }, { "scale", scale } };   // rotation in radians
}

void ApplyTransform(SceneObject& object, const json& source)
{
    if (!source.is_object()) return;
    DirectX::XMFLOAT3 position, rotation, scale;
    if (!object.GetEditorTransform(position, rotation, scale)) return;
    object.SetEditorTransform(source.value("position", position), source.value("rotation", rotation), source.value("scale", scale));
}

// A path for the file: relative to the scene file's folder when possible (so the scene and its assets can move as a
// whole), absolute otherwise. `outsideAssets` says the model does not live under the project's "assets" folder, which
// is the folder that gets shipped: such a model will be missing on another machine.
std::string MakeStoredPath(const fs::path& target, const fs::path& sceneFolder, const fs::path& assetsRoot, bool& outsideAssets)
{
    std::error_code error;
    const fs::path absolute = fs::weakly_canonical(target, error);
    const fs::path base = fs::weakly_canonical(sceneFolder, error);
    const fs::path assets = fs::weakly_canonical(assetsRoot, error);

    const fs::path insideAssets = absolute.lexically_relative(assets);   // empty if there is no relative route (another drive)
    outsideAssets = insideAssets.empty() || *insideAssets.begin() == "..";

    const fs::path relative = absolute.lexically_relative(base);
    return (relative.empty() ? absolute : relative).generic_string();   // forward slashes: the same file text on any machine
}

}  // namespace

bool SaveScene(const Scene& scene, const std::string& path, std::vector<std::string>& warnings, std::string& error)
{
    const fs::path file = fs::absolute(fs::path(path));
    const fs::path folder = file.parent_path();

    json root;
    root["format"] = kFormatName;
    root["version"] = kSceneFileVersion;
    root["camera"] = { { "position", scene.GetCamera().GetPosition() }, { "target", scene.GetCamera().GetTarget() } };

    const LightSystem& lights = scene.GetLightSystem();
    json points = json::array();
    for (const PointLight& light : lights.PointLights()) points.push_back(light);
    root["lights"] = { { "ambient", lights.Ambient() }, { "directional", lights.Directional() }, { "points", points } };
    root["shadow"] = scene.GetShadowMap().GetSettings();
    root["post"] = scene.GetPostSettings();
    root["environment"] = scene.GetEnvironment();

    json objects = json::array();
    for (const auto& owned : scene.GetObjects()) {
        const SceneObject* object = owned.get();
        if (!object) continue;
        json entry;
        if (const auto* terrain = dynamic_cast<const Terrain*>(object)) {
            entry["type"] = "Terrain";
            entry["settings"] = terrain->GetSettings();

            // The images the terrain was made from. Only the ones the user REPLACED are written; the defaults need no entry.
            auto stored = [&](const std::string& current, const std::string& standard) -> json {
                if (current == standard) return nullptr;
                bool outside = false;
                const std::string text = MakeStoredPath(fs::path(current), folder, "assets", outside);
                if (outside) {
                    warnings.push_back("The terrain image \"" + current + "\" lives outside the assets folder. It will not be shipped with the game: copy it into assets/ and choose it again.");
                }
                return text;
            };
            json maps = json::object();
            if (json v = stored(terrain->GetHeightmapPath(), Terrain::DefaultHeightmapPath()); !v.is_null()) maps["heightmap"] = v;
            if (json v = stored(terrain->GetColorMapPath(), Terrain::DefaultColorMapPath()); !v.is_null()) maps["colorMap"] = v;
            static const char* const kMapKeys[] = { "albedo", "normal", "orm" };
            json layers = json::array();
            bool anyLayer = false;
            for (int layer = 0; layer < kTerrainLayerCount; ++layer) {
                json one = json::object();
                for (int kind = 0; kind < 3; ++kind) {
                    const auto map = static_cast<TerrainLayerMap>(kind);
                    if (json v = stored(terrain->GetLayerMapPath(layer, map), Terrain::DefaultLayerMapPath(layer, map)); !v.is_null()) {
                        one[kMapKeys[kind]] = v;
                        anyLayer = true;
                    }
                }
                layers.push_back(one);
            }
            if (anyLayer) maps["layers"] = layers;
            if (!maps.empty()) entry["maps"] = maps;
        } else if (const auto* water = dynamic_cast<const Water*>(object)) {
            entry["type"] = "Water";
            entry["settings"] = water->GetSettings();
        } else if (const auto* sky = dynamic_cast<const Skybox*>(object)) {
            entry["type"] = "Skybox";
            entry["intensity"] = sky->GetIntensity();
        } else if (const auto* cube = dynamic_cast<const Cube*>(object)) {
            entry["type"] = "Cube";
            entry["spinning"] = cube->IsSpinning();
            entry["surface"] = cube->GetSurface().Settings();
        } else if (const auto* model = dynamic_cast<const ModelObject*>(object)) {
            entry["type"] = "Model";
            bool outside = false;
            entry["path"] = MakeStoredPath(fs::path(model->GetSourcePath()), folder, "assets", outside);
            if (outside) {
                warnings.push_back("The model \"" + std::string(model->GetEditorName()) + "\" lives outside the assets folder (" +
                                   model->GetSourcePath() + "). It will not be shipped with the game: copy its folder into assets/ and import it from there.");
            }
            json materials = json::array();
            for (size_t i = 0; i < model->GetMaterialCount(); ++i) {
                materials.push_back({ { "name", model->GetMaterialName(i) }, { "surface", model->GetMaterial(i).Settings() } });
            }
            entry["materials"] = materials;
        } else {
            warnings.push_back(std::string("Skipped an object this editor cannot save yet: ") + object->GetEditorName());
            continue;
        }
        const json transform = TransformToJson(*object);
        if (!transform.is_null()) entry["transform"] = transform;
        objects.push_back(std::move(entry));
    }
    root["objects"] = std::move(objects);

    // Write to a temporary file first, then swap it in: a crash half-way through must never leave a
    // half-written scene in place of the good one.
    const fs::path temporary = file.string() + ".tmp";
    std::error_code fsError;
    fs::create_directories(folder, fsError);
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) { error = "Cannot write to " + temporary.string(); return false; }
        out << root.dump(2) << '\n';
        if (!out) { error = "Writing the scene failed (disk full?)."; return false; }
    }
    fs::rename(temporary, file, fsError);
    if (fsError) {   // some platforms refuse to rename over an existing file
        fs::remove(file, fsError);
        fs::rename(temporary, file, fsError);
        if (fsError) { error = "Could not replace " + file.string() + ": " + fsError.message(); return false; }
    }
    return true;
}

bool LoadScene(Scene& scene, ID3D11Device* device, const std::string& path, std::vector<std::string>& warnings, std::string& error)
{
    // Step 1: read and parse. Nothing in the scene has changed yet, so a bad file costs nothing.
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "Cannot open " + path; return false; }
    json root;
    try {
        root = json::parse(in);
    } catch (const json::exception& e) {
        error = std::string("The file is not valid JSON: ") + e.what();
        return false;
    }
    if (!root.is_object() || root.value("format", "") != kFormatName) {
        error = "This is not a scene file written by this editor.";
        return false;
    }
    if (root.value("version", 0) > kSceneFileVersion) {
        warnings.push_back("This scene was saved by a newer version of the editor; anything it does not understand is ignored.");
    }

    // Step 2: replace the contents. From here a problem affects one section or one object, never the whole load.
    const fs::path sceneFolder = fs::absolute(fs::path(path)).parent_path();
    scene.ClearObjects();

    auto attempt = [&](const std::string& what, auto&& action) {
        try {
            action();
        } catch (const json::exception& e) {
            warnings.push_back(what + ": " + e.what());
        }
    };

    attempt("camera", [&] {
        const json camera = root.value("camera", json::object());
        scene.GetCamera().SetPosition(camera.value("position", scene.GetCamera().GetPosition()));
        scene.GetCamera().SetTarget(camera.value("target", scene.GetCamera().GetTarget()));
    });
    attempt("lights", [&] {
        const json lights = root.value("lights", json::object());
        LightSystem& system = scene.GetLightSystem();
        system.Ambient() = lights.value("ambient", system.Ambient());
        system.Directional() = lights.value("directional", system.Directional());
        system.ClearPointLights();
        for (const json& point : lights.value("points", json::array())) {
            if (!system.AddPointLight(point.get<PointLight>())) { warnings.push_back("Too many point lights: the extra ones were dropped."); break; }
        }
    });
    attempt("shadow settings", [&] { scene.GetShadowMap().SetSettings(root.value("shadow", scene.GetShadowMap().GetSettings())); });
    attempt("post-processing", [&] {
        scene.GetPostSettings() = root.value("post", scene.GetPostSettings());
        scene.GetPostSettings().Clamp();
    });
    attempt("environment", [&] { scene.GetEnvironment() = root.value("environment", scene.GetEnvironment()); });

    for (const json& entry : root.value("objects", json::array())) {
        const std::string type = entry.value("type", "");
        attempt("object \"" + type + "\"", [&] {
            SceneObject* created = nullptr;
            if (type == "Terrain") {
                Terrain* terrain = scene.Add<Terrain>(device);

                // Replaced images first (the heightmap decides the mesh shape), then the settings.
                const json maps = entry.value("maps", json::object());
                auto resolve = [&](const std::string& text) {
                    fs::path p(text);
                    if (p.is_relative()) p = sceneFolder / p;   // stored relative to the scene file
                    return p.string();
                };
                if (maps.contains("heightmap") && !terrain->SetHeightmap(resolve(maps["heightmap"].get<std::string>()))) {
                    warnings.push_back("The terrain's heightmap could not be loaded: " + maps["heightmap"].get<std::string>() + " (the default is used)");
                }
                if (maps.contains("colorMap") && !terrain->SetColorMap(resolve(maps["colorMap"].get<std::string>()))) {
                    warnings.push_back("The terrain's colour map could not be loaded: " + maps["colorMap"].get<std::string>() + " (the default is used)");
                }
                if (maps.contains("layers") && maps["layers"].is_array()) {
                    static const char* const kMapKeys[] = { "albedo", "normal", "orm" };
                    for (int layer = 0; layer < kTerrainLayerCount && layer < static_cast<int>(maps["layers"].size()); ++layer) {
                        for (int kind = 0; kind < 3; ++kind) {
                            if (!maps["layers"][layer].contains(kMapKeys[kind])) continue;
                            const std::string text = maps["layers"][layer][kMapKeys[kind]].get<std::string>();
                            if (!terrain->SetLayerMap(layer, static_cast<TerrainLayerMap>(kind), resolve(text))) {
                                warnings.push_back(std::string("A terrain ") + kTerrainLayerNames[layer] + " " + kMapKeys[kind] + " image could not be loaded: " + text + " (the default is used)");
                            }
                        }
                    }
                }
                if (!terrain->ApplySettings(entry.value("settings", TerrainSettings{}))) {
                    warnings.push_back("The terrain mesh could not be rebuilt with the saved settings; the default terrain is shown.");
                }
                created = terrain;
            } else if (type == "Water") {
                Water* water = scene.Add<Water>(device);
                water->SetSettings(entry.value("settings", WaterSettings{}));
                created = water;
            } else if (type == "Skybox") {
                Skybox* sky = scene.Add<Skybox>(device);
                sky->SetIntensity(entry.value("intensity", 1.0f));
                created = sky;
            } else if (type == "Cube") {
                Cube* cube = scene.Add<Cube>(device);
                cube->GetSurface().Settings() = entry.value("surface", SurfaceSettings{});
                created = cube;
            } else if (type == "Model") {
                fs::path modelPath(entry.value("path", ""));
                if (modelPath.is_relative()) modelPath = sceneFolder / modelPath;   // stored relative to the scene file
                ImportedModel imported;
                std::string importError;
                if (!ImportModel(modelPath.string(), imported, importError)) {
                    warnings.push_back("Skipped a model: " + importError);
                    return;
                }
                ModelObject* model = scene.Add<ModelObject>(device, std::move(imported));
                if (!model->IsValid()) {
                    scene.Remove(model->GetId());
                    warnings.push_back("Skipped a model that could not be uploaded to the GPU: " + modelPath.string());
                    return;
                }
                const json materials = entry.value("materials", json::array());
                for (size_t i = 0; i < materials.size() && i < model->GetMaterialCount(); ++i) {
                    if (materials[i].value("name", "") != model->GetMaterialName(i)) {   // the model file changed since saving
                        warnings.push_back("A material of \"" + model->GetSourcePath() + "\" no longer matches the saved one; its saved settings were skipped.");
                        continue;
                    }
                    model->GetMaterial(i).Settings() = materials[i].value("surface", model->GetMaterial(i).Settings());
                }
                created = model;
            } else {
                warnings.push_back("Skipped an object of unknown type: \"" + type + "\"");
                return;
            }
            // Transform LAST: setting the transform of some objects (the cube, the water) changes other state.
            if (entry.contains("transform")) ApplyTransform(*created, entry["transform"]);
            if (type == "Cube") static_cast<Cube*>(created)->SetSpinning(entry.value("spinning", false));
        });
    }
    return true;
}
