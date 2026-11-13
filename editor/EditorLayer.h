#pragma once
#include "AssetBrowserPanel.h"
#include "ConsolePanel.h"
#include "GpuTimer.h"
#include "Layer.h"
#include "HeightfieldEdit.h"
#include "ModelObject.h"
#include "ScatterObject.h"
#include "Project.h"
#include "RayPicking.h"
#include "ScenesPanel.h"
#include "SceneObject.h"
#include "UndoHistory.h"
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
    enum class GizmoTool { Translate, Rotate, Scale };

    // --- the shell: dock space, menu, panels ---
    void DrawDockSpace();
    void BuildDefaultLayout(unsigned int dockSpaceId, float width, float height);
    void DrawMenuBar();
    void DrawStatusBar(float height);   // the strip along the bottom: frame rate, object count, selection, saved state
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
    void DrawSceneSettingsPanel();   // everything that belongs to the whole scene, in tabs: Environment, Lighting, Post, Weather, Camera

    // --- what the Inspector shows ---
    void DrawObjectInspector(SceneObject* object);
    void DrawCameraInspector();
    void DrawPostInspector();      // exposure, tone mapping, gamma
    void DrawWeatherInspector();   // the scene's wind (and, later, fog and rain)
    void DrawLightsInspector();
    void DrawShadowInspector();
    void DrawEnvironmentInspector();
    void DrawTerrainInspector(Terrain* terrain);
    void DrawSculptSection(Terrain* terrain);
    void DrawWaterInspector(Water* water);
    void DrawAddWaterItems();
    void DrawSkyboxInspector(Skybox* skybox);
    void DrawSkyboxPicker(Skybox* skybox);                                 // cubemap combo, Rescan, and the "not shipped" warning
    void RescanSkyboxes();                                                  // looks for skybox folders in the engine's and the project's assets
    void UseSkyboxFolder(LayerContext& ctx, const std::string& folder);    // the Assets panel's "Use as skybox": adds a skybox if there is none
    bool ChangeSkybox(Skybox* skybox, const std::string& folder);           // swaps the cubemap and records ONE undo step
    void DrawSurfaceInspector(SurfaceMaterial& surface);
    void DrawModelInspector(ModelObject* model);
    void DrawScatterInspector(ScatterObject* scatter);
    void ImportModelWithDialog(LayerContext& ctx);
    void ImportModelFromFile(LayerContext& ctx, const std::string& path);

    // --- projects and scenes ---
    enum class NavKind { None, OpenScene, OpenProject, NewScene };
    void RequestNavigation(NavKind kind, const std::string& path = std::string());
    void HandleNavigation(LayerContext& ctx);      // opens / replaces what is open, after asking about unsaved changes
    void OpenSceneFile(LayerContext& ctx, const std::string& path);
    void OpenProjectFile(LayerContext& ctx, const std::string& path);
    void NewProject(LayerContext& ctx);            // File > New Project: the open scene becomes its first scene
    void NewScene(LayerContext& ctx);
    bool SaveSceneTo(LayerContext& ctx, const std::string& path);
    void RegisterSceneInProject(const std::string& path);   // lists a scene saved inside the project, once
    void SaveProjectFile();                        // writes the project file, and tells the user what could not be done
    void RememberProject(const std::string& file);
    void LoadRecentProjects();
    void DrawProjectPanels();
    void DrawUnsavedPrompt();
    bool IsDirty() const { return history_.Revision() != savedRevision_; }
    std::string AssetsRootText() const;            // the folder that ships: the project's assets/, or "assets"
    std::filesystem::path SceneStartFolder() const;

    // --- undo / redo ---
    void TrackEdits(LayerContext& ctx);                 // notices finished edits and records them
    void ApplyHistoryRequests();                        // carries out Undo / Redo / jumps asked for last frame
    void RecordAdded(SceneObject* object, const std::string& name);   // an object was just put into the scene
    void RecordPropertyChange(SceneObjectId id, const std::string& name, const std::string& before, const std::string& after);
    void RecordGlobalChange(const std::string& before, const std::string& after);
    // --- terrain sculpting (lesson 27) ---
    void ToggleSculptMode();                                      // on: selects the terrain; off: lets go of it
    void FinishSculptStroke(Terrain* terrain);                    // the mouse button came up: one undo step
    void RecordSculptStroke(SceneObjectId terrainId, HeightfieldPatch patch);
    void DiscardSculpt(Terrain* terrain);                         // back to the heightmap and noise (undoable)
    // --- terrain painting (lesson 30): the same brush tool, aimed at the layers instead of the heights ---
    void RecordPaintStroke(SceneObjectId terrainId, HeightfieldPatch patch);
    void ClearPaint(Terrain* terrain);                            // back to the automatic rules (undoable)
    void DrawHistoryPanel();
    void HandleSceneFileRequests(LayerContext& ctx);
    void HandleTerrainFileRequest(LayerContext& ctx);
    void ShowMessage(const std::string& title, const std::string& text);
    void DrawTransformEditor(SceneObject* object);

    bool attached_ = false;
    HWND hwnd_ = nullptr;
    Scene* scene_ = nullptr;   // non-owning; refreshed from the LayerContext every frame
    bool showAllWaterSettings_ = false;   // show every water setting for the selected body, whatever its type
    GpuTimings gpuTimings_;    // a copy of the renderer's per-pass GPU times, refreshed every frame (shown in the Performance panels)
    SceneObjectId selectedId_ = 0;   // 0 = nothing; an id (not an index) survives adds and deletes
    TerrainSettings pendingTerrainSettings_;
    bool lastTerrainGenerationSucceeded_ = true;
    bool terrainKeepProportions_ = true;   // widening the terrain also raises its height scale, so it keeps its look (editor-only)
    bool showShadowMap_ = false;

    // Folders (in the engine's assets/ and the open project's) that hold all six skybox faces, found by RescanSkyboxes.
    struct SkyboxChoice {
        std::string folder;   // normalised, see Skybox::NormalizeFolder
        std::string label;    // what the combo shows: "Skybox  (engine)"
    };
    std::vector<SkyboxChoice> skyboxFolders_;
    std::string skyboxScanRoot_ = "?";   // the assets root the list was made for: another project = look again

    // Panels
    bool showHierarchy_ = true;
    bool showInspector_ = true;
    bool showSceneSettings_ = true;
    bool skyTabOpen_ = false;        // the Environment tab drew the sky controls last frame (see TrackEdits)
    bool showViewport_ = true;
    bool resetLayout_ = false;

    // Viewport tools and what the panels asked for this frame (carried out at the start of the NEXT frame,
    // so nothing is added or removed while a list is being drawn).
    GizmoTool gizmoTool_ = GizmoTool::Translate;
    bool gizmoLocal_ = false;
    bool sculptMode_ = false;       // left-drag on the terrain shapes it instead of selecting
    bool sculptStroking_ = false;   // a stroke is in progress (the button went down on the terrain)
    BrushSettings brush_;           // brush_.radius is in WORLD units here; Terrain::Sculpt converts it
    int brushTarget_ = 0;           // what the brush changes: 0 = the shape of the ground, 1 = its painted layers
    int brushLayer_ = 0;            // the layer being painted (0 grass, 1 rock, 2 sand, 3 snow)
    bool brushErase_ = false;       // painting: take paint away instead (Shift flips this while you drag)
    bool strokeIsPaint_ = false;    // the stroke in progress is a paint stroke (fixed when it began)
    bool viewportFocused_ = false;
    bool hierarchyFocused_ = false;
    bool addCubeRequested_ = false;
    bool addScatterRequested_ = false;   // add a scatter (trees and rocks on the terrain) at the start of the next frame
    int addWaterType_ = -1;           // >= 0: add a water body of this WaterBodyType at the start of the next frame
    bool duplicateRequested_ = false;
    bool deleteRequested_ = false;

    // File > Import Model: the request is noted by the menu and carried out at the start of the next frame.
    bool importRequested_ = false;
    // Scene files: File > Open / Save. Like the import, the dialogs run at the start of the next frame.
    bool saveSceneRequested_ = false;
    bool saveSceneAsRequested_ = false;
    std::string currentScenePath_;    // empty = not saved yet
    unsigned savedRevision_ = 0;      // history_.Revision() at the last save or load: different now = unsaved changes

    // The project (if one is open) and the panels around it.
    Project project_;
    std::vector<std::string> recentProjects_;
    ConsolePanel console_;
    AssetBrowserPanel assetBrowser_;
    ScenesPanel scenesPanel_;
    bool showAssets_ = true;
    bool showConsole_ = true;
    bool showScenes_ = true;
    std::string pendingSkyboxFolder_;   // a skybox folder chosen in the Assets panel (applied at the start of the next frame)
    bool newProjectRequested_ = false;
    std::string pendingImportPath_;   // an asset double-clicked in the Assets panel
    // Opening another scene or project replaces what is open, so it first asks about unsaved changes.
    struct Navigation {
        NavKind kind = NavKind::None;
        std::string path;             // "" = ask with a file dialog
        bool confirmed = false;       // the user said "discard" (or there was nothing to lose)
        bool waitingForSave = false;  // "save" was chosen: the save runs first, then this continues
    } nav_;
    bool unsavedPromptRequested_ = false;
    double lastSavedTime_ = -100.0;   // ImGui time of the last successful save, for the little "Saved" flash

    // Terrain > Texture files: which image the user asked to replace (the file dialog opens at the start of the next frame).
    struct TerrainFileRequest {
        int kind = 0;    // 0 none, 1 a layer map, 2 the heightmap
        int layer = 0;
        int map = 0;
    };
    TerrainFileRequest terrainFileRequest_;

    // Undo / redo. The history holds the commands; the two detectors turn "the state, once per frame" into finished
    // edits (one for the selected object, one for the global settings).
    UndoHistory history_;
    ChangeDetector objectDetector_;
    ChangeDetector globalDetector_;
    ChangeDetector skyDetector_;   // the sky, while it is edited from the Environment tab without being the selected object
    bool undoRequested_ = false;
    bool redoRequested_ = false;
    int historyJump_ = -1;       // >= 0: stand at this history position (clicked in the History panel)
    bool showHistory_ = false;

    // One pop-up for every message the user must read (import errors, scene warnings).
    bool showMessage_ = false;
    std::string messageTitle_;
    std::string message_;

    // Viewport panel: the size we want the 3D view to have next frame, and whether the pointer is over it.
    unsigned int viewportWidth_ = 1280;
    unsigned int viewportHeight_ = 720;
    unsigned int appliedWidth_ = 0;
    unsigned int appliedHeight_ = 0;
    bool viewportHovered_ = false;
};
