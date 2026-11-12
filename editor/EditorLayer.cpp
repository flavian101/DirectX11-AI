#include "EditorLayer.h"
#include "EditorTheme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include "Cube.h"
#include "ModelImporter.h"
#include "ProceduralModels.h"
#include "SceneSerializer.h"
#include "Direct3D11Renderer.h"
#include "Logger.h"
#include "Skybox.h"
#include "imgui.h"
#include "ImGuizmo.h"   // after imgui.h: ImGuizmo's header uses ImGui types but does not include them
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "imgui_internal.h"   // DockBuilder*: building the default panel layout from code
#include <DirectXMath.h>
#include <commdlg.h>   // GetOpenFileNameW: the Windows "Open" dialog

#ifndef IMGUI_HAS_DOCK
#error "The editor needs Dear ImGui's DOCKING branch: cd extern/imgui && git checkout docking"
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace DirectX;

namespace {
// An undoable action made of two small functions. Everything an editor action needs (an id, a snapshot, the object
// itself) is captured by the lambdas, so one class covers every kind of command.
class LambdaCommand final : public UndoCommand {
public:
    LambdaCommand(std::string name, std::function<void()> undo, std::function<void()> redo)
        : name_(std::move(name)), undo_(std::move(undo)), redo_(std::move(redo)) {}
    void Undo() override { undo_(); }
    void Redo() override { redo_(); }
    std::string Name() const override { return name_; }
private:
    std::string name_;
    std::function<void()> undo_;
    std::function<void()> redo_;
};
}

namespace {
// The gizmo edits ONE 4x4 matrix; the objects store position, rotation and scale. These two functions convert
// between them using the SAME order every SceneObject uses to build its world matrix:
// scale, then rotate about X, then Y, then Z, then move.
XMFLOAT4X4 ComposeTransform(const XMFLOAT3& position, const XMFLOAT3& rotation, const XMFLOAT3& scale)
{
    XMFLOAT4X4 result;
    XMStoreFloat4x4(&result, XMMatrixScaling(scale.x, scale.y, scale.z) * XMMatrixRotationX(rotation.x) *
                             XMMatrixRotationY(rotation.y) * XMMatrixRotationZ(rotation.z) *
                             XMMatrixTranslation(position.x, position.y, position.z));
    return result;
}

void DecomposeTransform(const XMFLOAT4X4& m, XMFLOAT3& position, XMFLOAT3& rotation, XMFLOAT3& scale)
{
    position = XMFLOAT3(m._41, m._42, m._43);   // the last row holds the translation

    // Each of the first three rows is one axis of the object, stretched by the scale: its length IS the scale.
    const float rows[3][3] = { { m._11, m._12, m._13 }, { m._21, m._22, m._23 }, { m._31, m._32, m._33 } };
    float lengths[3];
    float r[3][3];
    for (int i = 0; i < 3; ++i) {
        lengths[i] = std::sqrt(rows[i][0] * rows[i][0] + rows[i][1] * rows[i][1] + rows[i][2] * rows[i][2]);
        const float inverse = lengths[i] > 1e-8f ? 1.0f / lengths[i] : 0.0f;
        for (int j = 0; j < 3; ++j) r[i][j] = rows[i][j] * inverse;   // divide the scale out: a pure rotation remains
    }
    scale = XMFLOAT3((std::max)(lengths[0], 0.01f), (std::max)(lengths[1], 0.01f), (std::max)(lengths[2], 0.01f));

    // For R = Rx(a) * Ry(b) * Rz(g), multiplying it out gives  r[0][2] = -sin(b),  r[1][2] = sin(a)cos(b),
    // r[2][2] = cos(a)cos(b),  r[0][0] = cos(b)cos(g),  r[0][1] = cos(b)sin(g). So the angles can be read back:
    const float sinB = -r[0][2];
    const float b = std::asin((std::max)(-1.0f, (std::min)(1.0f, sinB)));
    float a, g;
    if (std::fabs(std::cos(b)) > 1e-4f) {
        a = std::atan2(r[1][2], r[2][2]);
        g = std::atan2(r[0][1], r[0][0]);
    } else {
        // Gimbal lock: looking straight along the Y axis, X and Z rotations turn the same way, so only their
        // sum is defined. Put it all in X.
        g = 0.0f;
        a = std::atan2(r[1][0] * sinB, r[1][1]);
    }
    rotation = XMFLOAT3(a, b, g);
}

// Draws the 12 edges of a box (in an object's local space, placed by `world`) as lines over the Viewport image.
void DrawBoxOutline(ImDrawList* list, const XMFLOAT4X4& world, const XMFLOAT3& boxMin, const XMFLOAT3& boxMax,
                    const XMFLOAT4X4& view, const XMFLOAT4X4& projection, float x, float y, float width, float height, ImU32 color)
{
    const XMMATRIX transform = XMLoadFloat4x4(&world) * XMLoadFloat4x4(&view) * XMLoadFloat4x4(&projection);
    ImVec2 screen[8];
    bool visible[8];
    for (int i = 0; i < 8; ++i) {
        const XMVECTOR corner = XMVectorSet((i & 1) ? boxMax.x : boxMin.x, (i & 2) ? boxMax.y : boxMin.y,
                                            (i & 4) ? boxMax.z : boxMin.z, 1.0f);
        const XMVECTOR clip = XMVector4Transform(corner, transform);
        const float w = XMVectorGetW(clip);
        visible[i] = w > 0.01f;   // behind the camera: the divide below would flip it to the wrong side
        if (!visible[i]) continue;
        screen[i] = ImVec2(x + (XMVectorGetX(clip) / w * 0.5f + 0.5f) * width, y + (0.5f - XMVectorGetY(clip) / w * 0.5f) * height);
    }
    for (int i = 0; i < 8; ++i) {
        for (int bit = 1; bit <= 4; bit <<= 1) {
            if (i & bit) continue;   // each edge once: from the corner whose bit is 0 to its neighbour with the bit set
            if (visible[i] && visible[i | bit]) list->AddLine(screen[i], screen[i | bit], color, 1.5f);
        }
    }
}
}

using namespace DirectX;

namespace {
void DrawFloat3(const char* label, XMFLOAT3& value, float speed, float minValue, float maxValue)
{
    float values[3] = { value.x, value.y, value.z };
    if (ImGui::DragFloat3(label, values, speed, minValue, maxValue)) {
        value = { values[0], values[1], values[2] };
    }
}
}

EditorLayer::EditorLayer() = default;

EditorLayer::~EditorLayer() { OnDetach(); }

bool EditorLayer::OnAttach(HWND hwnd, Direct3D11Renderer& renderer)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;       // the whole point of the docking branch
    // Without this, pressing the left button on the Viewport image would DRAG THE WINDOW out of its dock.
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    ApplyEditorTheme();   // our colours, rounding and spacing on top of the dark base

    if (!ImGui_ImplWin32_Init(hwnd) || !ImGui_ImplDX11_Init(renderer.GetDevice(), renderer.GetContext())) {
        LOG_ERROR("Failed to initialize Dear ImGui");
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    hwnd_ = hwnd;
    attached_ = true;
    LoadRecentProjects();
    return true;
}

void EditorLayer::OnDetach()
{
    if (!attached_) return;   // safe to call twice (SetLayer, then the destructor)
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    attached_ = false;
}

bool EditorLayer::OnWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return attached_ && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
}

bool EditorLayer::BlocksSceneKeyboard() const
{
    return attached_ && (ImGui::GetIO().WantTextInput ||   // typing into a text field: WASD must not fly the camera
                          ImGui::GetIO().KeyCtrl);       // Ctrl+D (duplicate) contains a D
}

void EditorLayer::OnFrame(LayerContext& ctx)
{
    if (!attached_) return;
    scene_ = &ctx.scene;
    gpuTimings_ = ctx.renderer.GetGpuTimings();

    // Record whatever the user finished editing in the previous frame, BEFORE this frame's actions change anything.
    TrackEdits(ctx);
    ApplyHistoryRequests();

    // Import BEFORE the UI frame starts: the file dialog blocks and pumps messages, which must not happen
    // between NewFrame and Render.
    if (importRequested_) {
        importRequested_ = false;
        ImportModelWithDialog(ctx);
    }
    if (!pendingImportPath_.empty()) {   // a model double-clicked in the Assets panel
        const std::string path = pendingImportPath_;
        pendingImportPath_.clear();
        ImportModelFromFile(ctx, path);
    }
    if (newProjectRequested_) {
        newProjectRequested_ = false;
        NewProject(ctx);
    }
    HandleSceneFileRequests(ctx);   // File > Open / Save, also before the UI frame starts
    HandleTerrainFileRequest(ctx);  // Terrain > Texture files
    ProcessPendingActions(ctx);     // add / duplicate / delete asked for by the previous frame's UI

    // Apply the size the Viewport panel measured LAST frame. The panel only knows its size while
    // it is being laid out (below), which is too late to resize the textures for this frame.
    if (ctx.renderer.SetViewTargetSize(viewportWidth_, viewportHeight_) &&
        (appliedWidth_ != viewportWidth_ || appliedHeight_ != viewportHeight_)) {
        appliedWidth_ = viewportWidth_;
        appliedHeight_ = viewportHeight_;
        ctx.scene.GetCamera().SetWindowSize(static_cast<int>(appliedWidth_), static_cast<int>(appliedHeight_));
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();   // must follow NewFrame: the gizmo library resets its per-frame state here

    DrawDockSpace();
    DrawViewportPanel(ctx);
    DrawHierarchyPanel();
    DrawInspectorPanel();
    DrawHistoryPanel();
    DrawProjectPanels();   // Scenes, Assets, Console
    HandleShortcuts();

    if (showMessage_) {
        ImGui::OpenPopup(messageTitle_.c_str());
        showMessage_ = false;
    }
    if (ImGui::BeginPopupModal(messageTitle_.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
        ImGui::TextUnformatted(message_.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    DrawUnsavedPrompt();

    ImGui::Render();
}

void EditorLayer::OnRenderOverlay(ID3D11DeviceContext*)
{
    if (attached_) ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void EditorLayer::DrawDockSpace()
{
    // A borderless host window that covers the whole client area. It owns the menu bar and the
    // dock space; every panel docks INTO this space.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;
    ImGui::Begin("##EditorHost", nullptr, flags);
    ImGui::PopStyleVar(3);

    DrawMenuBar();

    const ImGuiID dockSpaceId = ImGui::GetID("EditorDockSpace");
    // No node yet = first run (imgui.ini would have restored one) or the user asked for a reset.
    if (resetLayout_ || ImGui::DockBuilderGetNode(dockSpaceId) == nullptr) {
        BuildDefaultLayout(dockSpaceId, viewport->WorkSize.x, viewport->WorkSize.y);
        resetLayout_ = false;
    }
    ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();
}

void EditorLayer::BuildDefaultLayout(unsigned int dockSpaceId, float width, float height)
{
    ImGui::DockBuilderRemoveNode(dockSpaceId);
    ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockSpaceId, ImVec2(width, height));

    // Carve the space: a slice off the left, a slice off the right, the rest is the middle.
    ImGuiID middle = dockSpaceId;
    ImGuiID left = ImGui::DockBuilderSplitNode(middle, ImGuiDir_Left, 0.20f, nullptr, &middle);
    const ImGuiID right = ImGui::DockBuilderSplitNode(middle, ImGuiDir_Right, 0.28f, nullptr, &middle);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(middle, ImGuiDir_Down, 0.27f, nullptr, &middle);
    const ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.35f, nullptr, &left);
    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Scenes", leftBottom);
    ImGui::DockBuilderDockWindow("Assets", bottom);
    ImGui::DockBuilderDockWindow("Console", bottom);   // a tab next to Assets
    ImGui::DockBuilderDockWindow("Viewport", middle);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderFinish(dockSpaceId);
}

void EditorLayer::DrawMenuBar()
{
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Project...")) newProjectRequested_ = true;
        ImGui::SetItemTooltip("Makes a project folder and turns the scene you are editing into its first scene.");
        if (ImGui::MenuItem("Open Project...")) RequestNavigation(NavKind::OpenProject);
        if (ImGui::BeginMenu("Recent Projects", !recentProjects_.empty())) {
            for (const std::string& recent : recentProjects_) {
                if (ImGui::MenuItem(std::filesystem::path(recent).stem().string().c_str())) RequestNavigation(NavKind::OpenProject, recent);
                ImGui::SetItemTooltip("%s", recent.c_str());
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("New Scene")) RequestNavigation(NavKind::NewScene);
        if (ImGui::MenuItem("Open Scene...")) RequestNavigation(NavKind::OpenScene);
        if (ImGui::MenuItem("Save Scene", "Ctrl+S")) saveSceneRequested_ = true;
        if (ImGui::MenuItem("Save Scene As...")) saveSceneAsRequested_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Import Model...")) importRequested_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) PostQuitMessage(0);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        const std::string undoLabel = history_.CanUndo() ? "Undo " + history_.UndoName() : std::string("Undo");
        const std::string redoLabel = history_.CanRedo() ? "Redo " + history_.RedoName() : std::string("Redo");
        if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, history_.CanUndo())) undoRequested_ = true;
        if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, history_.CanRedo())) redoRequested_ = true;
        ImGui::Separator();
        const SceneObject* selected = SelectedObject();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, selected && selected->CanClone())) duplicateRequested_ = true;
        if (ImGui::MenuItem("Delete", "Del", false, selected && selected->CanDelete())) deleteRequested_ = true;
        if (ImGui::MenuItem("Frame selected", "F", false, selected != nullptr)) FrameSelected();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Add")) {
        if (ImGui::MenuItem("Cube")) addCubeRequested_ = true;
        if (ImGui::BeginMenu("Water body")) {
            DrawAddWaterItems();
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Model from file...")) importRequested_ = true;
        if (ImGui::MenuItem("Scatter on terrain (trees, rocks)", nullptr, false, scene_ && scene_->GetTerrain())) addScatterRequested_ = true;
        ImGui::SetItemTooltip("Grows trees and rocks over the terrain by rules (which layer, how steep, how high),\ndrawn with instancing, frustum culling and levels of detail. Needs a terrain.");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy_);
        ImGui::MenuItem("Viewport", nullptr, &showViewport_);
        ImGui::MenuItem("Inspector", nullptr, &showInspector_);
        ImGui::MenuItem("History", nullptr, &showHistory_);
        ImGui::MenuItem("Scenes", nullptr, &showScenes_);
        ImGui::MenuItem("Assets", nullptr, &showAssets_);
        ImGui::MenuItem("Console", nullptr, &showConsole_);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset layout")) resetLayout_ = true;
        ImGui::EndMenu();
    }
    // Which project and scene are open, a star when there are unsaved changes, and a short confirmation after a save.
    const std::string name = currentScenePath_.empty() ? "unsaved scene" : std::filesystem::path(currentScenePath_).filename().string();
    if (project_.IsOpen()) ImGui::TextDisabled("  |  %s  /  %s%s", project_.name.c_str(), name.c_str(), IsDirty() ? " *" : "");
    else ImGui::TextDisabled("  |  %s%s", name.c_str(), IsDirty() ? " *" : "");
    if (ImGui::GetTime() - lastSavedTime_ < 2.0) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "Saved");
    }
    ImGui::EndMenuBar();
}

static constexpr float kBrushRadiusMin = 1.0f;
static constexpr float kBrushRadiusMax = 120.0f;

// A circle on the ground around (centerX, centerZ), drawn as 64 short lines that each follow the terrain's height, so
// it hugs hills instead of floating flat. It is an ImGui overlay, so it also shows through hills; a shader decal would not.
static void DrawBrushRing(ImDrawList* list, const Terrain& terrain, float centerX, float centerZ, float radius,
                          const XMFLOAT4X4& view, const XMFLOAT4X4& projection,
                          float x, float y, float width, float height, ImU32 color)
{
    constexpr int kSegments = 64;
    const XMMATRIX viewProjection = XMLoadFloat4x4(&view) * XMLoadFloat4x4(&projection);
    ImVec2 points[kSegments];
    bool usable[kSegments];
    for (int i = 0; i < kSegments; ++i) {
        const float angle = XM_2PI * static_cast<float>(i) / static_cast<float>(kSegments);
        const float worldX = centerX + std::cos(angle) * radius;
        const float worldZ = centerZ + std::sin(angle) * radius;
        float worldY = 0.0f;
        usable[i] = terrain.GetHeightAt(worldX, worldZ, worldY);   // false off the edge of the terrain
        if (!usable[i]) continue;
        const XMVECTOR clip = XMVector4Transform(XMVectorSet(worldX, worldY + 0.2f, worldZ, 1.0f), viewProjection);
        const float w = XMVectorGetW(clip);
        if (w <= 0.001f) { usable[i] = false; continue; }   // behind the camera
        points[i] = ImVec2(x + (XMVectorGetX(clip) / w * 0.5f + 0.5f) * width,
                           y + (1.0f - (XMVectorGetY(clip) / w * 0.5f + 0.5f)) * height);
    }
    for (int i = 0; i < kSegments; ++i) {
        const int next = (i + 1) % kSegments;
        if (usable[i] && usable[next]) list->AddLine(points[i], points[next], color, 2.0f);
    }
}

void EditorLayer::DrawViewportToolbar()
{
    ImGui::SetCursorPos(ImVec2(6.0f, 4.0f));
    auto toolButton = [&](const char* label, GizmoTool tool) {
        const bool active = gizmoTool_ == tool;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label)) gizmoTool_ = tool;
        if (active) ImGui::PopStyleColor();
        ImGui::SameLine();
    };
    toolButton("Move [1]", GizmoTool::Translate);
    toolButton("Rotate [2]", GizmoTool::Rotate);
    toolButton("Scale [3]", GizmoTool::Scale);
    if (ImGui::Button(gizmoLocal_ ? "Local [G]" : "World [G]")) gizmoLocal_ = !gizmoLocal_;
    ImGui::SameLine();
    if (scene_ && scene_->GetTerrain()) {
        // Read the mode ONCE, before the click. The click flips sculptMode_, and a push and its pop must agree on it.
        const bool sculptWasOn = sculptMode_;
        if (sculptWasOn) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button("Brush [4]")) ToggleSculptMode();
        if (sculptWasOn) ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    ImGui::TextDisabled("%s", sculptMode_ ? "Left-drag brushes  |  Shift inverts  |  [ ] radius  |  Esc leaves"
                                          : "Hold Ctrl to snap  |  F frame  |  Del delete  |  Ctrl+D duplicate");
}

void EditorLayer::DrawViewportPanel(LayerContext& ctx)
{
    viewportHovered_ = false;
    viewportFocused_ = false;
    if (!showViewport_) return;

    // The image should touch the panel's edges, so no padding inside this one window.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool open = ImGui::Begin("Viewport", &showViewport_,
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (open) {
        viewportFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        DrawViewportToolbar();   // a strip ABOVE the image, so nothing overlaps the picture and the hover test stays clean

        const ImVec2 available = ImGui::GetContentRegionAvail();
        viewportWidth_ = static_cast<unsigned int>((std::max)(1.0f, available.x));
        viewportHeight_ = static_cast<unsigned int>((std::max)(1.0f, available.y));

        if (ID3D11ShaderResourceView* image = ctx.renderer.GetViewTexture()) {
            const ImVec2 imageMin = ImGui::GetCursorScreenPos();
            ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(image)), available);
            viewportHovered_ = ImGui::IsItemHovered();
            DrawSelectionAndGizmo(ctx, imageMin.x, imageMin.y, available.x, available.y);

            char fps[32];
            std::snprintf(fps, sizeof(fps), "FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::GetWindowDrawList()->AddText(ImVec2(imageMin.x + 8.0f, imageMin.y + available.y - 22.0f),
                                                IM_COL32(255, 255, 255, 200), fps);
        }
    }
    ImGui::End();
}

void EditorLayer::DrawSelectionAndGizmo(LayerContext& ctx, float x, float y, float width, float height)
{
    const Camera& camera = ctx.scene.GetCamera();
    const XMFLOAT4X4 view = camera.GetViewMatrix();
    const XMFLOAT4X4 projection = camera.GetProjectionMatrix();

    // Sculpt mode needs the terrain: if it is gone (deleted, or a scene without one was opened) the mode simply ends.
    Terrain* sculptTerrain = sculptMode_ ? ctx.scene.GetTerrain() : nullptr;
    if (sculptMode_ && !sculptTerrain) sculptMode_ = false;
    if (sculptStroking_ && !sculptTerrain) FinishSculptStroke(ctx.scene.GetTerrain());   // mode left in mid-stroke

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();                       // draw into the Viewport window, on top of the image
    ImGuizmo::SetRect(x, y, width, height);        // the gizmo must know which part of the screen is the 3D view

    SceneObject* selected = (selectionKind_ == SelectionKind::Object) ? SelectedObject() : nullptr;
    if (selected) {
        XMFLOAT4X4 boundsWorld;
        XMFLOAT3 boundsMin, boundsMax;
        if (selected->GetOrientedBounds(boundsWorld, boundsMin, boundsMax)) {
            ImGui::GetWindowDrawList()->PushClipRect(ImVec2(x, y), ImVec2(x + width, y + height), true);
            DrawBoxOutline(ImGui::GetWindowDrawList(), boundsWorld, boundsMin, boundsMax, view, projection, x, y, width, height,
                           IM_COL32(255, 170, 40, 255));
            ImGui::GetWindowDrawList()->PopClipRect();
        }

        XMFLOAT3 position, rotation, scale;
        if (!sculptTerrain && selected->GetEditorTransform(position, rotation, scale) &&   // no gizmo while sculpting
            (gizmoTool_ != GizmoTool::Rotate || selected->SupportsRotation())) {
            XMFLOAT4X4 matrix = ComposeTransform(position, rotation, scale);
            const ImGuizmo::OPERATION operation = gizmoTool_ == GizmoTool::Translate ? ImGuizmo::TRANSLATE
                                                : gizmoTool_ == GizmoTool::Rotate    ? ImGuizmo::ROTATE
                                                                                     : ImGuizmo::SCALE;
            const ImGuizmo::MODE mode = (gizmoLocal_ || gizmoTool_ == GizmoTool::Scale) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
            // Holding Ctrl snaps: half a unit, 15 degrees, or a tenth of the scale.
            const float step = gizmoTool_ == GizmoTool::Translate ? 0.5f : gizmoTool_ == GizmoTool::Rotate ? 15.0f : 0.1f;
            const float snap[3] = { step, step, step };
            ImGuizmo::Manipulate(&view._11, &projection._11, operation, mode, &matrix._11, nullptr,
                                 ImGui::GetIO().KeyCtrl ? snap : nullptr);
            if (ImGuizmo::IsUsing()) {
                DecomposeTransform(matrix, position, rotation, scale);
                selected->SetEditorTransform(position, rotation, scale);
            }
        }
    }

    // Click = pick. Only when the click is not on the gizmo's handles, or grabbing an arrow would also select
    // whatever object is behind it.
    if (!sculptTerrain && viewportHovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
        const ImVec2 mouse = ImGui::GetMousePos();
        const float ndcX = ((mouse.x - x) / width) * 2.0f - 1.0f;
        const float ndcY = 1.0f - ((mouse.y - y) / height) * 2.0f;   // screen y grows downward, ndc y grows upward
        const SceneObjectId hit = PickObject(MakePickRay(view, projection, ndcX, ndcY));
        if (hit != 0) { selectionKind_ = SelectionKind::Object; selectedId_ = hit; }
        else selectedId_ = 0;
    }

    // Where is the ground under the mouse? The terrain answers from its kept height grid (lesson 25). This is also
    // the first thing the sculpt brushes will need: a point on the ground to work around.
    if (viewportHovered_ && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
        if (const Terrain* terrain = ctx.scene.GetTerrain()) {
            const ImVec2 mouse = ImGui::GetMousePos();
            const float ndcX = ((mouse.x - x) / width) * 2.0f - 1.0f;
            const float ndcY = 1.0f - ((mouse.y - y) / height) * 2.0f;
            float distance = 0.0f;
            XMFLOAT3 point;
            if (terrain->Raycast(MakePickRay(view, projection, ndcX, ndcY), distance, &point)) {
                char text[96];
                std::snprintf(text, sizeof(text), "Ground: %.1f, %.1f, %.1f", point.x, point.y, point.z);
                ImGui::GetWindowDrawList()->AddText(ImVec2(x + 8.0f, y + height - 40.0f), IM_COL32(255, 255, 255, 200), text);
            }
        }
    }

    // ---- Sculpting (lesson 27): press = begin a stroke, hold = brush each frame, release = one undo step ----
    if (sculptTerrain) {
        const ImGuiIO& io = ImGui::GetIO();
        const float ndcX = ((io.MousePos.x - x) / width) * 2.0f - 1.0f;
        const float ndcY = 1.0f - ((io.MousePos.y - y) / height) * 2.0f;
        float distance = 0.0f;
        XMFLOAT3 point(0.0f, 0.0f, 0.0f);
        const bool onGround = (viewportHovered_ || sculptStroking_) &&
                              sculptTerrain->Raycast(MakePickRay(view, projection, ndcX, ndcY), distance, &point);

        if (viewportHovered_ && onGround && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            strokeIsPaint_ = (brushTarget_ == 1);   // fixed for the whole stroke, even if the target is changed meanwhile
            if (strokeIsPaint_) {
                sculptTerrain->BeginPaintStroke();
            } else {
                sculptTerrain->BeginSculptStroke();
                float level = 0.0f;   // Flatten levels the ground towards the height where the stroke began
                if (sculptTerrain->SampleNormalizedHeight(point.x, point.z, level)) brush_.targetHeight = level;
            }
            sculptStroking_ = true;
        }
        if (sculptStroking_) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (onGround) {
                    // A long frame (a hitch, a breakpoint) must not make the ground lurch: cap the step.
                    const float seconds = (std::min)(io.DeltaTime, 0.05f);
                    if (strokeIsPaint_) {
                        PaintBrush paint;
                        paint.layer = brushLayer_;
                        paint.erase = (brushErase_ != io.KeyShift);   // Shift flips paint and erase
                        paint.radius = brush_.radius;
                        paint.strength = brush_.strength;
                        paint.falloff = brush_.falloff;
                        sculptTerrain->Paint(paint, point.x, point.z, seconds);
                    } else {
                        BrushSettings step = brush_;
                        if (io.KeyShift && step.mode == BrushMode::Raise) step.mode = BrushMode::Lower;
                        else if (io.KeyShift && step.mode == BrushMode::Lower) step.mode = BrushMode::Raise;
                        sculptTerrain->Sculpt(step, point.x, point.z, seconds);
                    }
                }
            } else {
                FinishSculptStroke(sculptTerrain);
            }
        }
        if (onGround) {
            ImGui::GetWindowDrawList()->PushClipRect(ImVec2(x, y), ImVec2(x + width, y + height), true);
            const bool paintBrush = sculptStroking_ ? strokeIsPaint_ : (brushTarget_ == 1);   // yellow = shape, blue = paint
            DrawBrushRing(ImGui::GetWindowDrawList(), *sculptTerrain, point.x, point.z, brush_.radius, view, projection,
                          x, y, width, height, paintBrush ? IM_COL32(90, 200, 255, 255) : IM_COL32(255, 220, 80, 255));
            ImGui::GetWindowDrawList()->PopClipRect();
        }
    }
}

SceneObjectId EditorLayer::PickObject(const Ray& ray) const
{
    SceneObjectId best = 0;
    float bestDistance = 3.402823466e+38f;
    for (const auto& object : scene_->GetObjects()) {
        XMFLOAT4X4 world;
        XMFLOAT3 boundsMin, boundsMax;
        if (!object || !object->GetOrientedBounds(world, boundsMin, boundsMax)) continue;   // terrain, water and sky cannot be clicked
        float distance = 0.0f;
        if (RayIntersectsBox(ray, world, boundsMin, boundsMax, distance) && distance < bestDistance) {
            bestDistance = distance;   // the NEAREST object under the cursor wins
            best = object->GetId();
        }
    }
    return best;
}

SceneObject* EditorLayer::SelectedObject() const
{
    return (scene_ && selectedId_ != 0) ? scene_->Find(selectedId_) : nullptr;
}

void EditorLayer::Select(SceneObject* object)
{
    selectionKind_ = SelectionKind::Object;
    selectedId_ = object ? object->GetId() : 0;
}

void EditorLayer::FrameSelected()
{
    SceneObject* object = SelectedObject();
    XMFLOAT3 center;
    float radius = 0.0f;
    if (!object || !object->GetBoundingSphere(center, radius)) return;
    Camera& camera = scene_->GetCamera();
    const XMFLOAT3 eye = camera.GetPosition(), target = camera.GetTarget();
    const XMVECTOR forward = XMVector3Normalize(XMVectorSubtract(XMLoadFloat3(&target), XMLoadFloat3(&eye)));
    // Back off far enough that the object's bounding sphere fits comfortably, looking the same way as before.
    const float distance = (std::max)(radius, 0.5f) * 2.5f;
    XMFLOAT3 newEye;
    XMStoreFloat3(&newEye, XMVectorSubtract(XMLoadFloat3(&center), XMVectorScale(forward, distance)));
    camera.SetPosition(newEye);
    camera.SetTarget(center);
}

void EditorLayer::PlaceInFrontOfCamera(SceneObject* object, float distance)
{
    XMFLOAT3 position, rotation, scale;
    if (!object || !object->GetEditorTransform(position, rotation, scale)) return;
    const Camera& camera = scene_->GetCamera();
    const XMFLOAT3 eye = camera.GetPosition(), target = camera.GetTarget();
    const XMVECTOR forward = XMVector3Normalize(XMVectorSubtract(XMLoadFloat3(&target), XMLoadFloat3(&eye)));
    XMStoreFloat3(&position, XMVectorAdd(XMLoadFloat3(&eye), XMVectorScale(forward, distance)));
    object->SetEditorTransform(position, rotation, scale);
}

void EditorLayer::ProcessPendingActions(LayerContext& ctx)
{
    if (addCubeRequested_) {
        addCubeRequested_ = false;
        if (SceneObject* cube = ctx.scene.CreateCube()) {
            PlaceInFrontOfCamera(cube, 12.0f);
            Select(cube);
            RecordAdded(cube, "Add cube");
        }
    }
    if (addScatterRequested_) {
        addScatterRequested_ = false;
        if (ctx.scene.GetTerrain()) {
            ScatterObject* scatter = ctx.scene.Add<ScatterObject>(ctx.renderer.GetDevice());
            scatter->SetSettings(ScatterSettings::Starter());
            Select(scatter);
            RecordAdded(scatter, "Add scatter");
        }
    }
    if (addWaterType_ >= 0) {
        const WaterBodyType type = static_cast<WaterBodyType>(addWaterType_);
        addWaterType_ = -1;
        if (Water* water = ctx.scene.CreateWater(type)) {
            // A pool a little way ahead of the camera, resting just above the ground there (or at the level of the existing
            // water, or just below the camera when there is neither).
            const Camera& camera = scene_->GetCamera();
            const XMFLOAT3 eye = camera.GetPosition(), target = camera.GetTarget();
            XMFLOAT3 ahead = { target.x - eye.x, 0.0f, target.z - eye.z };
            const float flat = std::sqrt(ahead.x * ahead.x + ahead.z * ahead.z);
            ahead = flat > 1e-4f ? XMFLOAT3(ahead.x / flat, 0.0f, ahead.z / flat) : XMFLOAT3(0.0f, 0.0f, 1.0f);
            XMFLOAT3 position = { eye.x + ahead.x * 30.0f, eye.y - 4.0f, eye.z + ahead.z * 30.0f };
            float groundY = 0.0f;
            if (const Terrain* terrain = scene_->GetTerrain(); terrain && terrain->GetHeightAt(position.x, position.z, groundY)) {
                position.y = groundY + 0.4f;
            } else if (const Water* other = scene_->GetWater(); other && other != water) {
                position.y = other->GetSettings().height;
            }
            XMFLOAT3 rotation, scale, unusedPosition;
            water->GetEditorTransform(unusedPosition, rotation, scale);
            water->SetEditorTransform(position, rotation, scale);
            Select(water);
            RecordAdded(water, "Add water body");
        }
    }
    if (duplicateRequested_) {
        duplicateRequested_ = false;
        if (SceneObject* copy = ctx.scene.Duplicate(selectedId_)) {
            Select(copy);
            RecordAdded(copy, std::string("Duplicate ") + copy->GetEditorName());
        }
    }
    if (deleteRequested_) {
        deleteRequested_ = false;
        const SceneObject* selected = SelectedObject();
        if (selected && selected->CanDelete()) {
            // Not destroyed: the object is taken OUT of the scene and kept in the history, so that undo can put the very
            // same object back (with its GPU resources, id and name) instead of rebuilding it.
            auto holder = std::make_shared<std::unique_ptr<SceneObject>>();
            auto position = std::make_shared<size_t>(0);
            const SceneObjectId id = selectedId_;
            const std::string name = selected->GetEditorName();
            *holder = ctx.scene.Detach(id, position.get());
            selectedId_ = 0;
            history_.Push(std::make_unique<LambdaCommand>(
                "Delete " + name,
                [this, holder, position]() { if (*holder) Select(scene_->Attach(std::move(*holder), *position)); },       // undo: put it back
                [this, holder, position, id]() { *holder = scene_->Detach(id, position.get()); if (selectedId_ == id) selectedId_ = 0; }));   // redo
            objectDetector_.Reset();
        }
    }
}

// ----------------------------------------------------------------------------------------------------------------
// Undo / redo.
//
// Two kinds of thing are recorded:
//   * PROPERTY edits (a slider, a colour, a gizmo drag): found by comparing snapshots, see TrackEdits.
//   * OBJECT lifetime (add, duplicate, import, delete): recorded by the code that does it.
// ----------------------------------------------------------------------------------------------------------------
void EditorLayer::TrackEdits(LayerContext& ctx)
{
    // "Interacting" = the user is holding a slider, dragging a gizmo or typing in a field. While they are, the
    // baseline stays frozen; the first idle frame afterwards reports ONE edit. This runs before NewFrame, so
    // IsAnyItemActive reflects the end of the previous frame.
    const bool interacting = ImGui::IsAnyItemActive() || ImGuizmo::IsUsing();

    std::string before;
    const std::string globals = CaptureGlobalState(ctx.scene);
    if (globalDetector_.Observe(0, globals, interacting, before)) RecordGlobalChange(before, globals);

    SceneObject* selected = (selectionKind_ == SelectionKind::Object) ? SelectedObject() : nullptr;
    const std::string state = selected ? CaptureObjectState(*selected) : std::string();
    if (selected && !state.empty()) {
        if (objectDetector_.Observe(selected->GetId(), state, interacting, before)) {
            RecordPropertyChange(selected->GetId(), std::string("Edit ") + selected->GetEditorName(), before, state);
        }
    } else {
        objectDetector_.Reset();
    }
}

void EditorLayer::RecordPropertyChange(SceneObjectId id, const std::string& name, const std::string& before, const std::string& after)
{
    auto restore = [this, id](const std::string& state) {
        std::vector<std::string> warnings;
        if (SceneObject* object = scene_->Find(id)) ApplyObjectState(*object, state, warnings);   // gone (deleted) = nothing to restore
        for (const std::string& warning : warnings) LOG_WARNING(warning);
    };
    history_.Push(std::make_unique<LambdaCommand>(name, [restore, before]() { restore(before); }, [restore, after]() { restore(after); }));
}

void EditorLayer::RecordGlobalChange(const std::string& before, const std::string& after)
{
    auto restore = [this](const std::string& state) {
        std::vector<std::string> warnings;
        ApplyGlobalState(*scene_, state, warnings);
        for (const std::string& warning : warnings) LOG_WARNING(warning);
    };
    history_.Push(std::make_unique<LambdaCommand>("Edit scene settings", [restore, before]() { restore(before); }, [restore, after]() { restore(after); }));
}

void EditorLayer::RecordAdded(SceneObject* object, const std::string& name)
{
    // The object is in the scene now. Undo takes it OUT (and keeps it alive in this holder); redo puts the same
    // object back with the same id.
    auto holder = std::make_shared<std::unique_ptr<SceneObject>>();
    auto position = std::make_shared<size_t>(0);
    const SceneObjectId id = object->GetId();
    history_.Push(std::make_unique<LambdaCommand>(
        name,
        [this, holder, position, id]() { *holder = scene_->Detach(id, position.get()); if (selectedId_ == id) selectedId_ = 0; },
        [this, holder, position]() { if (*holder) Select(scene_->Attach(std::move(*holder), *position)); }));
}

void EditorLayer::ApplyHistoryRequests()
{
    const bool wanted = undoRequested_ || redoRequested_ || historyJump_ >= 0;
    if (undoRequested_) history_.Undo();
    if (redoRequested_) history_.Redo();
    if (historyJump_ >= 0) history_.JumpTo(static_cast<size_t>(historyJump_));
    undoRequested_ = redoRequested_ = false;
    historyJump_ = -1;
    if (wanted) {
        // The scene was changed by us, not by the user's hand: make the restored state the new baseline.
        objectDetector_.Reset();
        globalDetector_.Reset();
    }
}

void EditorLayer::DrawHistoryPanel()
{
    if (!showHistory_) return;
    if (!ImGui::Begin("History", &showHistory_)) { ImGui::End(); return; }
    ImGui::TextDisabled("Click a line to go back (or forward) to that point.");
    // Entry i is "done" when i < Position(). The line marked with > is where you stand now.
    const size_t position = history_.Position();
    if (ImGui::Selectable(position == 0 ? "> (start)" : "  (start)", position == 0)) historyJump_ = 0;
    for (size_t i = 0; i < history_.Size(); ++i) {
        const bool current = (i + 1 == position);
        const std::string label = std::string(current ? "> " : "  ") + history_.At(i).Name() + "##history" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), current)) historyJump_ = static_cast<int>(i + 1);
    }
    ImGui::End();
}

void EditorLayer::HandleShortcuts()
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;   // typing a number into a slider: "1" must not switch tools
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) saveSceneRequested_ = true;
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (io.KeyShift) redoRequested_ = true;   // Ctrl+Shift+Z
        else undoRequested_ = true;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) redoRequested_ = true;

    const bool inViewport = viewportHovered_ || viewportFocused_;
    if (inViewport) {
        if (ImGui::IsKeyPressed(ImGuiKey_1, false)) gizmoTool_ = GizmoTool::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_2, false)) gizmoTool_ = GizmoTool::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_3, false)) gizmoTool_ = GizmoTool::Scale;
        if (ImGui::IsKeyPressed(ImGuiKey_4, false)) ToggleSculptMode();
        if (sculptMode_) {   // [ and ] resize the brush; holding the key repeats
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) brush_.radius = (std::max)(kBrushRadiusMin, brush_.radius / 1.15f);
            if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) brush_.radius = (std::min)(kBrushRadiusMax, brush_.radius * 1.15f);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_G, false)) gizmoLocal_ = !gizmoLocal_;
    }
    if (inViewport || hierarchyFocused_) {
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) FrameSelected();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            if (sculptMode_) sculptMode_ = false;   // the first Escape leaves sculpt mode, the next one deselects
            else selectedId_ = 0;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) deleteRequested_ = true;
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateRequested_ = true;
    }
}

// The four kinds of water, as menu items (Add menu and the Hierarchy's right-click menu share them).
void EditorLayer::DrawAddWaterItems()
{
    static const struct { const char* name; const char* hint; WaterBodyType type; } kTypes[] = {
        { "Lake", "Short wind chop, round by default", WaterBodyType::Lake },
        { "River", "A long thin current: everything on it flows one way", WaterBodyType::River },
        { "Pond", "Small and nearly still", WaterBodyType::Pond },
        { "Ocean", "Big swells rolling in with the wind", WaterBodyType::Ocean } };
    for (const auto& entry : kTypes) {
        if (ImGui::MenuItem(entry.name)) addWaterType_ = static_cast<int>(entry.type);
        ImGui::SetItemTooltip("%s", entry.hint);
    }
}

void EditorLayer::DrawHierarchyPanel()
{
    hierarchyFocused_ = false;
    if (!showHierarchy_) return;
    if (!ImGui::Begin("Hierarchy", &showHierarchy_)) { ImGui::End(); return; }
    hierarchyFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    if (ImGui::Selectable("Camera", selectionKind_ == SelectionKind::Camera)) selectionKind_ = SelectionKind::Camera;
    if (ImGui::Selectable("Lights", selectionKind_ == SelectionKind::Lights)) selectionKind_ = SelectionKind::Lights;
    ImGui::Separator();

    for (const auto& owned : scene_->GetObjects()) {
        SceneObject* object = owned.get();
        if (!object) continue;
        ImGui::PushID(static_cast<int>(object->GetId()));   // two objects can share a name; the id keeps their rows distinct
        if (ImGui::Selectable(object->GetEditorName(), selectionKind_ == SelectionKind::Object && selectedId_ == object->GetId())) {
            Select(object);
        }
        if (ImGui::BeginPopupContextItem()) {   // right-click on a row
            Select(object);
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, object->CanClone())) duplicateRequested_ = true;
            if (ImGui::MenuItem("Delete", "Del", false, object->CanDelete())) deleteRequested_ = true;
            if (ImGui::MenuItem("Frame", "F")) FrameSelected();
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    // Right-click on the empty part of the panel.
    if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Add cube")) addCubeRequested_ = true;
        if (ImGui::BeginMenu("Add water body")) {
            DrawAddWaterItems();
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Add model from file...")) importRequested_ = true;
        if (ImGui::MenuItem("Add scatter on terrain", nullptr, false, scene_ && scene_->GetTerrain())) addScatterRequested_ = true;
        ImGui::EndPopup();
    }
    ImGui::End();
}

void EditorLayer::DrawInspectorPanel()
{
    if (!showInspector_) return;
    if (!ImGui::Begin("Inspector", &showInspector_)) { ImGui::End(); return; }

    if (selectionKind_ == SelectionKind::Camera) {
        DrawCameraInspector();
    } else if (selectionKind_ == SelectionKind::Lights) {
        DrawLightsInspector();
    } else if (SceneObject* selected = SelectedObject()) {
        DrawObjectInspector(selected);
    } else {
        ImGui::TextDisabled("Click an object in the Viewport or the Hierarchy.");
    }
    ImGui::End();
}

void EditorLayer::DrawObjectInspector(SceneObject* object)
{
    if (!object) return;
    ImGui::Text("Object: %s", object->GetEditorName());
    ImGui::Separator();
    DrawTransformEditor(object);

    if (auto* terrain = dynamic_cast<Terrain*>(object)) DrawTerrainInspector(terrain);
    if (auto* water = dynamic_cast<Water*>(object)) DrawWaterInspector(water);
    if (auto* skybox = dynamic_cast<Skybox*>(object)) DrawSkyboxInspector(skybox);
    if (auto* cube = dynamic_cast<Cube*>(object)) {
        bool spinning = cube->IsSpinning();
        if (ImGui::Checkbox("Spin", &spinning)) cube->SetSpinning(spinning);
        DrawSurfaceInspector(cube->GetSurface());
    }
    if (auto* model = dynamic_cast<ModelObject*>(object)) DrawModelInspector(model);
    if (auto* scatter = dynamic_cast<ScatterObject*>(object)) DrawScatterInspector(scatter);
}

namespace {
// Shows the Open dialog and returns the chosen file as UTF-8 (empty = cancelled).
std::string PickModelFile(HWND owner)
{
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"3D models (*.gltf;*.glb;*.fbx;*.obj)\0*.gltf;*.glb;*.fbx;*.obj\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    // NOCHANGEDIR matters: without it the dialog moves the program's working directory to the folder you
    // browsed, and every relative path afterwards ("shaders/vs.hlsl", "assets/...") stops resolving.
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, file, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return {};
    std::string path(static_cast<size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, file, -1, path.data(), bytes, nullptr, nullptr);
    return path;
}
}

namespace {
// Open dialog for an image (a terrain texture or heightmap). Returns UTF-8, empty if cancelled.
std::string PickImageFile(HWND owner)
{
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.tga;*.bmp)\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrInitialDir = L"assets";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;   // NOCHANGEDIR: see PickModelFile
    if (!GetOpenFileNameW(&dialog)) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, file, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return {};
    std::string path(static_cast<size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, file, -1, path.data(), bytes, nullptr, nullptr);
    return path;
}

// Is the file inside the "assets" folder, the folder that is shipped with the game?
bool IsInsideAssets(const std::string& path, const std::string& assetsRoot)
{
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::weakly_canonical(path, error).lexically_relative(std::filesystem::weakly_canonical(assetsRoot, error));
    return !relative.empty() && *relative.begin() != "..";
}
}

void EditorLayer::HandleTerrainFileRequest(LayerContext& ctx)
{
    if (terrainFileRequest_.kind == 0) return;
    const TerrainFileRequest request = terrainFileRequest_;
    terrainFileRequest_ = TerrainFileRequest{};
    Terrain* terrain = ctx.scene.GetTerrain();
    if (!terrain) return;

    const std::string path = PickImageFile(hwnd_);
    if (path.empty()) return;   // cancelled

    bool ok = false;
    if (request.kind == 1) ok = terrain->SetLayerMap(request.layer, static_cast<TerrainLayerMap>(request.map), path);
    else if (request.kind == 2) ok = terrain->SetHeightmap(path);

    if (!ok) {
        ShowMessage("Could not use that image", "The image could not be read (or the terrain could not be rebuilt with it):\n" + path);
    } else if (!IsInsideAssets(path, AssetsRootText())) {
        ShowMessage("Image applied, with a warning", "That image is outside the assets folder, so it will not be shipped with the game.\nCopy it into assets/ (for example assets/Terrain/<Layer>/) and choose it again from there.");
    }
}

namespace {
// Open / Save dialog for scene files. Returns UTF-8, empty if cancelled. For saving, makes sure the name ends in
// ".scene.json" so the Open dialog's filter finds it again.
std::string PickSceneFile(HWND owner, bool save, const std::filesystem::path& startFolder)
{
    std::error_code error;
    std::filesystem::create_directories(startFolder, error);   // the dialog starts here
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"Scene files (*.scene.json)\0*.scene.json\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    const std::wstring startDirectory = std::filesystem::absolute(startFolder).wstring();   // native (wide) text: no conversion
    dialog.lpstrInitialDir = startDirectory.c_str();
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, file, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return {};
    std::string path(static_cast<size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, file, -1, path.data(), bytes, nullptr, nullptr);
    if (save) {
        auto endsWith = [&](const std::string& suffix) { return path.size() >= suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0; };
        if (!endsWith(".scene.json")) {
            if (endsWith(".json")) path.resize(path.size() - 5);
            path += ".scene.json";
        }
    }
    return path;
}

// Open / New dialog for a project file (*.d3dproj). Returns UTF-8, empty if cancelled. For a new project the file does
// not exist yet, so this is the Save dialog: its name becomes the project's name, and its folder the project folder.
std::string PickProjectFile(HWND owner, bool create)
{
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"Projects (*.d3dproj)\0*.d3dproj\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = create ? L"New project: choose a folder and a name" : L"Open project";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (create ? 0 : OFN_FILEMUSTEXIST);   // a project is never overwritten: CreateProject refuses
    if (!(create ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, file, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return {};
    std::string path(static_cast<size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, file, -1, path.data(), bytes, nullptr, nullptr);
    const std::string extension = kProjectExtension;
    if (path.size() < extension.size() || path.compare(path.size() - extension.size(), extension.size(), extension) != 0) path += extension;
    return path;
}

std::string JoinLines(const std::vector<std::string>& lines)
{
    std::string text;
    for (const std::string& line : lines) text += "- " + line + "\n\n";
    return text;
}
}

void EditorLayer::ShowMessage(const std::string& title, const std::string& text)
{
    messageTitle_ = title;
    message_ = text;
    showMessage_ = true;
}

std::string EditorLayer::AssetsRootText() const
{
    return project_.IsOpen() ? project_.AssetsRoot().string() : std::string("assets");
}

std::filesystem::path EditorLayer::SceneStartFolder() const
{
    return project_.IsOpen() ? project_.ScenesFolder() : std::filesystem::path("assets/scenes");
}

void EditorLayer::RequestNavigation(NavKind kind, const std::string& path)
{
    nav_ = Navigation{};
    nav_.kind = kind;
    nav_.path = path;
}

// Saves the scene and keeps everything that depends on it in step: the file name, the "unsaved" star, and the project's list.
bool EditorLayer::SaveSceneTo(LayerContext& ctx, const std::string& path)
{
    std::vector<std::string> warnings;
    std::string error;
    if (!SaveScene(ctx.scene, path, warnings, error, AssetsRootText())) {
        ShowMessage("Could not save the scene", error);
        return false;
    }
    currentScenePath_ = path;
    lastSavedTime_ = ImGui::GetTime();
    savedRevision_ = history_.Revision();
    RegisterSceneInProject(path);
    if (!warnings.empty()) ShowMessage("Scene saved, with warnings", JoinLines(warnings));
    return true;
}

void EditorLayer::RegisterSceneInProject(const std::string& path)
{
    if (!project_.IsOpen()) return;
    const std::string relative = project_.RelativeToRoot(path);
    if (!relative.empty() && project_.AddScene(relative)) SaveProjectFile();
}

void EditorLayer::SaveProjectFile()
{
    std::vector<std::string> warnings;
    std::string error;
    if (!SaveProject(project_, error, &warnings)) ShowMessage("Could not save the project", error);
    else if (!warnings.empty()) ShowMessage("Project saved, with warnings", JoinLines(warnings));
}

void EditorLayer::LoadRecentProjects()
{
    recentProjects_.clear();
    std::ifstream in("editor_recent.txt");
    std::string line;
    while (std::getline(in, line) && recentProjects_.size() < 8) {
        std::error_code error;
        if (!line.empty() && std::filesystem::exists(line, error)) recentProjects_.push_back(line);   // a project that was moved or deleted drops out
    }
}

void EditorLayer::RememberProject(const std::string& file)
{
    recentProjects_.erase(std::remove(recentProjects_.begin(), recentProjects_.end(), file), recentProjects_.end());
    recentProjects_.insert(recentProjects_.begin(), file);
    if (recentProjects_.size() > 8) recentProjects_.resize(8);
    std::ofstream out("editor_recent.txt", std::ios::trunc);
    for (const std::string& recent : recentProjects_) out << recent << '\n';
}

void EditorLayer::OpenSceneFile(LayerContext& ctx, const std::string& path)
{
    std::vector<std::string> warnings;
    std::string error;
    if (!LoadScene(ctx.scene, ctx.renderer.GetDevice(), path, warnings, error)) {
        ShowMessage("Could not open the scene", error);
        return;
    }
    history_.Clear();                      // every id in the history now refers to an object that is gone
    objectDetector_.Reset();
    globalDetector_.Reset();
    currentScenePath_ = path;
    savedRevision_ = history_.Revision();  // what was just loaded is, by definition, what is saved
    selectedId_ = 0;                       // the old objects are gone, and so are their ids
    selectionKind_ = SelectionKind::Object;
    RegisterSceneInProject(path);          // a scene opened from inside the project joins its list
    if (!warnings.empty()) ShowMessage("Scene opened, with warnings", JoinLines(warnings));
}

void EditorLayer::OpenProjectFile(LayerContext& ctx, const std::string& path)
{
    Project loaded;
    std::string error;
    if (!LoadProject(path, loaded, error)) {
        ShowMessage("Could not open the project", error);
        return;
    }
    project_ = std::move(loaded);
    RememberProject(project_.file.string());
    // Start where the game starts. A project with no scenes yet keeps what is on screen: it is the first scene to be.
    if (!project_.startScene.empty()) {
        const std::filesystem::path start = project_.Resolve(project_.startScene);
        std::error_code exists;
        if (std::filesystem::exists(start, exists)) OpenSceneFile(ctx, start.string());
        else ShowMessage("The start scene is missing", "The project opened, but its start scene was not found:\n" + start.string());
    }
}

void EditorLayer::NewProject(LayerContext& ctx)
{
    const std::string file = PickProjectFile(hwnd_, true);
    if (file.empty()) return;
    Project created;
    std::string error;
    if (!CreateProject(file, created, error)) {
        ShowMessage("Could not create the project", error);
        return;
    }
    project_ = std::move(created);
    RememberProject(project_.file.string());
    // The scene on screen becomes the project's first scene (and so its start scene): nothing is thrown away.
    SaveSceneTo(ctx, (project_.ScenesFolder() / "main.scene.json").string());
}

void EditorLayer::NewScene(LayerContext& ctx)
{
    ctx.scene.ClearObjects();
    ctx.scene.Add<Skybox>(ctx.renderer.GetDevice());   // an empty scene still needs a sky, or the background is black
    history_.Clear();
    objectDetector_.Reset();
    globalDetector_.Reset();
    currentScenePath_.clear();
    savedRevision_ = history_.Revision();
    selectedId_ = 0;
    selectionKind_ = SelectionKind::Object;
}

// Opening another scene or project replaces what is on screen. If there are unsaved changes the user decides first;
// the request waits in nav_ meanwhile (nothing else changes while the prompt is up).
void EditorLayer::HandleNavigation(LayerContext& ctx)
{
    if (nav_.kind == NavKind::None) return;
    if (nav_.waitingForSave) {
        nav_.waitingForSave = false;
        if (IsDirty()) { nav_ = Navigation{}; return; }   // the save was cancelled or failed: stay where we are
        nav_.confirmed = true;
    }
    if (!nav_.confirmed && IsDirty()) {
        unsavedPromptRequested_ = true;   // DrawUnsavedPrompt shows it; its buttons set `confirmed`, start a save, or cancel
        return;
    }

    const Navigation nav = nav_;
    nav_ = Navigation{};
    switch (nav.kind) {
    case NavKind::OpenScene: {
        const std::string path = nav.path.empty() ? PickSceneFile(hwnd_, false, SceneStartFolder()) : nav.path;
        if (!path.empty()) OpenSceneFile(ctx, path);
        break;
    }
    case NavKind::OpenProject: {
        const std::string path = nav.path.empty() ? PickProjectFile(hwnd_, false) : nav.path;
        if (!path.empty()) OpenProjectFile(ctx, path);
        break;
    }
    case NavKind::NewScene:
        NewScene(ctx);
        break;
    case NavKind::None:
        break;
    }
}

void EditorLayer::HandleSceneFileRequests(LayerContext& ctx)
{
    if (saveSceneRequested_ || saveSceneAsRequested_) {
        const bool askForName = saveSceneAsRequested_ || currentScenePath_.empty();
        saveSceneRequested_ = false;
        saveSceneAsRequested_ = false;
        const std::string path = askForName ? PickSceneFile(hwnd_, true, SceneStartFolder()) : currentScenePath_;
        if (!path.empty()) SaveSceneTo(ctx, path);
    }
    HandleNavigation(ctx);   // after the save, so "Save" in the unsaved-changes prompt is seen as done
}

void EditorLayer::DrawUnsavedPrompt()
{
    if (unsavedPromptRequested_) {
        ImGui::OpenPopup("Unsaved changes");
        unsavedPromptRequested_ = false;
    }
    if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    const std::string name = currentScenePath_.empty() ? "the open scene" : std::filesystem::path(currentScenePath_).filename().string();
    ImGui::Text("%s has changes that are not saved.", name.c_str());
    ImGui::TextDisabled("Opening something else would lose them.");
    ImGui::Spacing();
    if (ImGui::Button("Save")) {
        saveSceneRequested_ = true;       // carried out at the start of the next frame, then the navigation continues
        nav_.waitingForSave = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard changes")) {
        nav_.confirmed = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        nav_ = Navigation{};
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorLayer::DrawProjectPanels()
{
    // Scenes: the project's list. It only reports; what it asked for is carried out here, right away, because nothing
    // below this point is walking a list (the file dialogs and scene replacement are deferred by RequestNavigation).
    ScenesState state;
    state.currentScene = project_.RelativeToRoot(currentScenePath_);
    state.dirty = IsDirty();
    state.canAddCurrent = project_.IsOpen() && !state.currentScene.empty() && !project_.HasScene(state.currentScene);
    const ScenesAction scenesAction = scenesPanel_.Draw(&showScenes_, project_, state);
    switch (scenesAction.kind) {
    case ScenesAction::Kind::Open:       RequestNavigation(NavKind::OpenScene, project_.Resolve(scenesAction.scene).string()); break;
    case ScenesAction::Kind::SetStart:   if (project_.SetStartScene(scenesAction.scene)) SaveProjectFile(); break;
    case ScenesAction::Kind::Remove:     if (project_.RemoveScene(scenesAction.scene)) SaveProjectFile(); break;
    case ScenesAction::Kind::AddCurrent: RegisterSceneInProject(currentScenePath_); break;
    case ScenesAction::Kind::NewScene:   RequestNavigation(NavKind::NewScene); break;
    case ScenesAction::Kind::None:       break;
    }

    // Assets: a double-clicked scene opens, a double-clicked model is imported (both at the start of the next frame).
    const AssetAction assetAction = assetBrowser_.Draw(&showAssets_, project_.IsOpen() ? project_.AssetsRoot() : std::filesystem::path("assets"));
    if (assetAction.kind == AssetAction::Kind::OpenScene) RequestNavigation(NavKind::OpenScene, assetAction.path);
    else if (assetAction.kind == AssetAction::Kind::ImportModel) pendingImportPath_ = assetAction.path;

    console_.Draw(&showConsole_);
}

void EditorLayer::ImportModelWithDialog(LayerContext& ctx)
{
    const std::string path = PickModelFile(hwnd_);
    if (path.empty()) return;
    ImportModelFromFile(ctx, path);
}

void EditorLayer::ImportModelFromFile(LayerContext& ctx, const std::string& path)
{

    // Reading and uploading happens right here, on this thread, so a big model freezes the editor for a
    // moment. (Loading in the background is a later improvement.)
    ImportedModel imported;
    std::string error;
    if (!ImportModel(path, imported, error)) {
        ShowMessage("Import failed", error);
        return;
    }
    ModelObject* model = ctx.scene.Add<ModelObject>(ctx.renderer.GetDevice(), std::move(imported));
    if (!model->IsValid()) {
        ctx.scene.Remove(model->GetId());
        ShowMessage("Import failed", "The model was read, but none of its parts could be uploaded to the GPU.");
        return;
    }

    // Files come in any unit and any position: make the longest side 10 units and put the model's base in
    // front of the camera, so it is visible straight away.
    model->FitToSize(10.0f);
    PlaceInFrontOfCamera(model, 20.0f);
    Select(model);
    RecordAdded(model, std::string("Import ") + model->GetEditorName());
}

void EditorLayer::DrawModelInspector(ModelObject* model)
{
    ImGui::SeparatorText("Model");
    ImGui::TextDisabled("%s", model->GetSourcePath().c_str());
    ImGui::Text("%zu parts, %zu materials, %u triangles", model->GetPartCount(), model->GetMaterialCount(), model->GetTriangleCount());
    if (ImGui::Button("Fit to 10 units")) model->FitToSize(10.0f);

    // Culling and level of detail (lesson 31).
    ImGui::SeparatorText("Culling and detail");
    ModelRenderSettings render = model->GetRenderSettings();
    bool renderChanged = false;
    renderChanged |= ImGui::Checkbox("Frustum culling", &render.frustumCulling);
    ImGui::SetItemTooltip("Parts the camera (or the sun) cannot see are not sent to the GPU at all.\nTurn it off to see how many triangles that saves.");
    renderChanged |= ImGui::Checkbox("Level of detail", &render.lodEnabled);
    ImGui::SetItemTooltip("Parts that look small are drawn with fewer triangles.\nLevels come from the file (meshes named Pine_LOD0, Pine_LOD1...) or are generated when the model loads.");
    if (render.lodEnabled) {
        renderChanged |= ImGui::SliderFloat("Detail bias", &render.lodBias, ModelRenderSettings::kBiasMin, ModelRenderSettings::kBiasMax, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("Above 1: stay detailed for longer. Below 1: simplify sooner.");
        renderChanged |= ImGui::SliderFloat("Level 1 below", &render.lodScreen[0], 0.003f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("Fractions of the screen's height that the part covers. Level 1 starts when it covers less than this,\nlevel 2 and 3 below the next two. A part half the screen tall is 0.5.");
        renderChanged |= ImGui::SliderFloat("Level 2 below", &render.lodScreen[1], 0.002f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        renderChanged |= ImGui::SliderFloat("Level 3 below", &render.lodScreen[2], 0.001f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        renderChanged |= ImGui::SliderFloat("Hide below", &render.cullScreen, 0.0f, 0.05f, "%.4f");
        ImGui::SetItemTooltip("A part covering less than this fraction of the screen is not drawn at all. 0 = always drawn.");
    }
    if (renderChanged) model->SetRenderSettings(render);
    const ModelViewStats& camera = model->GetViewStats(ViewKind::Main);
    ImGui::TextDisabled("%s: %d levels", model->HasAuthoredLods() ? "From the file" : "Generated", model->GetLodLevelCount());
    ImGui::TextDisabled("Camera drew %d of %d parts, %d triangles", camera.partsDrawn, camera.partsTotal, camera.trianglesDrawn);
    if (model->GetLodLevelCount() > 1) {
        ImGui::TextDisabled("Parts per level: %d / %d / %d / %d", camera.partsPerLod[0], camera.partsPerLod[1], camera.partsPerLod[2], camera.partsPerLod[3]);
    }

    // Floating on water, and hiding the water inside the hull (lesson 33).
    ImGui::SeparatorText("Floating");
    FloatingSettings floating = model->GetFloatingSettings();
    bool floatChanged = false;
    floatChanged |= ImGui::Checkbox("Float on water", &floating.enabled);
    ImGui::SetItemTooltip("The model rides the highest water body under it: it rises, falls and leans with the waves.\nIts height is then set by the water and the draft, not by the gizmo.");
    if (floating.enabled) {
        floatChanged |= ImGui::SliderFloat("Draft", &floating.draft, 0.0f, 0.9f, "%.2f of height");
        ImGui::SetItemTooltip("How deep the bottom of the model sits below the surface, as a fraction of its height.\nNow %.2f world units.", floating.draft * model->GetModelHeight());
        floatChanged |= ImGui::SliderFloat("Responsiveness", &floating.responsiveness, 0.3f, 20.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("How stiffly it follows the water. Low = a heavy, lazy boat; high = it sticks to the surface.");
        floatChanged |= ImGui::SliderFloat("Damping", &floating.damping, 0.1f, 1.5f, "%.2f");
        ImGui::SetItemTooltip("1 = settles without bouncing. Lower = it overshoots and rocks a little before settling.");
        floatChanged |= ImGui::SliderFloat("Lean", &floating.tilt, 0.0f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("0 = stays level; 1 = pitches and rolls fully with the waves.");
    }
    floatChanged |= ImGui::Checkbox("Hide water inside hull", &floating.hullMask);
    ImGui::SetItemTooltip("Writes a lid over the inside of the hull into the depth buffer before the water is drawn,\nso the waves do not show inside the boat. Needs an open hull with a visible rim.");
    if (floating.hullMask) {
        const int autoPart = model->GetAutoHullPart();
        const int shown = floating.maskPart >= 0 ? floating.maskPart : autoPart;
        const std::string preview = shown >= 0 ? model->GetPartName(static_cast<size_t>(shown)) + (floating.maskPart < 0 ? " (auto)" : "") : "none found";
        if (ImGui::BeginCombo("Hull part", preview.c_str())) {
            if (ImGui::Selectable("Auto (biggest outline)", floating.maskPart < 0)) { floating.maskPart = -1; floatChanged = true; }
            for (size_t i = 0; i < model->GetPartCount(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                const std::string label = std::to_string(i) + ": " + model->GetPartName(i);
                if (ImGui::Selectable(label.c_str(), floating.maskPart == static_cast<int>(i))) { floating.maskPart = static_cast<int>(i); floatChanged = true; }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("The part whose outline (seen from above) is the hull. Oars and masts have thin outlines, so\nAuto normally finds the hull; pick it by hand if a rail or a deck is chosen instead.");
        bool autoHeight = floating.maskHeight < 0.0f;
        if (ImGui::Checkbox("Auto lid height", &autoHeight)) { floating.maskHeight = autoHeight ? -1.0f : 0.8f; floatChanged = true; }
        if (!autoHeight) {
            floatChanged |= ImGui::SliderFloat("Lid height", &floating.maskHeight, 0.0f, 1.5f, "%.2f of height");
            ImGui::SetItemTooltip("The lid must sit ABOVE the water inside the hull and NOT above the rim:\nhigher than the rim and it hides water behind the boat too.");
        }
        floatChanged |= ImGui::SliderFloat("Lid inset", &floating.maskInset, 0.0f, 0.4f, "%.3f of width");
        ImGui::SetItemTooltip("How far the lid is kept inside the hull's outline. Too small: the lid pokes through the walls\nand cuts a hole in the water beside the boat. Too large: a ring of water shows inside the rim.");
    }
    if (floatChanged) model->SetFloatingSettings(floating);

    for (size_t i = 0; i < model->GetMaterialCount(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::TreeNode(model->GetMaterialName(i).c_str())) {
            DrawSurfaceInspector(model->GetMaterial(i));
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void EditorLayer::DrawScatterInspector(ScatterObject* scatter)
{
    // Like water, every value here applies live: read the settings, edit, write back if anything moved. What actually has to
    // be done (reload a model, place the cells again, nothing) is worked out by the scatter.
    ScatterSettings s = scatter->GetSettings();
    bool changed = false;
    ImGui::SeparatorText("Scatter");
    if (!scatter->HasGround()) ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "There is no terrain to scatter on.");
    changed |= ImGui::Checkbox("Enabled##scatter", &s.enabled);
    if (scatter->GetPendingCells() > 0) ImGui::TextDisabled("Placing: %d cells left (nearest first)", scatter->GetPendingCells());

    if (ImGui::CollapsingHeader("Placement")) {
        changed |= ImGui::InputInt("Seed", &s.seed);
        ImGui::SetItemTooltip("A different seed is a different forest.\nThe same seed always gives the same forest, on any machine.");
        changed |= ImGui::SliderFloat("Cell size (m)", &s.cellSize, ScatterSettings::kCellMin, ScatterSettings::kCellMax, "%.0f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("The terrain is cut into square cells. A cell is culled, simplified and regenerated as one block.\nSmall: finer culling and quicker edits, but more draw calls.\nLarge: fewer draw calls, coarser culling.");
        changed |= ImGui::SliderFloat("Placement budget (ms/frame)", &s.generationBudgetMs, 0.5f, 30.0f, "%.1f");
        ImGui::SetItemTooltip("How long each frame may spend placing cells after an edit. Higher: the forest appears sooner, the editor stutters more.");
        changed |= ImGui::SliderInt("Max instances per species", &s.maxInstancesPerSpecies, 1000, 5000000, "%d", ImGuiSliderFlags_Logarithmic);
    }
    if (ImGui::CollapsingHeader("Culling and detail", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::Checkbox("Frustum culling", &s.frustumCulling);
        ImGui::SetItemTooltip("Cells outside the view are skipped. Turn it off to see how much that saves.");
        changed |= ImGui::Checkbox("Level of detail", &s.lodEnabled);
        changed |= ImGui::SliderFloat("Detail bias", &s.lodBias, ModelRenderSettings::kBiasMin, ModelRenderSettings::kBiasMax, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("Above 1: stay detailed for longer. Below 1: simplify sooner and hide small things earlier.");
        changed |= ImGui::Checkbox("Cast shadows", &s.shadows);
        ImGui::SetItemTooltip("The master switch. Each species also has its own, and a distance beyond which it casts none.");
        changed |= ImGui::SliderFloat("Reflection detail", &s.reflectionDetail, 0.1f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("The water's mirror draws the scene a second time. At 0.5 it sees scatter at half the distance.");
    }
    if (ImGui::CollapsingHeader("Statistics")) {
        const ScatterViewStats& main = scatter->GetViewStats(ViewKind::Main);
        const ScatterViewStats& sun = scatter->GetViewStats(ViewKind::Shadow);
        ImGui::Text("Camera: %d / %d cells, %d / %d instances", main.cellsDrawn, main.cellsTotal, main.instancesDrawn, main.instancesTotal);
        ImGui::Text("%d draw calls, %.2f M triangles", main.drawCalls, main.trianglesDrawn / 1.0e6f);
        ImGui::TextDisabled("Instances per level: %d / %d / %d / %d", main.instancesPerLod[0], main.instancesPerLod[1], main.instancesPerLod[2], main.instancesPerLod[3]);
        ImGui::Text("Sun: %d instances, %d draw calls, %.2f M triangles", sun.instancesDrawn, sun.drawCalls, sun.trianglesDrawn / 1.0e6f);
    }

    ImGui::SeparatorText("Species");
    int removeIndex = -1;
    for (size_t i = 0; i < s.species.size(); ++i) {
        ScatterSpeciesSettings& sp = s.species[i];
        ImGui::PushID(static_cast<int>(i));
        const bool open = ImGui::TreeNodeEx("##species", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap, "%s", sp.name.c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40.0f);
        if (ImGui::SmallButton("Remove##species")) removeIndex = static_cast<int>(i);
        if (open) {
            char nameBuffer[64];
            std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", sp.name.c_str());
            if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) { sp.name = nameBuffer; changed = true; }
            changed |= ImGui::Checkbox("Enabled##species", &sp.enabled);

            // The model: a built-in one or any file the importer reads.
            ImGui::TextDisabled("%s", sp.model.c_str());
            if (ImGui::SmallButton("Pine##builtin")) { sp.model = kBuiltinPine; changed = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Rock##builtin")) { sp.model = kBuiltinRock; changed = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Model file...##species")) {
                const std::string path = PickModelFile(hwnd_);
                if (!path.empty()) { sp.model = path; changed = true; }
            }
            ImGui::SetItemTooltip("Any glTF / FBX / OBJ. Its base centre is where it stands.\nMeshes named Name_LOD0, Name_LOD1... are used as levels of detail; otherwise they are generated.");
            if (!scatter->IsSpeciesReady(i)) {
                const std::string& error = scatter->GetSpeciesError(i);
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", error.empty() ? "Loading..." : error.c_str());
            } else {
                ImGui::TextDisabled("%zu placed, %d levels of detail", scatter->GetInstanceCount(i), scatter->GetSpeciesLodLevels(i));
            }

            ImGui::SeparatorText("Where");
            changed |= ImGui::SliderFloat("Spacing (m)", &sp.spacing, 0.5f, 100.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
            ImGui::SetItemTooltip("The ground is cut into squares this wide, each with at most one plant. The most you can get is 1 / spacing^2 per m^2.");
            changed |= ImGui::SliderFloat("Min distance (m)", &sp.minDistance, 0.0f, sp.spacing, "%.1f");
            ImGui::SetItemTooltip("Two plants are never closer than this. Set it to roughly the width of a full-grown one.");
            changed |= ImGui::SliderFloat("Density", &sp.density, 0.0f, 1.0f, "%.2f");
            ImGui::SetItemTooltip("The chance that a square on PERFECT ground gets a plant.\nAbout %.1f per 1000 m^2 at most.", 1000.0f * sp.density / (sp.spacing * sp.spacing));
            ImGui::TextDisabled("Likes the terrain layer:");
            for (int k = 0; k < kSplatChannels; ++k) changed |= ImGui::SliderFloat(kTerrainLayerNames[k], &sp.layerDensity[k], 0.0f, 1.0f, "%.2f");
            ImGui::SetItemTooltip("1 = grows freely where this layer is, 0 = never. The layers are the baked rules plus what you painted:\npaint grass and trees come, paint rock and they leave.");
            changed |= ImGui::SliderFloat("Min slope", &sp.minSlope, 0.0f, 90.0f, "%.0f deg");
            changed |= ImGui::SliderFloat("Max slope", &sp.maxSlope, 0.0f, 90.0f, "%.0f deg");
            changed |= ImGui::DragFloatRange2("Height (m)", &sp.minHeight, &sp.maxHeight, 1.0f, -1000.0f, 5000.0f, "%.0f", "%.0f");
            changed |= ImGui::SliderFloat("Clumping", &sp.clusterStrength, 0.0f, 1.0f, "%.2f");
            ImGui::SetItemTooltip("0 = evenly spread. 1 = groves with clearings between them.");
            if (sp.clusterStrength > 0.0f) changed |= ImGui::SliderFloat("Clump size (m)", &sp.clusterScale, 5.0f, 300.0f, "%.0f", ImGuiSliderFlags_Logarithmic);

            ImGui::SeparatorText("Each one");
            changed |= ImGui::DragFloatRange2("Scale", &sp.scaleMin, &sp.scaleMax, 0.01f, 0.05f, 10.0f, "%.2f", "%.2f");
            changed |= ImGui::SliderFloat("Follow ground", &sp.alignToNormal, 0.0f, 1.0f, "%.2f");
            ImGui::SetItemTooltip("0 = stands straight up (trees). 1 = leans with the slope (rocks).");
            changed |= ImGui::SliderFloat("Random lean", &sp.tilt, 0.0f, 30.0f, "%.1f deg");
            changed |= ImGui::SliderFloat("Sink", &sp.sink, 0.0f, 0.8f, "%.2f");
            ImGui::SetItemTooltip("The fraction of its height buried in the ground. Rocks look planted, not placed, with a little sink.");
            changed |= ImGui::InputInt("Seed##species", &sp.seed);

            ImGui::SeparatorText("Drawing");
            changed |= ImGui::SliderFloat("Level 1 below", &sp.lodScreen[0], 0.003f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            ImGui::SetItemTooltip("Fractions of the screen's height that one plant covers. Level 1 starts below the first,\nlevel 2 below the second, level 3 below the third. 0.1 is about 100 pixels on a 1080p screen.");
            changed |= ImGui::SliderFloat("Level 2 below", &sp.lodScreen[1], 0.002f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            changed |= ImGui::SliderFloat("Level 3 below", &sp.lodScreen[2], 0.001f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            changed |= ImGui::SliderFloat("Hide below", &sp.cullScreen, 0.0f, 0.05f, "%.4f");
            ImGui::SetItemTooltip("Plants covering less than this fraction of the screen are not drawn. 0.004 is about 4 pixels.");
            changed |= ImGui::Checkbox("Casts shadows", &sp.castShadows);
            if (sp.castShadows) changed |= ImGui::SliderFloat("Shadow distance (m)", &sp.shadowDistance, 10.0f, 500.0f, "%.0f");
            ImGui::SetItemTooltip("Beyond this distance from the camera the species casts no shadow. The sun's shadow map covers the whole terrain,\nso far shadows would be a few blurry texels anyway.");
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removeIndex >= 0) { s.species.erase(s.species.begin() + removeIndex); changed = true; }

    if (ImGui::Button("Add species...")) ImGui::OpenPopup("AddSpecies");
    if (ImGui::BeginPopup("AddSpecies")) {
        auto add = [&](const char* name, const std::string& model, float spacing, float maxSlope) {
            ScatterSpeciesSettings fresh;
            fresh.name = name; fresh.model = model; fresh.spacing = spacing; fresh.minDistance = spacing * 0.6f; fresh.maxSlope = maxSlope;
            fresh.seed = static_cast<int>(s.species.size()) + 1;
            s.species.push_back(fresh);
            changed = true;
        };
        if (ImGui::MenuItem("Pine (built-in)")) add("Pine", kBuiltinPine, 7.0f, 32.0f);
        if (ImGui::MenuItem("Rock (built-in)")) add("Rock", kBuiltinRock, 9.0f, 60.0f);
        if (ImGui::MenuItem("From a model file...")) {
            const std::string path = PickModelFile(hwnd_);
            if (!path.empty()) add(std::filesystem::path(path).stem().string().c_str(), path, 8.0f, 35.0f);
        }
        ImGui::EndPopup();
    }
    if (changed) scatter->SetSettings(s);
}

void EditorLayer::DrawEnvironmentInspector()
{
    EnvironmentSettings& env = scene_->GetEnvironment();
    ImGui::SeparatorText("Image-based lighting");
    ImGui::Checkbox("Sky lighting", &env.enabled);
    ImGui::SetItemTooltip("On: the sky lights everything (soft light + reflections).\nOff: the flat 'Ambient' colour above, like before.");
    ImGui::SliderFloat("Diffuse intensity", &env.diffuseIntensity, 0.0f, 3.0f);
    ImGui::SliderFloat("Specular intensity", &env.specularIntensity, 0.0f, 3.0f);

    static const char* const kViews[] = { "Sky", "Irradiance (diffuse)", "Prefiltered (reflections)" };
    int view = static_cast<int>(env.skyView);
    if (ImGui::Combo("Show as sky background", &view, kViews, IM_ARRAYSIZE(kViews))) env.skyView = static_cast<SkyDebugView>(view);
    if (env.skyView == SkyDebugView::Prefiltered) {
        ImGui::SliderFloat("Roughness level (mip)", &env.debugMip, 0.0f, EnvironmentLighting::MaxPrefilterMip());
    }
    if (ImGui::Button("Bake again")) scene_->InvalidateEnvironment();

    if (ID3D11ShaderResourceView* table = scene_->GetEnvironmentLighting().GetBrdfTable()) {
        ImGui::TextDisabled("BRDF table: x = view angle, y = roughness (red = scale, green = bias)");
        ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(table)), ImVec2(128.0f, 128.0f));
    }
}

void EditorLayer::DrawSurfaceInspector(SurfaceMaterial& surface)
{
    SurfaceSettings& s = surface.Settings();
    ImGui::SeparatorText("Surface maps");
    ImGui::TextDisabled("normal: %s   ORM: %s   emissive: %s",
                        surface.HasNormalMap() ? "yes" : "default", surface.HasOrmMap() ? "yes" : "default",
                        surface.HasEmissiveMap() ? "yes" : "default");

    ImGui::Checkbox("Use normal map", &s.useNormalMap);
    ImGui::SliderFloat("Normal strength", &s.normalStrength, 0.0f, 3.0f);
    ImGui::Checkbox("Flip green (OpenGL maps)", &s.flipGreen);
    ImGui::SetItemTooltip("If the bricks look like they stick OUT where they should be sunken (or the lighting\nseems to come from the wrong side), toggle this.");
    if (surface.HasOrmMap()) {
        ImGui::SliderFloat("Roughness scale", &s.roughnessScale, 0.0f, 2.0f);
        ImGui::SliderFloat("Metallic scale", &s.metallicScale, 0.0f, 1.0f);
    } else {
        ImGui::SliderFloat("Roughness", &s.fallbackRoughness, 0.0f, 1.0f);
        ImGui::SetItemTooltip("0 = mirror, 1 = chalk. (This surface has no ORM map, so this value is the roughness.)");
        ImGui::SliderFloat("Metallic", &s.fallbackMetallic, 0.0f, 1.0f);
    }
    ImGui::SliderFloat("AO strength", &s.aoStrength, 0.0f, 1.0f);
    ImGui::SliderFloat("Emissive intensity", &s.emissiveIntensity, 0.0f, 10.0f);
    ImGui::ColorEdit4("Base colour", &s.baseColor.x, ImGuiColorEditFlags_Float);
    ImGui::SetItemTooltip("Multiplies the albedo map (linear). A surface with no map is just this colour.");
    ImGui::SliderFloat("Alpha cutoff", &s.alphaCutoff, 0.0f, 1.0f);
    ImGui::SetItemTooltip("0 = opaque. Above 0, pixels whose alpha is lower are thrown away (leaves, fences).");

    static const char* const kDebugViews[] = { "Lit", "Vertex normal", "Mapped normal", "Tangent" };
    int debugView = static_cast<int>(s.debugView);
    if (ImGui::Combo("Debug view", &debugView, kDebugViews, IM_ARRAYSIZE(kDebugViews))) {
        s.debugView = static_cast<SurfaceDebugView>(debugView);
    }
    ImGui::SetItemTooltip("Shows a direction as a colour (x = red, y = green, z = blue).\nCompare 'Vertex normal' with 'Mapped normal': the bumps should appear only in the second.");
    if (ImGui::Button("Reset surface")) {
        // Reset only the LOOK. The map convention and the material's own factors come from the FILE: keep them.
        const SurfaceSettings fromFile = s;
        s = SurfaceSettings{};
        s.flipGreen = fromFile.flipGreen;
        s.baseColor = fromFile.baseColor;
        s.alphaCutoff = fromFile.alphaCutoff;
        s.roughnessScale = fromFile.roughnessScale;
        s.metallicScale = fromFile.metallicScale;
        s.fallbackRoughness = fromFile.fallbackRoughness;
        s.fallbackMetallic = fromFile.fallbackMetallic;
    }
}

// Moves `object` straight up or down until the bottom of its box rests on the terrain. The ground is looked up under
// every corner of the box and under its centre, and the HIGHEST ground wins: on a slope the uphill side would
// otherwise sink into the hill. Does nothing if the object is not over the terrain at all.
static void SnapToTerrain(SceneObject& object, const Terrain& terrain)
{
    XMFLOAT4X4 worldFloats;
    XMFLOAT3 boundsMin, boundsMax;
    XMFLOAT3 position, rotation, scale;
    if (!object.GetOrientedBounds(worldFloats, boundsMin, boundsMax)) return;
    if (!object.GetEditorTransform(position, rotation, scale)) return;
    const XMMATRIX world = XMLoadFloat4x4(&worldFloats);

    float lowestY = FLT_MAX;     // the lowest point of the box, in world space
    float groundY = -FLT_MAX;    // the highest ground under it
    bool overTerrain = false;
    auto consider = [&](const XMFLOAT3& localPoint) {
        XMFLOAT3 p;
        XMStoreFloat3(&p, XMVector3Transform(XMLoadFloat3(&localPoint), world));
        lowestY = (std::min)(lowestY, p.y);
        float ground = 0.0f;
        if (terrain.GetHeightAt(p.x, p.z, ground)) { groundY = (std::max)(groundY, ground); overTerrain = true; }
    };
    for (int i = 0; i < 8; ++i) {
        consider({ (i & 1) ? boundsMax.x : boundsMin.x, (i & 2) ? boundsMax.y : boundsMin.y, (i & 4) ? boundsMax.z : boundsMin.z });
    }
    consider({ (boundsMin.x + boundsMax.x) * 0.5f, (boundsMin.y + boundsMax.y) * 0.5f, (boundsMin.z + boundsMax.z) * 0.5f });
    if (!overTerrain) return;

    position.y += groundY - lowestY;
    object.SetEditorTransform(position, rotation, scale);
}

void EditorLayer::DrawTransformEditor(SceneObject* object)
{
    XMFLOAT3 position;
    XMFLOAT3 rotation;
    XMFLOAT3 scale;
    if (!object->GetEditorTransform(position, rotation, scale)) {
        ImGui::Text("Transform editing is not available for this object.");
        return;
    }

    bool changed = false;
    float positionValues[3] = { position.x, position.y, position.z };
    if (ImGui::DragFloat3("Position", positionValues, 0.1f)) {
        position = { positionValues[0], positionValues[1], positionValues[2] };
        changed = true;
    }

    XMFLOAT3 rotationDegrees = {
        XMConvertToDegrees(rotation.x), XMConvertToDegrees(rotation.y), XMConvertToDegrees(rotation.z)
    };
    float rotationValues[3] = { rotationDegrees.x, rotationDegrees.y, rotationDegrees.z };
    if (object->SupportsRotation() && ImGui::DragFloat3("Rotation", rotationValues, 1.0f)) {
        rotation = { XMConvertToRadians(rotationValues[0]), XMConvertToRadians(rotationValues[1]),
                     XMConvertToRadians(rotationValues[2]) };
        changed = true;
    }

    float scaleValues[3] = { scale.x, scale.y, scale.z };
    if (ImGui::DragFloat3("Scale", scaleValues, 0.01f, 0.01f, 100.0f)) {
        scale = { scaleValues[0], scaleValues[1], scaleValues[2] };
        changed = true;
    }
    if (changed) object->SetEditorTransform(position, rotation, scale);

    // Cubes and models can be dropped onto the ground. (Terrain, water and sky are fixtures, not content.)
    const Terrain* terrain = scene_ ? scene_->GetTerrain() : nullptr;
    if (terrain && object->IsUserContent()) {
        if (ImGui::Button("Snap to terrain")) SnapToTerrain(*object, *terrain);
        ImGui::SetItemTooltip("Moves the object straight up or down until the bottom of its box rests on the ground.\nOn a slope it rests on the highest ground under it, so it never sinks in.");
    }
}

void EditorLayer::DrawCameraInspector()
{
    Camera& camera = scene_->GetCamera();
    ImGui::Text("Camera");
    XMFLOAT3 position = camera.GetPosition();
    XMFLOAT3 target = camera.GetTarget();
    XMFLOAT3 up = camera.GetUp();

    float positionValues[3] = { position.x, position.y, position.z };
    if (ImGui::DragFloat3("Position", positionValues, 0.1f))
        camera.SetPosition({ positionValues[0], positionValues[1], positionValues[2] });
    float targetValues[3] = { target.x, target.y, target.z };
    if (ImGui::DragFloat3("Target", targetValues, 0.1f))
        camera.SetTarget({ targetValues[0], targetValues[1], targetValues[2] });
    float upValues[3] = { up.x, up.y, up.z };
    if (ImGui::DragFloat3("Up", upValues, 0.01f, -1.0f, 1.0f))
        camera.SetUp({ upValues[0], upValues[1], upValues[2] });

    float moveSpeed = camera.GetMoveSpeed();
    if (ImGui::SliderFloat("Move speed", &moveSpeed, 0.1f, 100.0f)) camera.SetMoveSpeed(moveSpeed);
    float rotationSpeed = camera.GetRotationSpeed();
    if (ImGui::SliderFloat("Rotation sensitivity", &rotationSpeed, 0.0001f, 0.02f, "%.4f"))
        camera.SetRotationSpeed(rotationSpeed);

    ImGui::SeparatorText("Display");
    PostSettings& post = scene_->GetPostSettings();
    ImGui::SliderFloat("Exposure (stops)", &post.exposureEV, PostSettings::kExposureMin, PostSettings::kExposureMax, "%+.1f EV");
    ImGui::SetItemTooltip("Each +1 doubles the light, each -1 halves it - like a camera's exposure compensation.");
    static const char* const kToneMapperNames[] = { "None (clamp)", "Reinhard", "ACES filmic", "Uncharted 2 filmic" };
    int toneMapper = static_cast<int>(post.toneMapper);
    if (ImGui::Combo("Tone mapping", &toneMapper, kToneMapperNames, IM_ARRAYSIZE(kToneMapperNames))) {
        post.toneMapper = static_cast<ToneMapper>(toneMapper);
    }
    ImGui::SetItemTooltip("Squeezes unlimited HDR brightness into what a screen can show.\n'None' clips everything above 1.0 to white.");
    ImGui::Checkbox("sRGB encode (gamma)", &post.srgbEncode);
    ImGui::SetItemTooltip("The HDR buffer is linear light; monitors expect the sRGB curve.\nTurn it off to see how dark and harsh un-encoded linear light looks.");
    if (ImGui::Button("Reset display")) post = PostSettings{};
}

void EditorLayer::DrawLightsInspector()
{
    LightSystem& lights = scene_->GetLightSystem();
    XMFLOAT3& ambient = lights.Ambient();
    float ambientValues[3] = { ambient.x, ambient.y, ambient.z };
    if (ImGui::ColorEdit3("Ambient", ambientValues)) ambient = { ambientValues[0], ambientValues[1], ambientValues[2] };

    DirectionalLight& directional = lights.Directional();
    ImGui::SeparatorText("Directional light");
    DrawFloat3("Direction", directional.direction, 0.01f, -1.0f, 1.0f);
    float directionalColor[3] = { directional.color.x, directional.color.y, directional.color.z };
    if (ImGui::ColorEdit3("Directional color", directionalColor))
        directional.color = { directionalColor[0], directionalColor[1], directionalColor[2] };
    ImGui::SliderFloat("Directional intensity", &directional.intensity, 0.0f, 5.0f);

    DrawShadowInspector();
    DrawEnvironmentInspector();

    ImGui::SeparatorText("Point lights");
    auto& pointLights = lights.PointLights();
    for (int index = 0; index < static_cast<int>(pointLights.size()); ++index) {
        PointLight& point = pointLights[index];
        ImGui::PushID(index);
        if (ImGui::TreeNode("Point light")) {
            DrawFloat3("Position", point.position, 0.1f, -1000.0f, 1000.0f);
            float color[3] = { point.color.x, point.color.y, point.color.z };
            if (ImGui::ColorEdit3("Color", color)) point.color = { color[0], color[1], color[2] };
            ImGui::SliderFloat("Intensity", &point.intensity, 0.0f, 10.0f);
            ImGui::SliderFloat("Range", &point.range, 0.1f, 100.0f);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (pointLights.size() < LightSystem::kMaxPointLights && ImGui::Button("Add point light")) {
        PointLight point;
        point.position = { 0.0f, 10.0f, 0.0f };
        lights.AddPointLight(point);
    }
}

void EditorLayer::DrawShadowInspector()
{
    ImGui::SeparatorText("Shadows");
    ShadowMap& shadow = scene_->GetShadowMap();
    if (!shadow.IsValid()) { ImGui::TextDisabled("Shadow map unavailable"); return; }

    // Every value is cheap to change, so edits apply live (see ShadowMap::SetSettings for what
    // actually gets rebuilt: only the resolution reallocates, only the biases rebuild a state).
    ShadowSettings s = shadow.GetSettings();
    bool changed = false;
    changed |= ImGui::Checkbox("Enabled", &s.enabled);

    char label[32];
    std::snprintf(label, sizeof(label), "%d x %d", s.resolution, s.resolution);
    if (ImGui::BeginCombo("Resolution", label)) {
        for (int size = ShadowSettings::kResolutionMin; size <= ShadowSettings::kResolutionMax; size *= 2) {
            std::snprintf(label, sizeof(label), "%d x %d", size, size);
            if (ImGui::Selectable(label, size == s.resolution)) { s.resolution = size; changed = true; }
        }
        ImGui::EndCombo();
    }
    changed |= ImGui::SliderFloat("Strength", &s.strength, 0.0f, 1.0f);
    ImGui::SetItemTooltip("How dark shadowed areas get. Only the sun is shadowed; ambient light remains.");

    ImGui::SeparatorText("Bias (fights shadow acne)");
    changed |= ImGui::SliderInt("Depth bias", &s.depthBias, 0, ShadowSettings::kDepthBiasMax);
    ImGui::SetItemTooltip("Constant push away from the sun while drawing the map. Too high: shadows detach from objects.");
    changed |= ImGui::SliderFloat("Slope bias", &s.slopeBias, 0.0f, ShadowSettings::kSlopeBiasMax);
    ImGui::SetItemTooltip("Extra push that grows on surfaces tilted away from the sun.");
    changed |= ImGui::SliderFloat("Normal offset", &s.normalOffset, 0.0f, ShadowSettings::kNormalOffsetMax, "%.2f texels");
    ImGui::SetItemTooltip("Moves the lookup point off the surface along its normal. Best against stripes on slopes.");

    if (ImGui::Button("Reset shadows")) { s = ShadowSettings{}; changed = true; }
    if (changed) shadow.SetSettings(s);

    // What the current settings cost and cover.
    const double megabytes = static_cast<double>(shadow.GetResolution()) * shadow.GetResolution() * 4.0 / (1024.0 * 1024.0);
    ImGui::TextDisabled("Map memory: %.0f MB", megabytes);
    if (shadow.GetTexelWorldSize() > 0.0f) ImGui::TextDisabled("One texel covers %.3f world units", shadow.GetTexelWorldSize());

    ImGui::Checkbox("Show sun depth map", &showShadowMap_);
    if (showShadowMap_) {
        // Depth is stored in one channel, so ImGui draws it in red: darker = closer to the sun.
        ImGui::Image(reinterpret_cast<ImTextureID>(shadow.GetShaderView()), ImVec2(256.0f, 256.0f));
    }
}

void EditorLayer::ToggleSculptMode()
{
    Terrain* terrain = scene_ ? scene_->GetTerrain() : nullptr;
    if (!terrain) { sculptMode_ = false; return; }
    sculptMode_ = !sculptMode_;
    if (sculptMode_) Select(terrain);   // the brush settings live in the terrain's inspector
}

// The mouse button came up (or the mode ended): what the stroke changed becomes ONE undo step.
void EditorLayer::FinishSculptStroke(Terrain* terrain)
{
    sculptStroking_ = false;
    if (!terrain) return;
    if (strokeIsPaint_) {
        HeightfieldPatch patch = terrain->EndPaintStroke();
        if (!patch.IsEmpty()) RecordPaintStroke(terrain->GetId(), std::move(patch));
    } else {
        HeightfieldPatch patch = terrain->EndSculptStroke();
        if (!patch.IsEmpty()) RecordSculptStroke(terrain->GetId(), std::move(patch));
    }
}

void EditorLayer::RecordSculptStroke(SceneObjectId terrainId, HeightfieldPatch patch)
{
    // The command holds the patch (not the terrain, which may be rebuilt or reloaded) and looks the terrain up by id.
    const auto shared = std::make_shared<HeightfieldPatch>(std::move(patch));
    auto run = [this, terrainId, shared](bool redo) {
        if (!scene_) return;
        if (auto* terrain = dynamic_cast<Terrain*>(scene_->Find(terrainId))) terrain->ApplyPatch(*shared, redo);
    };
    history_.Push(std::make_unique<LambdaCommand>("Sculpt terrain", [run]() { run(false); }, [run]() { run(true); }));
}

void EditorLayer::DiscardSculpt(Terrain* terrain)
{
    std::vector<float> original;
    if (!terrain || !terrain->GenerateOriginalHeights(original)) return;
    const auto before = std::make_shared<std::vector<float>>(terrain->GetHeightfield().Samples());
    const auto after = std::make_shared<std::vector<float>>(std::move(original));
    if (!terrain->ReplaceHeights(*after, false)) return;

    const SceneObjectId id = terrain->GetId();
    auto replace = [this, id](const std::shared_ptr<std::vector<float>>& samples, bool sculpted) {
        if (!scene_) return;
        if (auto* target = dynamic_cast<Terrain*>(scene_->Find(id))) target->ReplaceHeights(*samples, sculpted);
    };
    history_.Push(std::make_unique<LambdaCommand>("Discard sculpting", [replace, before]() { replace(before, true); },
                                                  [replace, after]() { replace(after, false); }));
}

void EditorLayer::RecordPaintStroke(SceneObjectId terrainId, HeightfieldPatch patch)
{
    // Same shape as a sculpt stroke: the command holds the patch and finds the terrain by id.
    const auto shared = std::make_shared<HeightfieldPatch>(std::move(patch));
    auto run = [this, terrainId, shared](bool redo) {
        if (!scene_) return;
        if (auto* terrain = dynamic_cast<Terrain*>(scene_->Find(terrainId))) terrain->ApplyPaintPatch(*shared, redo);
    };
    history_.Push(std::make_unique<LambdaCommand>("Paint terrain", [run]() { run(false); }, [run]() { run(true); }));
}

void EditorLayer::ClearPaint(Terrain* terrain)
{
    if (!terrain || terrain->GetPaintMap().IsEmpty()) return;
    const auto before = std::make_shared<std::vector<float>>(terrain->GetPaintMap().weights);
    const auto after = std::make_shared<std::vector<float>>(before->size(), 0.0f);   // nothing painted: the rules rule again
    if (!terrain->ReplacePaint(*after, false)) return;

    const SceneObjectId id = terrain->GetId();
    auto replace = [this, id](const std::shared_ptr<std::vector<float>>& weights, bool painted) {
        if (!scene_) return;
        if (auto* target = dynamic_cast<Terrain*>(scene_->Find(id))) target->ReplacePaint(*weights, painted);
    };
    history_.Push(std::make_unique<LambdaCommand>("Clear painting", [replace, before]() { replace(before, true); },
                                                  [replace, after]() { replace(after, false); }));
}

void EditorLayer::DrawSculptSection(Terrain* terrain)
{
    // ImGui tells widgets apart by their label, and the noise section below also has a "Strength" slider. Without
    // its own ID scope the two would be ONE widget: dragging either would move both.
    ImGui::PushID("sculpt");
    ImGui::SeparatorText("Brush");
    ImGui::Checkbox("Brush mode  [4]", &sculptMode_);
    ImGui::SetItemTooltip("Left-drag on the terrain to use the brush.\nShift inverts it.  [ and ] change the radius.");

    // What the brush changes: the SHAPE of the ground, or what it is MADE OF.
    ImGui::RadioButton("Shape", &brushTarget_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Paint layers", &brushTarget_, 1);
    ImGui::SetItemTooltip("Shape raises and lowers the ground. Paint layers overrules the automatic slope and height rules.");

    if (brushTarget_ == 0) {
        int mode = static_cast<int>(brush_.mode);
        ImGui::RadioButton("Raise", &mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Lower", &mode, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Smooth", &mode, 2);
        ImGui::SameLine();
        ImGui::RadioButton("Flatten", &mode, 3);
        ImGui::SetItemTooltip("Flatten levels the ground towards the height where the stroke began.");
        brush_.mode = static_cast<BrushMode>(mode);
    } else {
        // One button per layer; the chosen one is highlighted. As with every push/pop: decide from `chosen`, which is
        // read BEFORE the click can change brushLayer_.
        for (int layer = 0; layer < kTerrainLayerCount; ++layer) {
            if (layer > 0) ImGui::SameLine();
            const bool chosen = (brushLayer_ == layer);
            if (chosen) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(kTerrainLayerNames[layer])) brushLayer_ = layer;
            if (chosen) ImGui::PopStyleColor();
        }
        ImGui::Checkbox("Erase", &brushErase_);
        ImGui::SetItemTooltip("Erase takes painted layers away, and the automatic rules come back.\nShift flips paint and erase while you drag.");
    }
    ImGui::SliderFloat("Radius", &brush_.radius, kBrushRadiusMin, kBrushRadiusMax, "%.1f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Strength", &brush_.strength, 0.02f, 1.0f, "%.2f");
    ImGui::SliderFloat("Falloff", &brush_.falloff, 0.0f, 1.0f, "%.2f");
    ImGui::SetItemTooltip("0 = full strength right to the edge of the brush, 1 = fades out from the centre.");

    if (terrain->IsSculpted()) {
        ImGui::TextDisabled("Shaped by hand. Saved next to the scene file as a 16-bit PNG.");
        ImGui::TextDisabled("Mesh resolution, noise and the heightmap are locked until you discard.");
        if (ImGui::Button("Discard sculpting")) DiscardSculpt(terrain);
        ImGui::SetItemTooltip("Go back to the heightmap and noise. Undo brings the sculpting back.");
    }
    if (terrain->IsPainted()) {
        ImGui::TextDisabled("Painted by hand. Saved next to the scene file as a PNG.");
        ImGui::TextDisabled("Mesh resolution and the heightmap are locked until you clear the painting.");
        if (ImGui::Button("Clear painting")) ClearPaint(terrain);
        ImGui::SetItemTooltip("Back to the automatic rules everywhere. Undo brings the painting back.");
    }
    ImGui::PopID();
}

void EditorLayer::DrawTerrainInspector(Terrain* terrain)
{
    // Mesh-shaping values rebuild ~half a million vertices, so they are applied when you
    // RELEASE the control, not on every frame of a drag. While a control is being edited we keep
    // our own copy; otherwise we follow the terrain so changes made elsewhere show up.
    if (!ImGui::IsAnyItemActive()) pendingTerrainSettings_ = terrain->GetSettings();
    TerrainSettings& s = pendingTerrainSettings_;

    bool commit = false;
    auto commitOnRelease = [&commit] { if (ImGui::IsItemDeactivatedAfterEdit()) commit = true; };

    DrawSculptSection(terrain);

    ImGui::SeparatorText("Shape");
    // One world unit is one metre. Width is shown in kilometres; height in metres. Both are ABSOLUTE sizes: a wider terrain with
    // the same height scale is simply flatter (the same heightmap picture is stretched over more ground), which is how every
    // engine behaves, so "Keep proportions" offers to raise the height scale together with the width.
    {
        const TerrainSettings& committed = terrain->GetSettings();   // unchanged until the drag is released
        float widthKm = s.width / 1000.0f;
        if (ImGui::SliderFloat("Width", &widthKm, TerrainSettings::kWidthMin / 1000.0f, TerrainSettings::kWidthMax / 1000.0f, "%.3f km",
                               ImGuiSliderFlags_Logarithmic)) {
            s.width = widthKm * 1000.0f;
            if (terrainKeepProportions_ && committed.width > 0.0f) {
                s.heightScale = std::clamp(committed.heightScale * s.width / committed.width, TerrainSettings::kHeightMin, TerrainSettings::kHeightMax);
            }
        }
        commitOnRelease();
        ImGui::SetItemTooltip("Along X, in kilometres (1 world unit = 1 metre). The depth follows the heightmap's shape.");
        ImGui::Checkbox("Keep proportions", &terrainKeepProportions_);
        ImGui::SetItemTooltip("On: changing the width changes the height scale by the same factor, so the terrain keeps its look.\nOff: the height stays the same, so a wider terrain is flatter.");
    }
    ImGui::SliderFloat("Height scale", &s.heightScale, TerrainSettings::kHeightMin, TerrainSettings::kHeightMax, "%.0f m");
    commitOnRelease();
    ImGui::SetItemTooltip("How tall a white heightmap pixel is, in metres.");
    // A different grid would lose both a sculpt and a painting, so the grid-defining controls lock for either.
    ImGui::BeginDisabled(terrain->IsSculpted() || terrain->IsPainted());
    ImGui::SliderInt("Mesh resolution", &s.maxCells, TerrainSettings::kCellsMin, TerrainSettings::kCellsMax);
    commitOnRelease();
    ImGui::SetItemTooltip("Grid cells across the terrain. Higher = smoother and slower to rebuild.");
    ImGui::EndDisabled();
    {
        const DirectX::XMFLOAT2 extent = terrain->GetWorldExtent();   // includes the transform's scale
        const int cellsX = terrain->GetHeightfield().GetLayout().cellsX;
        ImGui::TextDisabled("World size %.3f x %.3f km, %.2f m per cell", extent.x / 1000.0f, extent.y / 1000.0f,
                            cellsX > 0 ? extent.x / static_cast<float>(cellsX) : 0.0f);
    }

    ImGui::SeparatorText("Noise");
    ImGui::BeginDisabled(terrain->IsSculpted());   // noise makes the heights, which a sculpt replaces (a painting does not mind)
    ImGui::SliderFloat("Strength", &s.noiseStrength, 0.0f, 1.0f, "%.3f");
    commitOnRelease();
    ImGui::SliderFloat("Scale", &s.noiseScale, TerrainSettings::kNoiseScaleMin, TerrainSettings::kNoiseScaleMax, "%.2f");
    commitOnRelease();
    ImGui::SliderInt("Octaves", &s.noiseOctaves, TerrainSettings::kOctavesMin, TerrainSettings::kOctavesMax);
    commitOnRelease();
    ImGui::SetItemTooltip("Layers of detail. Each octave doubles the frequency and halves the amplitude.");
    ImGui::InputInt("Seed", &s.noiseSeed);
    commitOnRelease();
    ImGui::SameLine();
    if (ImGui::Button("Random")) {
        s.noiseSeed = static_cast<int>(ImGui::GetTime() * 1000.0) & 0xffff;
        commit = true;
    }

    ImGui::EndDisabled();

    ImGui::SeparatorText("Material");
    // Tiling is just a shader constant, so it applies live while you drag.
    if (ImGui::SliderFloat("Texture tiling", &s.uvTiling, TerrainSettings::kTilingMin,
                           TerrainSettings::kTilingMax, "%.2f", ImGuiSliderFlags_Logarithmic)) {
        terrain->ApplySettings(s);
    }

    ImGui::SeparatorText("Texture files");
    ImGui::TextDisabled("Click a button to choose an image. Right-click it to go back to the default.");
    {
        // The label is the file name ("default" for the stock file); the tooltip shows the whole path.
        // Returns 0 = nothing, 1 = clicked, 2 = right-clicked.
        auto fileButton = [](const char* id, const std::string& current, const std::string& standard, float width) -> int {
            std::string label = (current == standard) ? std::string("default") : std::filesystem::path(current).filename().string();
            if (label.size() > 14) label = label.substr(0, 11) + "...";
            label += "##";
            label += id;
            const bool clicked = ImGui::Button(label.c_str(), ImVec2(width, 0.0f));
            ImGui::SetItemTooltip("%s", current.c_str());
            if (clicked) return 1;
            return ImGui::IsItemClicked(ImGuiMouseButton_Right) ? 2 : 0;
        };

        if (ImGui::BeginTable("terrainTextureFiles", 4, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 48.0f);
            ImGui::TableSetupColumn("Albedo");
            ImGui::TableSetupColumn("Normal");
            ImGui::TableSetupColumn("ORM");
            ImGui::TableHeadersRow();
            for (int layer = 0; layer < kTerrainLayerCount; ++layer) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(kTerrainLayerNames[layer]);
                for (int map = 0; map < 3; ++map) {
                    ImGui::TableNextColumn();
                    const auto kind = static_cast<TerrainLayerMap>(map);
                    char id[32];
                    std::snprintf(id, sizeof(id), "layer%d_%d", layer, map);
                    const int result = fileButton(id, terrain->GetLayerMapPath(layer, kind), Terrain::DefaultLayerMapPath(layer, kind), -FLT_MIN);
                    if (result == 1) terrainFileRequest_ = { 1, layer, map };
                    else if (result == 2) terrain->SetLayerMap(layer, kind, "");
                }
            }
            ImGui::EndTable();
        }
        ImGui::BeginDisabled(terrain->IsSculpted() || terrain->IsPainted());   // a new heightmap could change the grid, losing a sculpt or a painting
        ImGui::TextUnformatted("Heightmap");
        ImGui::SameLine(90.0f);
        const int heightResult = fileButton("heightmap", terrain->GetHeightmapPath(), Terrain::DefaultHeightmapPath(), 160.0f);
        if (heightResult == 1) terrainFileRequest_ = { 2, 0, 0 };
        else if (heightResult == 2) terrain->SetHeightmap("");
        ImGui::SetItemTooltip("%s\nA 16-bit greyscale image gives much smoother slopes than 8-bit.", terrain->GetHeightmapPath().c_str());
        ImGui::EndDisabled();
    }

    ImGui::SeparatorText("Layers");
    // Layer settings are only shader constants, so they apply live while you drag.
    bool layersChanged = false;
    {
        int albedoFiles = 0, normalFiles = 0, ormFiles = 0;
        terrain->GetLayerFileCounts(albedoFiles, normalFiles, ormFiles);
        ImGui::TextDisabled("Layer files found (of 4): albedo %d, normal %d, ORM %d", albedoFiles, normalFiles, ormFiles);
        ImGui::SetItemTooltip("Looked up as assets/Terrain/<Grass|Rock|Sand|Snow>/albedo.png, normal.png, orm.png.\nA missing file falls back to a neutral map, so the layer is plain tinted colour.");
    }
    layersChanged |= ImGui::Checkbox("Show layer weights", &s.showLayers);
    ImGui::SetItemTooltip("Paints rock as red, snow as green and shore sand as blue - use it to tune the thresholds.");
    layersChanged |= ImGui::SliderFloat("Rock starts at slope", &s.slopeStart, 0.0f, 1.0f, "%.2f");
    ImGui::SetItemTooltip("0 = flat, 1 = vertical. About 0.3 is a 45 degree slope.");
    layersChanged |= ImGui::SliderFloat("Rock blend", &s.slopeFade, 0.01f, 1.0f, "%.2f");
    layersChanged |= ImGui::SliderFloat("Rock tiling", &s.rockTiling, TerrainSettings::kRockTilingMin,
                                        TerrainSettings::kRockTilingMax, "%.3f", ImGuiSliderFlags_Logarithmic);
    layersChanged |= ImGui::SliderFloat("Rock brightness", &s.rockDarkness, 0.1f, 1.5f, "%.2f");
    layersChanged |= ImGui::SliderFloat("Shore height", &s.shoreHeight, TerrainSettings::kBandMin, TerrainSettings::kBandMax, "%.1f");
    if (Water* water = scene_->GetWater()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Match water")) { s.shoreHeight = water->GetSettings().height + 1.0f; layersChanged = true; }
    }
    layersChanged |= ImGui::SliderFloat("Shore blend", &s.shoreWidth, 0.1f, 30.0f, "%.1f");
    layersChanged |= ImGui::SliderFloat("Snow height", &s.snowHeight, TerrainSettings::kBandMin, TerrainSettings::kBandMax, "%.1f");
    layersChanged |= ImGui::SliderFloat("Snow blend", &s.snowFade, 0.1f, 50.0f, "%.1f");
    layersChanged |= ImGui::SliderFloat("Edge breakup", &s.breakupAmount, 0.0f, 1.0f, "%.2f");
    layersChanged |= ImGui::SliderFloat("Breakup scale", &s.breakupScale, TerrainSettings::kBreakupScaleMin,
                                        TerrainSettings::kBreakupScaleMax, "%.3f", ImGuiSliderFlags_Logarithmic);
    layersChanged |= ImGui::SliderFloat("Ground tiling", &s.groundTiling, TerrainSettings::kRockTilingMin,
                                        TerrainSettings::kRockTilingMax, "%.3f", ImGuiSliderFlags_Logarithmic);
    layersChanged |= ImGui::SliderFloat("Sand tiling", &s.sandTiling, TerrainSettings::kRockTilingMin,
                                        TerrainSettings::kRockTilingMax, "%.3f", ImGuiSliderFlags_Logarithmic);
    layersChanged |= ImGui::SliderFloat("Snow tiling", &s.snowTiling, TerrainSettings::kRockTilingMin,
                                        TerrainSettings::kRockTilingMax, "%.3f", ImGuiSliderFlags_Logarithmic);
    layersChanged |= ImGui::SliderFloat("Height blend", &s.heightBlend, 0.0f, 1.0f, "%.2f");
    ImGui::SetItemTooltip("0 = layers fade into each other like a double exposure.\nHigher = the taller bumps of each layer win, so stones poke through sand.");
    layersChanged |= ImGui::SliderFloat("Normal strength", &s.normalStrength, 0.0f, 3.0f, "%.2f");
    layersChanged |= ImGui::Checkbox("Flip green (OpenGL maps)", &s.flipGreen);
    layersChanged |= ImGui::SliderFloat("AO strength", &s.aoStrength, 0.0f, 1.0f, "%.2f");
    layersChanged |= ImGui::SliderFloat("Roughness scale", &s.roughnessScale, 0.0f, 2.0f, "%.2f");
    if (ImGui::Button("Use original colours")) {
        const XMFLOAT3 white(1.0f, 1.0f, 1.0f);
        s.groundTint = white; s.rockTint = white; s.shoreColor = white; s.snowColor = white;
        layersChanged = true;
    }
    ImGui::SetItemTooltip("Tints MULTIPLY the layer textures.\nThis resets them to white, so every layer shows exactly the colours of its image.");
    float groundRgb[3] = { s.groundTint.x, s.groundTint.y, s.groundTint.z };
    if (ImGui::ColorEdit3("Ground tint", groundRgb)) { s.groundTint = { groundRgb[0], groundRgb[1], groundRgb[2] }; layersChanged = true; }
    float rockRgb[3] = { s.rockTint.x, s.rockTint.y, s.rockTint.z };
    if (ImGui::ColorEdit3("Rock tint", rockRgb)) { s.rockTint = { rockRgb[0], rockRgb[1], rockRgb[2] }; layersChanged = true; }
    float sand[3] = { s.shoreColor.x, s.shoreColor.y, s.shoreColor.z };
    if (ImGui::ColorEdit3("Sand tint", sand)) { s.shoreColor = { sand[0], sand[1], sand[2] }; layersChanged = true; }
    float snowRgb[3] = { s.snowColor.x, s.snowColor.y, s.snowColor.z };
    if (ImGui::ColorEdit3("Snow tint", snowRgb)) { s.snowColor = { snowRgb[0], snowRgb[1], snowRgb[2] }; layersChanged = true; }
    if (ImGui::Button("Reset layers")) { s.ResetLayers(); layersChanged = true; }
    if (layersChanged) terrain->ApplySettings(s);

    ImGui::SeparatorText("Chunks, culling and detail");
    // These are all cheap switches, so they apply live.
    bool detailChanged = false;
    detailChanged |= ImGui::Checkbox("Frustum culling", &s.frustumCulling);
    ImGui::SetItemTooltip("Skip terrain tiles that cannot be seen. Turn it off to compare the triangle counts.");
    detailChanged |= ImGui::Checkbox("Level of detail", &s.lodEnabled);
    ImGui::SetItemTooltip("Distant tiles use fewer triangles.");
    detailChanged |= ImGui::SliderFloat("Full detail until", &s.lodDistance0, TerrainSettings::kLodDistanceMin,
                                        TerrainSettings::kLodDistanceMax, "%.0f", ImGuiSliderFlags_Logarithmic);
    detailChanged |= ImGui::SliderFloat("Half detail until", &s.lodDistance1, TerrainSettings::kLodDistanceMin,
                                        TerrainSettings::kLodDistanceMax * 2.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
    detailChanged |= ImGui::Checkbox("Wireframe", &s.wireframe);
    ImGui::SetItemTooltip("Draws the terrain as wires so you can SEE the detail levels (and the skirts at tile edges).");
    if (detailChanged) terrain->ApplySettings(s);
    static const char* const kViewNames[] = { "Camera", "Mirror", "Sun" };
    for (int i = 0; i < kViewKindCount; ++i) {
        const TerrainViewStats& stats = terrain->GetStats(static_cast<ViewKind>(i));
        ImGui::TextDisabled("%-6s %3d/%3d chunks  LOD %d/%d/%d  %d tris", kViewNames[i],
                            stats.chunksDrawn, stats.chunksTotal, stats.chunksPerLod[0], stats.chunksPerLod[1],
                            stats.chunksPerLod[2], stats.trianglesDrawn);
    }

    ImGui::Separator();
    if (ImGui::Button("Reset terrain")) {
        s = TerrainSettings{};
        commit = true;
    }
    if (commit) lastTerrainGenerationSucceeded_ = terrain->ApplySettings(s);
    if (!lastTerrainGenerationSucceeded_) ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Terrain rebuild failed");

    float minY = 0.0f, maxY = 0.0f;
    terrain->GetHeightRange(minY, maxY);
    ImGui::TextDisabled("Surface spans Y %.1f to %.1f", minY, maxY);
}

void EditorLayer::DrawWaterInspector(Water* water)
{
    // Every water value is just a shader constant, so changes apply live - no pending copy,
    // no Apply button (the one exception, Mesh detail, rebuilds the grid in a blink).
    // We read the current settings, edit them, and write back if anything moved.
    WaterSettings s = water->GetSettings();
    bool changed = false;
    auto colorEdit = [&changed](const char* label, DirectX::XMFLOAT3& value) {
        float rgb[3] = { value.x, value.y, value.z };
        if (ImGui::ColorEdit3(label, rgb)) {
            value = { rgb[0], rgb[1], rgb[2] };
            changed = true;
        }
    };

    // A scene can hold several water bodies (a lake, a pool, a river): the name tells them apart in the Hierarchy.
    {
        char nameBuffer[64];
        std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", water->GetName().c_str());
        if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) water->SetName(nameBuffer);
    }

    // The TYPE comes first: it decides how the water moves, and which of the sections below apply.
    static const char* const kTypeNames[] = { "Ocean", "Lake", "River", "Pond" };
    int typeIndex = static_cast<int>(s.bodyType);
    if (ImGui::Combo("Type", &typeIndex, kTypeNames, IM_ARRAYSIZE(kTypeNames))) {
        s.ApplyBodyType(static_cast<WaterBodyType>(typeIndex));
        changed = true;
    }
    ImGui::SetItemTooltip("Ocean: big swells rolling with the wind.\nLake: short wind chop.\nRiver: a current - waves and ripples all travel downstream.\nPond: nearly still.\n\nChanging the type loads that type's starting values (waves, ripples, foam, colours).\nLevel, size and shape stay. Ctrl+Z brings the old values back.");
    const bool river = s.bodyType == WaterBodyType::River;

    // A whole look in one click: colours, depth and foam. Waves, level and size are left alone.
    ImGui::TextDisabled("Looks");
    static const struct { const char* name; WaterLook look; } kLooks[] = {
        { "Tropical", WaterLook::Tropical }, { "Ocean", WaterLook::Ocean }, { "Lake", WaterLook::Lake },
        { "Swamp", WaterLook::Swamp }, { "Arctic", WaterLook::Arctic } };
    for (size_t i = 0; i < IM_ARRAYSIZE(kLooks); ++i) {
        if (i > 0) ImGui::SameLine();
        if (ImGui::SmallButton(kLooks[i].name)) { s.ApplyLook(kLooks[i].look); changed = true; }
    }
    ImGui::SetItemTooltip("Each button sets colours, depth fade, foam, refraction and scatter.\nWaves, level and size stay as they are. Tweak freely afterwards.");

    if (ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Level", &s.height, WaterSettings::kHeightMin, WaterSettings::kHeightMax, "%.1f");
        static const char* const kShapeNames[] = { "Rectangle (lake, river, pool)", "Ellipse (round pond)" };
        int shapeIndex = static_cast<int>(s.shape);
        if (ImGui::Combo("Shape", &shapeIndex, kShapeNames, IM_ARRAYSIZE(kShapeNames))) {
            s.shape = static_cast<WaterShape>(shapeIndex);
            changed = true;
        }
        changed |= ImGui::SliderFloat("Width (X)", &s.size, WaterSettings::kSizeMin, WaterSettings::kSizeMax, "%.0f", ImGuiSliderFlags_Logarithmic);
        changed |= ImGui::SliderFloat("Length (Z)", &s.sizeZ, WaterSettings::kSizeMin, WaterSettings::kSizeMax, "%.0f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("A river is a long thin rectangle: set the Type to River, make it long, then turn it with the\nRotation Y in the transform. Its current follows the way it is turned.");
        if (Terrain* terrain = scene_->GetTerrain()) {
            if (ImGui::Button("Fit size to terrain")) {
                const DirectX::XMFLOAT2 extent = terrain->GetWorldExtent();
                s.size = (std::max)(extent.x, extent.y) * 1.2f;   // a little larger: no gap at the shore
                s.sizeZ = s.size;
                changed = true;
            }
            float minY = 0.0f, maxY = 0.0f;
            terrain->GetHeightRange(minY, maxY);
            ImGui::SameLine();
            ImGui::TextDisabled("terrain Y %.1f to %.1f", minY, maxY);
        }
        colorEdit("Deep color", s.color);
        changed |= ImGui::SliderFloat("Opacity", &s.alpha, 0.05f, 1.0f);
        colorEdit("Shallow color", s.shallowColor);
        changed |= ImGui::SliderFloat("Absorption", &s.absorption, WaterSettings::kAbsorptionMin,
                                      WaterSettings::kAbsorptionMax, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("How quickly the colour goes from shallow to deep as the water gets thicker.");
        changed |= ImGui::SliderFloat("Shore softness", &s.fadeDistance, WaterSettings::kFadeMin,
                                      WaterSettings::kFadeMax, "%.1f");
        ImGui::SetItemTooltip("Depth at which the water becomes fully opaque. Smaller = sharper shoreline.");
    }

    if (river && ImGui::CollapsingHeader("Current", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Flow speed", &s.flowSpeed, 0.0f, WaterSettings::kFlowSpeedMax, "%.1f units/s");
        ImGui::SetItemTooltip("How fast the waves and ripples are carried downstream.");
        changed |= ImGui::SliderFloat("Flow direction", &s.flowAngle, -180.0f, 180.0f, "%.0f deg");
        ImGui::SetItemTooltip("Measured from the river's own length: 0 = toward the +Z end of the body, 180 = the other way.\nTurning the river with Rotation Y turns the current with it.");
        ImGui::TextDisabled("Flows toward %.0f deg in the world (0 = +X, 90 = +Z)", water->GetTravelDegrees());
    }

    if (ImGui::CollapsingHeader("Waves", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!river) {   // the three presets are open-water swells: a river's waves come from its type
            if (ImGui::Button("Calm"))   { s.ApplyWavePreset(WavePreset::Calm);   changed = true; }
            ImGui::SameLine();
            if (ImGui::Button("Choppy")) { s.ApplyWavePreset(WavePreset::Choppy); changed = true; }
            ImGui::SameLine();
            if (ImGui::Button("Stormy")) { s.ApplyWavePreset(WavePreset::Stormy); changed = true; }
        }
        changed |= ImGui::SliderFloat("Amplitude", &s.waveAmplitude, 0.0f, WaterSettings::kAmplitudeMax);
        if (!river) changed |= ImGui::SliderFloat("Speed", &s.waveSpeed, 0.0f, WaterSettings::kSpeedMax);
        changed |= ImGui::SliderFloat("Frequency", &s.waveFrequency, WaterSettings::kFrequencyMin, WaterSettings::kFrequencyMax);
        ImGui::SetItemTooltip("Radians per world unit. Lower = longer, lazier waves.");
        if (!river) {   // a river's direction is its current (above)
            changed |= ImGui::SliderFloat("Wind direction", &s.windDirection, 0.0f, 360.0f, "%.0f deg");
            ImGui::SetItemTooltip("The way the waves travel. 0 = toward +X, 90 = toward +Z.");
        }
        changed |= ImGui::SliderFloat("Direction spread", &s.directionalSpread, 0.0f, 1.0f);
        ImGui::SetItemTooltip(river ? "0 = every wave runs with the current. A river caps this low: nothing runs upstream."
                                    : "0 = every wave runs with the wind (a clean swell). 1 = waves cross from many directions.");
        changed |= ImGui::SliderFloat("Choppiness", &s.choppiness, 0.0f, 1.0f);
        ImGui::SetItemTooltip("0 = rounded sine waves. 1 = sharp crests and wide troughs (Gerstner waves). Never folds over.");
        changed |= ImGui::SliderInt("Wave count", &s.waveCount, WaterSettings::kWaveCountMin, WaterSettings::kWaveCountMax);
        ImGui::SetItemTooltip("How many waves are added together. Fewer = cheaper and simpler. Each costs a little in the\nvertex AND pixel shader.");
        ImGui::SeparatorText("Ripples");
        changed |= ImGui::SliderFloat("Ripple strength", &s.detailNormal, 0.0f, WaterSettings::kDetailMax);
        ImGui::SetItemTooltip("Small ripples on top of the waves. 0 turns them off and skips their cost.");
        changed |= ImGui::SliderFloat("Ripple scale", &s.detailScale, WaterSettings::kDetailScaleMin, WaterSettings::kDetailScaleMax,
                                      "%.2f", ImGuiSliderFlags_Logarithmic);
        changed |= ImGui::SliderFloat("Ripple distance", &s.detailDistance, WaterSettings::kDetailDistanceMin,
                                      WaterSettings::kDetailDistanceMax, "%.0f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("Ripples fade out by this distance (also hides shimmer on far water).");
    }

    if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Reflection strength", &s.reflectivity, 0.0f, 1.0f);
        static const char* const kReflectionNames[] = { "Sky only (cubemap)", "Planar (half resolution)",
                                                        "Planar (full resolution)", "Planar (quarter resolution)" };
        // The enum's values are saved in scene files, so they cannot be reordered: show them cheapest first instead.
        static const ReflectionMode kReflectionOrder[] = { ReflectionMode::Sky, ReflectionMode::PlanarQuarter,
                                                           ReflectionMode::PlanarHalf, ReflectionMode::PlanarFull };
        static const char* const kReflectionLabels[] = { kReflectionNames[0], kReflectionNames[3], kReflectionNames[1], kReflectionNames[2] };
        int reflectionIndex = 0;
        for (int i = 0; i < 4; ++i) if (kReflectionOrder[i] == s.reflectionMode) reflectionIndex = i;
        if (ImGui::Combo("Reflection", &reflectionIndex, kReflectionLabels, IM_ARRAYSIZE(kReflectionLabels))) {
            s.reflectionMode = kReflectionOrder[reflectionIndex];
            changed = true;
        }
        ImGui::SetItemTooltip("Planar modes draw the scene a second time from below the water, so the water\nreflects the terrain and cube too. That pass is skipped while no water is on screen.\nBodies at the same level share one image; at most 2 planar images are drawn per frame (nearest bodies first),\nthe others use the sky. Quarter resolution is the cheapest; the waves blur it anyway.");
        if (s.reflectionMode != ReflectionMode::Sky) {
            changed |= ImGui::SliderFloat("Mirror distortion", &s.reflectionDistortion, 0.0f, WaterSettings::kDistortionMax, "%.3f");
        }
        changed |= ImGui::SliderFloat("Refraction", &s.refraction, 0.0f, WaterSettings::kRefractionMax, "%.3f");
        ImGui::SetItemTooltip("How much the waves bend the view of the bottom. 0 = off, and the screen copy it needs is skipped.");
        changed |= ImGui::SliderFloat("Crest glow", &s.scatter, 0.0f, WaterSettings::kScatterMax);
        ImGui::SetItemTooltip("Light shining through wave crests when you look toward the sun.");
        changed |= ImGui::SliderFloat("Sun glint", &s.glintStrength, 0.0f, WaterSettings::kGlintMax);
        changed |= ImGui::SliderFloat("Glint sharpness", &s.glintSharpness, WaterSettings::kSharpnessMin,
                                      WaterSettings::kSharpnessMax, "%.0f", ImGuiSliderFlags_Logarithmic);
    }

    if (ImGui::CollapsingHeader("Foam")) {
        changed |= ImGui::SliderFloat("Shore width", &s.foamDistance, 0.0f, WaterSettings::kFoamDistanceMax, "%.2f");
        changed |= ImGui::SliderFloat("Shore strength", &s.foamStrength, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Crest foam", &s.crestFoam, 0.0f, 1.0f);
        ImGui::SetItemTooltip("White caps where a wave is squeezed to its sharpest. Needs Choppiness above 0.");
        changed |= ImGui::SliderFloat("Crest threshold", &s.crestFoamThreshold, 0.0f, 1.0f);
        ImGui::SetItemTooltip("Higher = only the very sharpest crests get foam.");
    }

    if (ImGui::CollapsingHeader("Shore")) {
        changed |= ImGui::SliderFloat("Shore waves", &s.shoreWaves, 0.0f, 1.0f);
        ImGui::SetItemTooltip("How much the waves react to the ground under them. In shallow water they grow taller,\nthen cannot be taller than the water is deep, so they break and die at the waterline.\n0 = identical everywhere (the old look).");
        changed |= ImGui::SliderFloat("Swash", &s.swash, 0.0f, WaterSettings::kSwashMax, "%.2f");
        ImGui::SetItemTooltip("How far the waterline (foam and the see-through edge) slides up and down the beach with each wave.");
        int mapWidth = 0, mapHeight = 0;
        water->GetShoreMapSize(mapWidth, mapHeight);
        if (water->HasShoreDepth()) ImGui::TextDisabled("Depth map: %d x %d texels. See Advanced > Debug > Shore depth.", mapWidth, mapHeight);
        else ImGui::TextDisabled("No terrain under this body: the shore effects are off.");
    }

    // The things you set once and rarely touch again live together, folded away.
    if (ImGui::CollapsingHeader("Advanced")) {
        if (ImGui::TreeNode("Performance")) {
            static const char* const kMeshNames[] = { "Low (16 cells per tile)", "Medium (32 cells per tile)", "High (64 cells per tile)" };
            changed |= ImGui::Combo("Mesh detail", &s.meshDetail, kMeshNames, IM_ARRAYSIZE(kMeshNames));
            ImGui::SetItemTooltip("Grid cells along one tile edge at the finest level. The water is cut into square tiles:\n8 x 8 for a square body, long rows for a river.");
            changed |= ImGui::SliderFloat("Full detail until", &s.lodDistance, WaterSettings::kLodDistanceMin,
                                          WaterSettings::kLodDistanceMax, "%.0f", ImGuiSliderFlags_Logarithmic);
            ImGui::SetItemTooltip("Tiles farther than this use fewer triangles. 4 levels, switching at 1x, 2.2x and 4.5x this distance.");
            changed |= ImGui::Checkbox("Skip tiles off screen", &s.frustumCulling);
            const WaterViewStats& stats = water->GetStats(ViewKind::Main);
            ImGui::TextDisabled("Camera: %d/%d tiles  LOD %d/%d/%d/%d  %d tris", stats.tilesDrawn, stats.tilesTotal,
                                stats.tilesPerLod[0], stats.tilesPerLod[1], stats.tilesPerLod[2], stats.tilesPerLod[3],
                                stats.trianglesDrawn);
            if (s.reflectionMode != ReflectionMode::Sky) {
                if (stats.tilesDrawn == 0) ImGui::TextDisabled("Mirror: not on screen");
                else if (stats.reflectionSlot >= 0) ImGui::TextDisabled("Mirror: planar image %d (bodies at this level share it)", stats.reflectionSlot + 1);
                else ImGui::TextDisabled("Mirror: sky only (over the budget of %d planar images, or camera below)", WaterFramePlan::kMaxReflections);
            }
            if (stats.tilesDry > 0) ImGui::TextDisabled("%d tiles skipped: under dry ground", stats.tilesDry);
            if (gpuTimings_.valid) {
                using P = GpuPass;
                const float* ms = gpuTimings_.ms;
                ImGui::TextDisabled("GPU ms: shadows %.2f  mirrors %.2f  opaque %.2f  water %.2f  post %.2f  (frame %.2f)",
                                    ms[static_cast<int>(P::Shadows)], ms[static_cast<int>(P::Reflections)], ms[static_cast<int>(P::Opaque)],
                                    ms[static_cast<int>(P::Water)], ms[static_cast<int>(P::PostProcess)], ms[static_cast<int>(P::Total)]);
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Debug")) {
            static const char* const kDebugNames[] = { "Off", "Normals", "Depth (shore / deep)", "Foam", "Fresnel", "Crest height", "Shore depth (red land, green depth, blue surf)" };
            int debugView = static_cast<int>(s.debugView);
            if (ImGui::Combo("View", &debugView, kDebugNames, IM_ARRAYSIZE(kDebugNames))) {
                s.debugView = static_cast<WaterDebugView>(debugView);
                changed = true;
            }
            ImGui::SetItemTooltip("Shows one ingredient of the water on its own. Not saved with the scene.");
            changed |= ImGui::Checkbox("Wireframe", &s.wireframe);
            ImGui::SetItemTooltip("Draws the water mesh as wires so you can SEE the tiles and their detail levels.");
            ImGui::TreePop();
        }
        ImGui::Separator();
        if (ImGui::Button("Reset to type defaults")) {   // this type's starting values; keeps level, size, shape and the debug switches
            WaterSettings defaults;
            defaults.height = s.height;
            defaults.size = s.size;
            defaults.sizeZ = s.sizeZ;
            defaults.shape = s.shape;
            defaults.debugView = s.debugView;
            defaults.wireframe = s.wireframe;
            defaults.ApplyBodyType(s.bodyType);
            s = defaults;
            changed = true;
        }
    }

    if (changed) water->SetSettings(s);
}

void EditorLayer::DrawSkyboxInspector(Skybox* skybox)
{
    ImGui::SeparatorText("Skybox material");
    float intensity = skybox->GetIntensity();
    if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 3.0f)) skybox->SetIntensity(intensity);
    ImGui::Text("Cubemap: assets/Skybox");
}
