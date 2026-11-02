#pragma once
#include "Scene.h"

class ImGuiLayer {
public:
	explicit ImGuiLayer(Scene* scene);

	void Draw();

private:
	void DrawMainMenuBar();
	void DrawSceneWindow();
	void DrawLightsWindow();
	void DrawTerrainWindow();
	void DrawCameraWindow();
	void DrawTransformEditor(SceneObject* object);

	Scene* scene_ = nullptr;
	bool showSceneWindow_ = true;
	bool showLightsWindow_ = true;
	bool showTerrainWindow_ = true;
	bool showCameraWindow_ = true;
	int selectedObject_ = -1;
	TerrainSettings pendingTerrainSettings_;
	bool terrainSettingsInitialized_ = false;
	bool lastTerrainGenerationSucceeded_ = true;
};
