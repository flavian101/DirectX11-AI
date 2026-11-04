#pragma once
#include "Layer.h"
#include "Scene.h"
#include "Water.h"

// The editor: a Layer that hosts Dear ImGui (docking branch) around the engine. Nothing in the
// engine includes this file - the editor depends on the engine, never the other way round.
class EditorLayer : public Layer {
public:
    EditorLayer();
    ~EditorLayer() override;

    bool OnAttach(HWND hwnd, Direct3D11Renderer& renderer) override;
    void OnDetach() override;
    bool OnWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;
    void OnFrame(LayerContext& ctx) override;
    void OnRenderOverlay(ID3D11DeviceContext* ctx) override;

    // Pointer must be over the Viewport panel for the middle-mouse look to start.
    bool BlocksSceneMouse() const override { return !viewportHovered_; }
    bool BlocksSceneKeyboard() const override;

private:
    enum class SelectionKind { Object, Camera, Lights };

    // --- the shell: dock space, menu, panels ---
    void DrawDockSpace();
    void BuildDefaultLayout(unsigned int dockSpaceId, float width, float height);
    void DrawMenuBar();
    void DrawViewportPanel(LayerContext& ctx);
    void DrawHierarchyPanel();
    void DrawInspectorPanel();

    // --- what the Inspector shows ---
    void DrawObjectInspector(SceneObject* object);
    void DrawCameraInspector();
    void DrawLightsInspector();
    void DrawShadowInspector();
    void DrawTerrainInspector(Terrain* terrain);
    void DrawWaterInspector(Water* water);
    void DrawSkyboxInspector(Skybox* skybox);
    void DrawTransformEditor(SceneObject* object);

    bool attached_ = false;
    Scene* scene_ = nullptr;   // non-owning; refreshed from the LayerContext every frame
    SelectionKind selectionKind_ = SelectionKind::Object;
    int selectedObject_ = -1;
    TerrainSettings pendingTerrainSettings_;
    bool lastTerrainGenerationSucceeded_ = true;
    bool showShadowMap_ = false;

    // Panels
    bool showHierarchy_ = true;
    bool showInspector_ = true;
    bool showViewport_ = true;
    bool resetLayout_ = false;

    // Viewport panel: the size we want the 3D view to have next frame, and whether the pointer is over it.
    unsigned int viewportWidth_ = 1280;
    unsigned int viewportHeight_ = 720;
    unsigned int appliedWidth_ = 0;
    unsigned int appliedHeight_ = 0;
    bool viewportHovered_ = false;
};
