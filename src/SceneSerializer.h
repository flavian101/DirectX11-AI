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

constexpr int kSceneFileVersion = 1;

// Writes `scene` to `path` (JSON). `warnings` receives things worth telling the user that did not stop the
// save (for example a model that lives outside the scene's folder and will not travel with it).
bool SaveScene(const Scene& scene, const std::string& path, std::vector<std::string>& warnings, std::string& error);

// Replaces the CONTENTS of an already-initialised scene (Scene::Initialize) with what the file describes.
// A file that cannot be read or parsed fails BEFORE anything is changed. A single bad object (a model whose
// file is missing) is skipped and reported in `warnings`; the rest still loads.
bool LoadScene(Scene& scene, ID3D11Device* device, const std::string& path, std::vector<std::string>& warnings,
               std::string& error);
