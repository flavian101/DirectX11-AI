#include "SceneSerializer.h"
#include "Cube.h"
#include "Logger.h"
#include "ModelImporter.h"
#include "ModelObject.h"
#include "ProceduralModels.h"
#include "ScatterObject.h"
#include "Png16.h"
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


// ------------------------------------------------------------------------------------------------------------
// One object <-> one JSON entry. The same code serves THREE jobs: saving a file, loading a file, and the
// snapshots that undo/redo takes. With `folder` set (file mode) image and model paths are stored relative to the
// scene file; with `folder == nullptr` (snapshot mode) they are kept exactly as they are, so comparing two
// snapshots is a plain text comparison.
// ------------------------------------------------------------------------------------------------------------
// `assetsRoot`: the folder that gets shipped. A file outside it is saved anyway but reported (file mode only).
json DescribeObject(const SceneObject& object, const fs::path* folder, std::vector<std::string>* warnings, const fs::path& assetsRoot)
{
    const bool snapshot = (folder == nullptr);
    auto warn = [&](const std::string& text) { if (warnings) warnings->push_back(text); };
    // A path as it goes into the entry. Returns null when `current` is the default (defaults need no entry).
    auto storedImage = [&](const std::string& current, const std::string& standard) -> json {
        if (current == standard) return nullptr;
        if (snapshot) return current;
        bool outside = false;
        const std::string text = MakeStoredPath(fs::path(current), *folder, assetsRoot, outside);
        if (outside) warn("The terrain image \"" + current + "\" lives outside the assets folder. It will not be shipped with the game: copy it into assets/ and choose it again.");
        return text;
    };

    json entry;
    if (const auto* terrain = dynamic_cast<const Terrain*>(&object)) {
        entry["type"] = "Terrain";
        entry["settings"] = terrain->GetSettings();
        json maps = json::object();
        if (json v = storedImage(terrain->GetHeightmapPath(), Terrain::DefaultHeightmapPath()); !v.is_null()) maps["heightmap"] = v;
        static const char* const kMapKeys[] = { "albedo", "normal", "orm" };
        json layers = json::array();
        bool anyLayer = false;
        for (int layer = 0; layer < kTerrainLayerCount; ++layer) {
            json one = json::object();
            for (int kind = 0; kind < 3; ++kind) {
                const auto map = static_cast<TerrainLayerMap>(kind);
                if (json v = storedImage(terrain->GetLayerMapPath(layer, map), Terrain::DefaultLayerMapPath(layer, map)); !v.is_null()) {
                    one[kMapKeys[kind]] = v;
                    anyLayer = true;
                }
            }
            layers.push_back(one);
        }
        if (anyLayer) maps["layers"] = layers;
        if (!maps.empty()) entry["maps"] = maps;
    } else if (const auto* water = dynamic_cast<const Water*>(&object)) {
        entry["type"] = "Water";
        entry["name"] = water->GetName();   // a scene can hold several bodies ("Lake", "Pool"): they keep their names
        entry["settings"] = water->GetSettings();
    } else if (const auto* sky = dynamic_cast<const Skybox*>(&object)) {
        entry["type"] = "Skybox";
        entry["intensity"] = sky->GetIntensity();
    } else if (const auto* cube = dynamic_cast<const Cube*>(&object)) {
        entry["type"] = "Cube";
        entry["spinning"] = cube->IsSpinning();
        entry["surface"] = cube->GetSurface().Settings();
    } else if (const auto* model = dynamic_cast<const ModelObject*>(&object)) {
        entry["type"] = "Model";
        if (snapshot) {
            entry["path"] = model->GetSourcePath();
        } else {
            bool outside = false;
            entry["path"] = MakeStoredPath(fs::path(model->GetSourcePath()), *folder, assetsRoot, outside);
            if (outside) {
                warn("The model \"" + std::string(model->GetEditorName()) + "\" lives outside the assets folder (" +
                     model->GetSourcePath() + "). It will not be shipped with the game: copy its folder into assets/ and import it from there.");
            }
        }
        json materials = json::array();
        for (size_t i = 0; i < model->GetMaterialCount(); ++i) {
            materials.push_back({ { "name", model->GetMaterialName(i) }, { "surface", model->GetMaterial(i).Settings() } });
        }
        entry["materials"] = materials;
        entry["render"] = model->GetRenderSettings();   // culling and level of detail
    } else if (const auto* scatter = dynamic_cast<const ScatterObject*>(&object)) {
        entry["type"] = "Scatter";
        ScatterSettings settings = scatter->GetSettings();
        if (!snapshot) {
            for (ScatterSpeciesSettings& species : settings.species) {
                if (species.model.empty() || IsBuiltinModel(species.model)) continue;   // the built-in ones are not files
                bool outside = false;
                species.model = MakeStoredPath(fs::path(species.model), *folder, assetsRoot, outside);
                if (outside) {
                    warn("The scatter species \"" + species.name + "\" uses a model outside the assets folder (" + species.model +
                         "). It will not be shipped with the game: copy its folder into assets/ and choose it again.");
                }
            }
        }
        entry["settings"] = settings;
    } else {
        warn(std::string("Skipped an object this editor cannot save yet: ") + object.GetEditorName());
        return nullptr;
    }

    // A spinning demo cube changes its rotation every frame by itself. In a snapshot that would look like an edit
    // on every frame, so a spinning cube's transform is left out (a file still stores it).
    const bool spinningCube = snapshot && entry["type"] == "Cube" && entry.value("spinning", false);
    if (!spinningCube) {
        const json transform = TransformToJson(object);
        if (!transform.is_null()) entry["transform"] = transform;
    }
    return entry;
}

// Puts the properties in `entry` onto an object that already exists (just created by the loader, or alive in the
// scene when undo restores an earlier state). Images that already match are not loaded again.
void ApplyObject(SceneObject& object, const json& entry, const fs::path* sceneFolder, std::vector<std::string>& warnings)
{
    auto resolve = [&](const std::string& text) {
        fs::path p(text);
        if (sceneFolder && p.is_relative()) p = *sceneFolder / p;   // file mode: stored relative to the scene file
        return p.string();
    };

    if (auto* terrain = dynamic_cast<Terrain*>(&object)) {
        // Images first (the heightmap decides the mesh shape), then the settings. A key that is ABSENT means "the
        // default image", which matters for undo: undoing an image import must put the old image back.
        const json maps = entry.value("maps", json::object());
        auto wanted = [&](const json& source, const char* key, const std::string& standard) {
            return source.is_object() && source.contains(key) ? resolve(source[key].get<std::string>()) : standard;
        };
        const std::string height = wanted(maps, "heightmap", Terrain::DefaultHeightmapPath());
        if (height != terrain->GetHeightmapPath() && !terrain->SetHeightmap(height)) warnings.push_back("The terrain's heightmap could not be loaded: " + height);
        static const char* const kMapKeys[] = { "albedo", "normal", "orm" };
        for (int layer = 0; layer < kTerrainLayerCount; ++layer) {
            const json layerEntry = (maps.contains("layers") && maps["layers"].is_array() && layer < static_cast<int>(maps["layers"].size()))
                                        ? maps["layers"][layer] : json::object();
            for (int kind = 0; kind < 3; ++kind) {
                const auto map = static_cast<TerrainLayerMap>(kind);
                const std::string path = wanted(layerEntry, kMapKeys[kind], Terrain::DefaultLayerMapPath(layer, map));
                if (path != terrain->GetLayerMapPath(layer, map) && !terrain->SetLayerMap(layer, map, path)) {
                    warnings.push_back(std::string("A terrain ") + kTerrainLayerNames[layer] + " " + kMapKeys[kind] + " image could not be loaded: " + path);
                }
            }
        }
        if (!terrain->ApplySettings(entry.value("settings", TerrainSettings{}))) {
            warnings.push_back("The terrain mesh could not be rebuilt with the saved settings.");
        }
        // Hand-sculpted heights (lesson 27): a 16-bit PNG beside the scene file. Only scene FILES carry this entry (undo
        // snapshots never do), and it is applied last, once the settings have given the grid its final size.
        if (entry.contains("sculpt") && entry["sculpt"].is_object() && entry["sculpt"].contains("heights") &&
            entry["sculpt"]["heights"].is_string()) {
            const std::string heightsFile = resolve(entry["sculpt"]["heights"].get<std::string>());
            std::string loadError;
            if (!terrain->LoadSculptedHeights(heightsFile, loadError)) {
                warnings.push_back("The sculpted terrain heights could not be loaded (" + loadError + "): " + heightsFile);
            }
        }
        // Painted layers (lesson 30): an RGBA8 PNG beside the scene file, one channel per layer. Applied last, after the
        // settings have given the grid its final size (the image must match it).
        if (entry.contains("paint") && entry["paint"].is_object() && entry["paint"].contains("weights") &&
            entry["paint"]["weights"].is_string()) {
            const std::string paintFile = resolve(entry["paint"]["weights"].get<std::string>());
            std::string loadError;
            if (!terrain->LoadPaint(paintFile, loadError)) {
                warnings.push_back("The painted terrain layers could not be loaded (" + loadError + "): " + paintFile);
            }
        }
    } else if (auto* water = dynamic_cast<Water*>(&object)) {
        if (entry.contains("name") && entry["name"].is_string()) water->SetName(entry["name"].get<std::string>());
        WaterSettings loaded = entry.value("settings", WaterSettings{});
        // Scenes saved before water had a separate length were square: the missing length is the width.
        if (entry.contains("settings") && entry["settings"].is_object() && !entry["settings"].contains("sizeZ")) loaded.sizeZ = loaded.size;
        loaded.debugView = water->GetSettings().debugView;   // debug switches are not part of a scene: undo and load must not flip them
        loaded.wireframe = water->GetSettings().wireframe;
        water->SetSettings(loaded);
    } else if (auto* sky = dynamic_cast<Skybox*>(&object)) {
        sky->SetIntensity(entry.value("intensity", 1.0f));
    } else if (auto* cube = dynamic_cast<Cube*>(&object)) {
        cube->GetSurface().Settings() = entry.value("surface", SurfaceSettings{});
    } else if (auto* model = dynamic_cast<ModelObject*>(&object)) {
        const json materials = entry.value("materials", json::array());
        for (size_t i = 0; i < materials.size() && i < model->GetMaterialCount(); ++i) {
            if (materials[i].value("name", "") != model->GetMaterialName(i)) {   // the model file changed since saving
                warnings.push_back("A material of \"" + model->GetSourcePath() + "\" no longer matches the saved one; its saved settings were skipped.");
                continue;
            }
            model->GetMaterial(i).Settings() = materials[i].value("surface", model->GetMaterial(i).Settings());
        }
        model->SetRenderSettings(entry.value("render", ModelRenderSettings{}));
    } else if (auto* scatter = dynamic_cast<ScatterObject*>(&object)) {
        ScatterSettings loaded = entry.value("settings", ScatterSettings{});
        for (ScatterSpeciesSettings& species : loaded.species) {
            if (!species.model.empty() && !IsBuiltinModel(species.model)) species.model = resolve(species.model);
        }
        scatter->SetSettings(loaded);
    }

    // Transform LAST: setting the transform of some objects (the cube, the water) changes other state.
    if (entry.contains("transform")) ApplyTransform(object, entry["transform"]);
    if (dynamic_cast<Cube*>(&object)) static_cast<Cube*>(&object)->SetSpinning(entry.value("spinning", false));
}

// Lights, shadow, post-processing and sky lighting: everything global that the Inspector edits (the camera is not
// part of it: flying around is not an edit).
json DescribeGlobals(const Scene& scene)
{
    json globals;
    const LightSystem& lights = scene.GetLightSystem();
    json points = json::array();
    for (const PointLight& light : lights.PointLights()) points.push_back(light);
    globals["lights"] = { { "ambient", lights.Ambient() }, { "directional", lights.Directional() }, { "points", points } };
    globals["shadow"] = scene.GetShadowMap().GetSettings();
    globals["post"] = scene.GetPostSettings();
    globals["environment"] = scene.GetEnvironment();
    return globals;
}

void ApplyGlobals(Scene& scene, const json& root, std::vector<std::string>& warnings)
{
    auto attempt = [&](const std::string& what, auto&& action) {
        try {
            action();
        } catch (const json::exception& e) {
            warnings.push_back(what + ": " + e.what());
        }
    };
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
}

}  // namespace

// The file a terrain's hand-made data is saved in, next to the scene file. `kind` says which data:
// "castle.scene.json" -> "castle.terrain.png" (sculpted heights) or "castle.paint.png" (painted layers). A second terrain
// would get "castle.terrain2.png".
static std::string SidecarFileName(const fs::path& sceneFile, const char* kind, int index)
{
    std::string stem = sceneFile.filename().string();
    for (const char* suffixText : { ".scene.json", ".json" }) {
        const std::string suffix = suffixText;
        if (stem.size() > suffix.size() && stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0) {
            stem.resize(stem.size() - suffix.size());
            break;
        }
    }
    return stem + "." + kind + (index > 0 ? std::to_string(index + 1) : std::string()) + ".png";
}

bool SaveScene(const Scene& scene, const std::string& path, std::vector<std::string>& warnings, std::string& error,
               const std::string& assetsRoot)
{
    const fs::path file = fs::absolute(fs::path(path));
    const fs::path folder = file.parent_path();
    int sculptedTerrains = 0;   // how many sculpted terrains have been written so far (names their PNG files)
    int paintedTerrains = 0;    // the same for painted ones

    json root;
    root["format"] = kFormatName;
    root["version"] = kSceneFileVersion;
    root["camera"] = { { "position", scene.GetCamera().GetPosition() }, { "target", scene.GetCamera().GetTarget() } };

    // A named local lives to the end of the function, so it outlives the loop.
    const json globals = DescribeGlobals(scene);
    for (const auto& [key, value] : globals.items()) root[key] = value;

    json objects = json::array();
    for (const auto& owned : scene.GetObjects()) {
        if (!owned) continue;
        json entry = DescribeObject(*owned, &folder, &warnings, assetsRoot);
        if (entry.is_null()) continue;
        // A sculpted terrain's heights go in a 16-bit PNG next to the scene file, and the entry points at it. If that
        // file cannot be written the whole save fails: a scene that silently forgets your sculpting is worse.
        if (const auto* terrain = dynamic_cast<const Terrain*>(owned.get()); terrain && terrain->IsSculpted()) {
            const HeightfieldLayout& layout = terrain->GetHeightfield().GetLayout();
            const std::string heightsName = SidecarFileName(file, "terrain", sculptedTerrains++);
            std::string pngError;
            if (!WriteGrayscale16Png((folder / heightsName).string(), layout.cellsX + 1, layout.cellsZ + 1,
                                     QuantizeHeights(terrain->GetHeightfield().Samples()), pngError)) {
                error = "Could not save the sculpted terrain heights: " + pngError;
                return false;
            }
            entry["sculpt"] = { { "heights", heightsName } };
        }
        // Painted layers (lesson 30): an RGBA8 PNG, one texel per grid cell, one channel per layer. Same rule: if it cannot
        // be written, the save fails.
        if (const auto* terrain = dynamic_cast<const Terrain*>(owned.get()); terrain && terrain->IsPainted()) {
            const PaintMap& paint = terrain->GetPaintMap();
            const std::string paintName = SidecarFileName(file, "paint", paintedTerrains++);
            std::string pngError;
            if (!WriteRgba8Png((folder / paintName).string(), paint.width, paint.height, QuantisePaint(paint), pngError)) {
                error = "Could not save the painted terrain layers: " + pngError;
                return false;
            }
            entry["paint"] = { { "weights", paintName } };
        }
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
    ApplyGlobals(scene, root, warnings);   // lights, shadow, post-processing, sky lighting

    for (const json& entry : root.value("objects", json::array())) {
        const std::string type = entry.value("type", "");
        attempt("object \"" + type + "\"", [&] {
            SceneObject* created = nullptr;
            if (type == "Terrain") created = scene.Add<Terrain>(device);
            else if (type == "Water") created = scene.Add<Water>(device);
            else if (type == "Skybox") created = scene.Add<Skybox>(device);
            else if (type == "Cube") created = scene.Add<Cube>(device);
            else if (type == "Scatter") created = scene.Add<ScatterObject>(device);
            else if (type == "Model") {
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
                created = model;
            } else {
                warnings.push_back("Skipped an object of unknown type: \"" + type + "\"");
                return;
            }
            ApplyObject(*created, entry, &sceneFolder, warnings);   // settings, images, materials, transform
        });
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------
// Snapshots for undo / redo: the same description a file uses, as compact JSON text. Object keys are sorted, so
// the same state always gives the same text, and "did anything change?" is just comparing two strings.
// ---------------------------------------------------------------------------------------------------------
std::string CaptureObjectState(const SceneObject& object)
{
    const json entry = DescribeObject(object, nullptr, nullptr, fs::path());
    return entry.is_null() ? std::string() : entry.dump();
}

bool ApplyObjectState(SceneObject& object, const std::string& state, std::vector<std::string>& warnings)
{
    try {
        ApplyObject(object, json::parse(state), nullptr, warnings);
        return true;
    } catch (const json::exception& e) {
        warnings.push_back(std::string("Could not restore an earlier state: ") + e.what());
        return false;
    }
}

std::string CaptureGlobalState(const Scene& scene)
{
    return DescribeGlobals(scene).dump();
}

bool ApplyGlobalState(Scene& scene, const std::string& state, std::vector<std::string>& warnings)
{
    try {
        ApplyGlobals(scene, json::parse(state), warnings);
        return true;
    } catch (const json::exception& e) {
        warnings.push_back(std::string("Could not restore earlier settings: ") + e.what());
        return false;
    }
}
