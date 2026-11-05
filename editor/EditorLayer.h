#pragma once
#include "Layer.h"
#include "ModelObject.h"
#include "RayPicking.h"
#include "SceneObject.h"
#include <string>
#include "Scene.h"
#include "SurfaceMaterial.h"
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
    enum class GizmoTool { Translate, Rotate, Scale };

    // --- the shell: dock space, menu, panels ---
    void DrawDockSpace();
    void BuildDefaultLayout(unsigned int dockSpaceId, float width, float height);
    void DrawMenuBar();
    void DrawViewportPanel(LayerContext& ctx);
    void DrawViewportToolbar();
    void DrawSelectionAndGizmo(LayerContext& ctx, float imageX, float imageY, float imageWidth, float imageHeight);
    void HandleShortcuts();
    void ProcessPendingActions(LayerContext& ctx);
    SceneObjectId PickObject(const Ray& ray) const;
    SceneObject* SelectedObject() const;
    void FrameSelected();
    void PlaceInFrontOfCamera(SceneObject* object, float distance);
    void Select(SceneObject* object);
    void DrawHierarchyPanel();
    void DrawInspectorPanel();

    // --- what the Inspector shows ---
    void DrawObjectInspector(SceneObject* object);
    void DrawCameraInspector();
    void DrawLightsInspector();
    void DrawShadowInspector();
    void DrawEnvironmentInspector();
    void DrawTerrainInspector(Terrain* terrain);
    void DrawWaterInspector(Water* water);
    void DrawSkyboxInspector(Skybox* skybox);
    void DrawSurfaceInspector(SurfaceMaterial& surface);
    void DrawModelInspector(ModelObject* model);
    void ImportModelWithDialog(LayerContext& ctx);
    void DrawTransformEditor(SceneObject* object);

    bool attached_ = false;
    HWND hwnd_ = nullptr;
    Scene* scene_ = nullptr;   // non-owning; refreshed from the LayerContext every frame
    SelectionKind selectionKind_ = SelectionKind::Object;
    SceneObjectId selectedId_ = 0;   // 0 = nothing; an id (not an index) survives adds and deletes
    TerrainSettings pendingTerrainSettings_;
    bool lastTerrainGenerationSucceeded_ = true;
    bool showShadowMap_ = false;

    // Panels
    bool showHierarchy_ = true;
    bool showInspector_ = true;
    bool showViewport_ = true;
    bool resetLayout_ = false;

    // Viewport tools and what the panels asked for this frame (carried out at the start of the NEXT frame,
    // so nothing is added or removed while a list is being drawn).
    GizmoTool gizmoTool_ = GizmoTool::Translate;
    bool gizmoLocal_ = false;
    bool viewportFocused_ = false;
    bool hierarchyFocused_ = false;
    bool addCubeRequested_ = false;
    bool duplicateRequested_ = false;
    bool deleteRequested_ = false;

    // File > Import Model: the request is noted by the menu and carried out at the start of the next frame.
    bool importRequested_ = false;
    bool showImportError_ = false;
    std::string importError_;

    // Viewport panel: the size we want the 3D view to have next frame, and whether the pointer is over it.
    unsigned int viewportWidth_ = 1280;
    unsigned int viewportHeight_ = 720;
    unsigned int appliedWidth_ = 0;
    unsigned int appliedHeight_ = 0;
    bool viewportHovered_ = false;
};
