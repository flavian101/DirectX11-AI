#pragma once
// The version of the scene file, and the one rule about it (roadmap T3): the editor reads files of exactly this version and no other.
// There is no conversion of older files and no guessing about newer ones: a scene that is not this version is built again.
// Pure: no Direct3D, so the rule is tested on its own.
#include <string>

// 3: the island-scene rewrite (one ground, one kind of water). Bump it, and say why here, whenever a scene file's meaning changes;
// nothing migrates the older files, so the bump is the migration.
constexpr int kSceneFileVersion = 3;

// Empty when a file of `version` can be read; otherwise the one message to show. (A file with no version field has version 0.)
inline std::string SceneVersionProblem(int version)
{
	if (version == kSceneFileVersion) return {};
	if (version > kSceneFileVersion)
		return "This scene was saved by a newer version of the editor (file format " + std::to_string(version) + "; this one reads format " +
			   std::to_string(kSceneFileVersion) + "). Open it with that editor.";
	return "This scene is in an old file format (" + std::to_string(version) + "; this editor reads format " + std::to_string(kSceneFileVersion) +
		   "). Old scenes are not converted: build the scene again, or open it in the editor that wrote it.";
}
