#pragma once
// Two small models made in code, so scatter works the moment you add it, with no asset to download first (lesson 31):
// "builtin:pine" (a trunk and four tiers of needles, about 1900 triangles) and "builtin:rock" (a lumpy boulder, 1280).
// They are ordinary ImportedModels, so they take exactly the same path as a glTF file: materials, levels of detail,
// instancing. They are dense on purpose: the automatic level of detail has something to take away.
#include "ModelImporter.h"
#include <string>

constexpr const char* kBuiltinPine = "builtin:pine";
constexpr const char* kBuiltinRock = "builtin:rock";

inline bool IsBuiltinModel(const std::string& path) { return path.rfind("builtin:", 0) == 0; }

// False if `id` is not one of the built-in names. The model's base is at y = 0 and it is centred on x = z = 0.
bool MakeBuiltinModel(const std::string& id, ImportedModel& out);
