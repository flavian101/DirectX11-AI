#pragma once
// Saving a scene to a text file and loading it back.
//
// The file stores REFERENCES and SETTINGS, not assets: an imported model is saved as the path of its file
// plus what you changed (position, materials), never its vertices. The scene file stays small and readable,
// and editing the model in another program is picked up the next time the scene loads. Shipping the game
// then means copying the scene file together with the files it points to.
#include "Scene.h"
#include <string>
#include <vector>

constexpr int kSceneFileVersion = 2;   // 2: lights are objects ("Light"), and the weather has fog; a version-1 file still loads

// Writes `scene` to `path` (JSON). `warnings` receives things worth telling the user that did not stop the
// save (for example a model that lives outside the scene's folder and will not travel with it).
// `assetsRoot` is the folder that ships with the game (a project's assets/ folder; "assets" next to the program otherwise).
bool SaveScene(const Scene& scene, const std::string& path, std::vector<std::string>& warnings, std::string& error,
               const std::string& assetsRoot = "assets");

// Replaces the CONTENTS of an already-initialised scene (Scene::Initialize) with what the file describes.
// A file that cannot be read or parsed fails BEFORE anything is changed. A single bad object (a model whose
// file is missing) is skipped and reported in `warnings`; the rest still loads.
bool LoadScene(Scene& scene, ID3D11Device* device, const std::string& path, std::vector<std::string>& warnings,
               std::string& error);

// ---------------------------------------------------------------------------------------------------------------
// Snapshots, for undo and redo. A snapshot is the state of ONE object (or of the global settings) as compact JSON
// text, in the same description a scene file uses. Two snapshots of the same thing are equal text exactly when
// nothing changed, so detecting an edit is just comparing strings.
// ---------------------------------------------------------------------------------------------------------------

// "" if this kind of object cannot be captured. A spinning demo cube leaves its transform out (it changes by itself).
std::string CaptureObjectState(const SceneObject& object);
// Puts a snapshot back on the object it was taken from. Images that already match are not loaded again.
bool ApplyObjectState(SceneObject& object, const std::string& state, std::vector<std::string>& warnings);

// Lights, shadow, post-processing and sky lighting. The camera is NOT part of it: flying around is not an edit.
std::string CaptureGlobalState(const Scene& scene);
bool ApplyGlobalState(Scene& scene, const std::string& state, std::vector<std::string>& warnings);
