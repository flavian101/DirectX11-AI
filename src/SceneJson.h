#pragma once
// INTERNAL to the scene file code (SceneSerializer.cpp and its tests): how each settings type becomes JSON.
// Kept out of the public headers so the rest of the engine does not need nlohmann/json.
#include "EnvironmentLighting.h"
#include "Light.h"
#include "LightObject.h"
#include "PostSettings.h"
#include "ModelObject.h"
#include "ScatterObject.h"
#include "ShadowMap.h"
#include "Skybox.h"
#include "SurfaceMaterial.h"
#include "Terrain.h"
#include "TerrainWorld.h"
#include "Water.h"
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ---------------------------------------------------------------------------------------------------------
// How each type becomes JSON. nlohmann/json finds these by name (argument-dependent lookup), so a settings
// struct can be written with  json j = settings;  and read with  settings = j.get<TerrainSettings>();
// ---------------------------------------------------------------------------------------------------------
namespace DirectX {
inline void to_json(json& j, const XMFLOAT3& v) { j = json::array({ v.x, v.y, v.z }); }
inline void from_json(const json& j, XMFLOAT3& v) { v = XMFLOAT3(j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()); }
inline void to_json(json& j, const XMFLOAT4& v) { j = json::array({ v.x, v.y, v.z, v.w }); }
inline void from_json(const json& j, XMFLOAT4& v) { v = XMFLOAT4(j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>(), j.at(3).get<float>()); }
}

// Enums are stored by NAME, so reordering or adding an enumerator later cannot silently change what an old file
// means. (An unknown name falls back to the FIRST entry.)
NLOHMANN_JSON_SERIALIZE_ENUM(ToneMapper, { { ToneMapper::Clamp, "clamp" }, { ToneMapper::Reinhard, "reinhard" },
                                           { ToneMapper::ACES, "aces" }, { ToneMapper::Filmic, "filmic" } })
NLOHMANN_JSON_SERIALIZE_ENUM(LightRole, { { LightRole::None, "none" }, { LightRole::Sun, "sun" }, { LightRole::Moon, "moon" } })
NLOHMANN_JSON_SERIALIZE_ENUM(SkyMode, { { SkyMode::Cubemap, "cubemap" }, { SkyMode::Procedural, "procedural" } })
NLOHMANN_JSON_SERIALIZE_ENUM(LightKind, { { LightKind::Directional, "directional" }, { LightKind::Point, "point" } })
NLOHMANN_JSON_SERIALIZE_ENUM(ReflectionMode, { { ReflectionMode::Sky, "sky" }, { ReflectionMode::PlanarHalf, "planar_half" },
                                               { ReflectionMode::PlanarFull, "planar_full" }, { ReflectionMode::PlanarQuarter, "planar_quarter" } })

// "WITH_DEFAULT": a field missing from the file keeps the struct's default. That is what lets an OLD file load
// after a settings struct gains a new field. Debug-only fields (debug views, wireframe...) are deliberately not
// listed: they are not part of what a scene IS.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WorldGridSettings, chunksPerTile, metresPerCell)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FractalWorldSettings, seed, octaves, baseWavelength)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WorldBounds, bounded, minX, minZ, maxX, maxZ)   // (phase 1 of docs/ROADMAP.md)
// A river of the world: its settings, and the path as [x, z, halfWidth, level] per sample (the carve needs no more than that).
inline void to_json(json& j, const WorldRiver& river)
{
    json path = json::array();
    for (const RiverPath::Sample& s : river.path) path.push_back(json::array({ s.x, s.z, s.halfWidth, s.level }));
    j = { { "id", river.id }, { "level", river.carve.level }, { "depth", river.carve.depth }, { "bankWidth", river.carve.bankWidth },
          { "bankHeight", river.carve.bankHeight }, { "path", std::move(path) } };
}
inline void from_json(const json& j, WorldRiver& river)
{
    river = WorldRiver{};
    river.id = j.value("id", 0);
    river.carve.level = j.value("level", river.carve.level);
    river.carve.depth = j.value("depth", river.carve.depth);
    river.carve.bankWidth = j.value("bankWidth", river.carve.bankWidth);
    river.carve.bankHeight = j.value("bankHeight", river.carve.bankHeight);
    if (!j.contains("path") || !j["path"].is_array()) return;
    for (const json& p : j["path"]) {
        if (!p.is_array() || p.size() < 4) continue;
        RiverPath::Sample s;
        s.x = p[0].get<float>();
        s.z = p[1].get<float>();
        s.halfWidth = p[2].get<float>();
        s.level = p[3].get<float>();
        river.path.push_back(s);
    }
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PostSettings, srgbEncode, exposureEV, toneMapper)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ShadowSettings, enabled, resolution, depthBias, slopeBias, normalOffset, strength)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WindSettings, direction, speed)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FogSettings, enabled, color, followSky, brightness, visibility, startDistance, heightFalloff, baseHeight, sunGlow, skyBlend, maxOpacity)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TimeOfDaySettings, enabled, hour, animate, dayLengthMinutes, noonAzimuth, maxElevation, sunSize, moonSize, starBrightness, sunsetStrength, skyBrightness, skyUpdateSeconds)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WeatherSettings, wind, fog, timeOfDay)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EnvironmentSettings, enabled, diffuseIntensity, specularIntensity)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DirectionalLight, direction, color, intensity)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PointLight, position, color, intensity, range)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SurfaceSettings, baseColor, alphaCutoff, useNormalMap, normalStrength, flipGreen,
                                                roughnessScale, metallicScale, fallbackRoughness, fallbackMetallic, aoStrength,
                                                emissiveIntensity)
// (WaterSettings::debugView and ::wireframe are debug-only and deliberately not listed.)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(UnderwaterSettings, custom, shallowColor, deepColor, visibility, colorDepth, lightDepth, brightness, nightBrightness, sunGlow, startDistance, surfaceMirror)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WaterSettings, shoreWaves, swash, height, size, sizeZ, color, alpha, shallowColor, absorption, fadeDistance,
                                                foamDistance, foamStrength, foamScale, foamTrail, shoreShelter,
                                                directionalSpread, choppiness, waveCount, swell, swellAngle, swellLength, waveGroups, waveSeed, detailNormal, detailScale, detailDistance,
                                                reflectivity, reflectionMode, reflectionDistortion, refraction, scatter,
                                                glintStrength, glintSharpness, crestFoam, crestFoamThreshold,
                                                meshDetail, lodDistance, frustumCulling, underwater, foamIntensity, waveScale)   // (WaterSettings::debugView and ::wireframe are debug-only and deliberately not listed.)
// Culling and level of detail of a model (lesson 31), and the scatter that grows trees and rocks on the terrain.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ModelRenderSettings, frustumCulling, lodEnabled, lodBias, lodScreen, cullScreen)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FloatingSettings, enabled, draft, responsiveness, damping, tilt, hullMask, maskPart, maskHeight, maskInset)
// Variants of a scatter species (lesson 36). Scene files from before it have no "variants" key: the species' own "model" is then
// its one look, exactly as before.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ScatterMaterialOverride, slot, albedo, normal, orm, emissive, baseColor, roughness, flipGreen)
// The numbers shared with the placement are listed ONCE (here), for the base structs; the settings add only their own keys.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ScatterVariantBase, weight, layerBias, minSlope, maxSlope, scaleMul)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ScatterSpeciesRules, spacing, minDistance, density, layerDensity, minSlope, maxSlope,
                                                minHeight, maxHeight, clusterStrength, clusterScale, scaleMin, scaleMax,
                                                alignToNormal, tilt, sink)
inline void to_json(json& j, const ScatterVariantSettings& v)
{
    to_json(j, static_cast<const ScatterVariantBase&>(v));
    j["name"] = v.name; j["model"] = v.model; j["enabled"] = v.enabled; j["materials"] = v.materials;
}
inline void from_json(const json& j, ScatterVariantSettings& v)
{
    const ScatterVariantSettings d;
    from_json(j, static_cast<ScatterVariantBase&>(v));
    v.name = j.value("name", d.name); v.model = j.value("model", d.model); v.enabled = j.value("enabled", d.enabled);
    v.materials = j.value("materials", d.materials);
}
inline void to_json(json& j, const ScatterSpeciesSettings& s)
{
    to_json(j, static_cast<const ScatterSpeciesRules&>(s));
    j["name"] = s.name; j["enabled"] = s.enabled; j["seed"] = s.seed; j["lodScreen"] = s.lodScreen;
    j["cullScreen"] = s.cullScreen; j["castShadows"] = s.castShadows; j["shadowDistance"] = s.shadowDistance; j["variants"] = s.variants;
}
inline void from_json(const json& j, ScatterSpeciesSettings& s)
{
    const ScatterSpeciesSettings d;
    from_json(j, static_cast<ScatterSpeciesRules&>(s));
    s.name = j.value("name", d.name); s.enabled = j.value("enabled", d.enabled);
    s.seed = j.value("seed", d.seed); s.lodScreen = j.value("lodScreen", d.lodScreen); s.cullScreen = j.value("cullScreen", d.cullScreen);
    s.castShadows = j.value("castShadows", d.castShadows); s.shadowDistance = j.value("shadowDistance", d.shadowDistance);
    if (j.contains("variants") && j["variants"].is_array() && !j["variants"].empty()) {
        s.variants = j["variants"].get<std::vector<ScatterVariantSettings>>();
    } else {
        // A scene saved before lesson 36: a species had one "model" and no looks. The model becomes its one look, named like the species.
        ScatterVariantSettings only;
        only.name = s.name;
        only.model = j.value("model", std::string("builtin:pine"));
        s.variants = { only };
    }
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ScatterSettings, enabled, cellSize, seed, lodBias, frustumCulling, lodEnabled, shadows,
                                                reflectionDetail, generationBudgetMs, maxInstancesPerSpecies, species)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TerrainSettings, width, heightScale, maxCells, breakupSeed, uvTiling, groundTiling, rockTiling, sandTiling, snowTiling,
                                                rockDarkness, normalStrength, flipGreen, heightBlend, aoStrength,
                                                roughnessScale, groundTint, rockTint, slopeStart, slopeFade, shoreHeight, shoreWidth,
                                                snowHeight, snowFade, breakupScale, breakupAmount, shoreColor, snowColor, frustumCulling,
                                                lodEnabled, lodDistance0, lodDistance1)
// The shape of a world's ground (levels, curve, smoothing, detail noise): src/WorldShape.h.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(HeightShapeSettings, blackPoint, whitePoint, curve, invert, smoothing, bicubic, prefilter, detailStrength,
                                                detailWavelength, detailOctaves, detailSeed, detailRidged, detailValleyMask)
// Defined AFTER TerrainSettings: a world can carry the material of the terrain it was migrated from. `source` is stored as a number (0 fractal
// hills, 1 picture). A scene saved before the picture source, the shape or the material existed has none of the new keys and loads as the
// endless fractal world it always was. (The world's EDITS are not here: they are a file beside the scene, see SceneSerializer.cpp.)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TerrainWorldSettings, grid, source, fractal, picturePath, pictureWidth, pictureOffsetX, pictureOffsetZ, bounds,
                                                shape, heightScale, baseHeight, viewDistance, rivers)

