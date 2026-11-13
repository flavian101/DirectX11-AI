#pragma once
// INTERNAL to the scene file code (SceneSerializer.cpp and its tests): how each settings type becomes JSON.
// Kept out of the public headers so the rest of the engine does not need nlohmann/json.
#include "EnvironmentLighting.h"
#include "Light.h"
#include "PostSettings.h"
#include "ModelObject.h"
#include "ScatterObject.h"
#include "ShadowMap.h"
#include "SurfaceMaterial.h"
#include "Terrain.h"
#include "Water.h"
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
NLOHMANN_JSON_SERIALIZE_ENUM(WaterShape, { { WaterShape::Rectangle, "rectangle" }, { WaterShape::Ellipse, "ellipse" } })
NLOHMANN_JSON_SERIALIZE_ENUM(WaterBodyType, { { WaterBodyType::Ocean, "ocean" }, { WaterBodyType::Lake, "lake" },
                                              { WaterBodyType::River, "river" }, { WaterBodyType::Pond, "pond" },
                                              { WaterBodyType::Custom, "custom" } })
NLOHMANN_JSON_SERIALIZE_ENUM(WindSource, { { WindSource::Scene, "scene" }, { WindSource::Custom, "custom" } })
NLOHMANN_JSON_SERIALIZE_ENUM(ReflectionMode, { { ReflectionMode::Sky, "sky" }, { ReflectionMode::PlanarHalf, "planar_half" },
                                               { ReflectionMode::PlanarFull, "planar_full" }, { ReflectionMode::PlanarQuarter, "planar_quarter" } })

// "WITH_DEFAULT": a field missing from the file keeps the struct's default. That is what lets an OLD file load
// after a settings struct gains a new field. Debug-only fields (debug views, wireframe...) are deliberately not
// listed: they are not part of what a scene IS.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PostSettings, srgbEncode, exposureEV, toneMapper)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ShadowSettings, enabled, resolution, depthBias, slopeBias, normalOffset, strength)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WindSettings, direction, strength)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WeatherSettings, wind)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EnvironmentSettings, enabled, diffuseIntensity, specularIntensity)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DirectionalLight, direction, color, intensity)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PointLight, position, color, intensity, range)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SurfaceSettings, baseColor, alphaCutoff, useNormalMap, normalStrength, flipGreen,
                                                roughnessScale, metallicScale, fallbackRoughness, fallbackMetallic, aoStrength,
                                                emissiveIntensity)
// (WaterSettings::debugView and ::wireframe are debug-only and deliberately not listed.)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WaterSettings, bodyType, flowSpeed, flowAngle, shoreWaves, swash, height, size, sizeZ, shape, color, alpha, shallowColor, absorption, fadeDistance,
                                                foamDistance, foamStrength, waveAmplitude, waveSpeed, waveFrequency, windSource, windDirection, shoreShelter,
                                                directionalSpread, choppiness, waveCount, waveStyle, waveRoughness, swell, swellAngle, swellLength, waveGroups, waveSeed, detailNormal, detailScale, detailDistance,
                                                reflectivity, reflectionMode, reflectionDistortion, refraction, scatter,
                                                glintStrength, glintSharpness, crestFoam, crestFoamThreshold,
                                                meshDetail, lodDistance, frustumCulling)
// Culling and level of detail of a model (lesson 31), and the scatter that grows trees and rocks on the terrain.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ModelRenderSettings, frustumCulling, lodEnabled, lodBias, lodScreen, cullScreen)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FloatingSettings, enabled, draft, responsiveness, damping, tilt, hullMask, maskPart, maskHeight, maskInset)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ScatterSpeciesSettings, name, model, enabled, spacing, minDistance, density, layerDensity,
                                                minSlope, maxSlope, minHeight, maxHeight, clusterStrength, clusterScale, scaleMin,
                                                scaleMax, alignToNormal, tilt, sink, seed, lodScreen, cullScreen, castShadows,
                                                shadowDistance)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ScatterSettings, enabled, cellSize, seed, lodBias, frustumCulling, lodEnabled, shadows,
                                                reflectionDetail, generationBudgetMs, maxInstancesPerSpecies, species)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TerrainSettings, width, heightScale, maxCells, noiseStrength, noiseScale,
                                                noiseOctaves, noiseSeed, uvTiling, groundTiling, rockTiling, sandTiling, snowTiling,
                                                rockDarkness, normalStrength, flipGreen, heightBlend, aoStrength,
                                                roughnessScale, groundTint, rockTint, slopeStart, slopeFade, shoreHeight, shoreWidth,
                                                snowHeight, snowFade, breakupScale, breakupAmount, shoreColor, snowColor, frustumCulling,
                                                lodEnabled, lodDistance0, lodDistance1)

