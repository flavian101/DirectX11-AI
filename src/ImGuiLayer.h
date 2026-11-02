#pragma once
#include "Scene.h"
#include "Water.h"

class ImGuiLayer {
public:
	explicit ImGuiLayer(Scene* scene);

	void Draw();

private:
	enum class SelectionKind { Object, Camera, Lights };

	void DrawCollectionWindow();
	void DrawObjectInspector(SceneObject* object);
	void DrawCameraInspector();
	void DrawLightsInspector();
	void DrawTerrainInspector(Terrain* terrain);
	void DrawWaterInspector(Water* water);
	void DrawSkyboxInspector(Skybox* skybox);
	void DrawTransformEditor(SceneObject* object);

	Scene* scene_ = nullptr;
	SelectionKind selectionKind_ = SelectionKind::Object;
	int selectedObject_ = -1;
	TerrainSettings pendingTerrainSettings_;
	bool terrainSettingsInitialized_ = false;
	bool lastTerrainGenerationSucceeded_ = true;
	WaterSettings pendingWaterSettings_;
	bool waterSettingsInitialized_ = false;
};
