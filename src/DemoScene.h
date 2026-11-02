#pragma once
#include "Scene.h"
#include <d3d11.h>

// Fills a Scene with the demo content: camera placement, lights, terrain, cube, water, skybox.
// Adding a second scene later (or loading one from a file) means writing another function
// like this one - Window and the renderer never change.
bool BuildDemoScene(Scene& scene, ID3D11Device* device);
