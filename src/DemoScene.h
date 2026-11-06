#pragma once
#include "Scene.h"
#include <d3d11.h>
#include <memory>

// Fills a Scene with the demo content: camera placement, lights, terrain, cube, water, skybox.
// Adding a second scene later (or loading one from a file) means writing another function
// like this one - Window and the renderer never change.
bool BuildDemoScene(Scene& scene, ID3D11Device* device);

// What a program starts with: the saved scene at kStartSceneFile if it exists and loads, otherwise the demo.
// The game and the editor both call this, so a scene saved in the editor is what the game shows.
// Returns null only if even the demo could not be built.
constexpr const char* kStartSceneFile = "assets/scenes/main.scene.json";
std::unique_ptr<Scene> BuildStartScene(ID3D11Device* device);
