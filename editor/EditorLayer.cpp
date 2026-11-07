#include "EditorLayer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include "Cube.h"
#include "ModelImporter.h"
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

    if (!ImGui_ImplWin32_Init(hwnd) || !ImGui_ImplDX11_Init(renderer.GetDevice(), renderer.GetContext())) {
        LOG_ERROR("Failed to initialize Dear ImGui");
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    hwnd_ = hwnd;
    attached_ = true;
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

    // Import BEFORE the UI frame starts: the file dialog blocks and pumps messages, which must not happen
    // between NewFrame and Render.
    if (importRequested_) {
        importRequested_ = false;
        ImportModelWithDialog(ctx);
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
    const ImGuiID left = ImGui::DockBuilderSplitNode(middle, ImGuiDir_Left, 0.20f, nullptr, &middle);
    const ImGuiID right = ImGui::DockBuilderSplitNode(middle, ImGuiDir_Right, 0.28f, nullptr, &middle);
    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Viewport", middle);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderFinish(dockSpaceId);
}

void EditorLayer::DrawMenuBar()
{
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open Scene...")) openSceneRequested_ = true;
        if (ImGui::MenuItem("Save Scene", "Ctrl+S")) saveSceneRequested_ = true;
        if (ImGui::MenuItem("Save Scene As...")) saveSceneAsRequested_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Import Model...")) importRequested_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) PostQuitMessage(0);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        const SceneObject* selected = SelectedObject();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, selected && selected->CanClone())) duplicateRequested_ = true;
        if (ImGui::MenuItem("Delete", "Del", false, selected && selected->IsUserContent())) deleteRequested_ = true;
        if (ImGui::MenuItem("Frame selected", "F", false, selected != nullptr)) FrameSelected();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Add")) {
        if (ImGui::MenuItem("Cube")) addCubeRequested_ = true;
        if (ImGui::MenuItem("Model from file...")) importRequested_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy_);
        ImGui::MenuItem("Viewport", nullptr, &showViewport_);
        ImGui::MenuItem("Inspector", nullptr, &showInspector_);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset layout")) resetLayout_ = true;
        ImGui::EndMenu();
    }
    // Which scene is open, and a short confirmation after a save.
    const std::string name = currentScenePath_.empty() ? "unsaved scene" : std::filesystem::path(currentScenePath_).filename().string();
    ImGui::TextDisabled("  |  %s", name.c_str());
    if (ImGui::GetTime() - lastSavedTime_ < 2.0) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "Saved");
    }
    ImGui::EndMenuBar();
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
    ImGui::TextDisabled("Hold Ctrl to snap  |  F frame  |  Del delete  |  Ctrl+D duplicate");
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
        if (selected->GetEditorTransform(position, rotation, scale)) {
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
    if (viewportHovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
        const ImVec2 mouse = ImGui::GetMousePos();
        const float ndcX = ((mouse.x - x) / width) * 2.0f - 1.0f;
        const float ndcY = 1.0f - ((mouse.y - y) / height) * 2.0f;   // screen y grows downward, ndc y grows upward
        const SceneObjectId hit = PickObject(MakePickRay(view, projection, ndcX, ndcY));
        if (hit != 0) { selectionKind_ = SelectionKind::Object; selectedId_ = hit; }
        else selectedId_ = 0;
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
        }
    }
    if (duplicateRequested_) {
        duplicateRequested_ = false;
        if (SceneObject* copy = ctx.scene.Duplicate(selectedId_)) Select(copy);
    }
    if (deleteRequested_) {
        deleteRequested_ = false;
        const SceneObject* selected = SelectedObject();
        if (selected && selected->IsUserContent()) {
            ctx.scene.Remove(selectedId_);
            selectedId_ = 0;
        }
    }
}

void EditorLayer::HandleShortcuts()
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;   // typing a number into a slider: "1" must not switch tools
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) saveSceneRequested_ = true;

    const bool inViewport = viewportHovered_ || viewportFocused_;
    if (inViewport) {
        if (ImGui::IsKeyPressed(ImGuiKey_1, false)) gizmoTool_ = GizmoTool::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_2, false)) gizmoTool_ = GizmoTool::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_3, false)) gizmoTool_ = GizmoTool::Scale;
        if (ImGui::IsKeyPressed(ImGuiKey_G, false)) gizmoLocal_ = !gizmoLocal_;
    }
    if (inViewport || hierarchyFocused_) {
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) FrameSelected();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) selectedId_ = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) deleteRequested_ = true;
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateRequested_ = true;
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
            if (ImGui::MenuItem("Delete", "Del", false, object->IsUserContent())) deleteRequested_ = true;
            if (ImGui::MenuItem("Frame", "F")) FrameSelected();
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    // Right-click on the empty part of the panel.
    if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Add cube")) addCubeRequested_ = true;
        if (ImGui::MenuItem("Add model from file...")) importRequested_ = true;
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
bool IsInsideAssets(const std::string& path)
{
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::weakly_canonical(path, error).lexically_relative(std::filesystem::weakly_canonical("assets", error));
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
    else if (request.kind == 3) ok = terrain->SetColorMap(path);

    if (!ok) {
        ShowMessage("Could not use that image", "The image could not be read (or the terrain could not be rebuilt with it):\n" + path);
    } else if (!IsInsideAssets(path)) {
        ShowMessage("Image applied, with a warning", "That image is outside the assets folder, so it will not be shipped with the game.\nCopy it into assets/ (for example assets/Terrain/<Layer>/) and choose it again from there.");
    }
}

namespace {
// Open / Save dialog for scene files. Returns UTF-8, empty if cancelled. For saving, makes sure the name ends in
// ".scene.json" so the Open dialog's filter finds it again.
std::string PickSceneFile(HWND owner, bool save)
{
    std::error_code error;
    std::filesystem::create_directories("assets/scenes", error);   // the dialog starts here
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"Scene files (*.scene.json)\0*.scene.json\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrInitialDir = L"assets\\scenes";
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

void EditorLayer::HandleSceneFileRequests(LayerContext& ctx)
{
    if (openSceneRequested_) {
        openSceneRequested_ = false;
        const std::string path = PickSceneFile(hwnd_, false);
        if (!path.empty()) {
            std::vector<std::string> warnings;
            std::string error;
            if (LoadScene(ctx.scene, ctx.renderer.GetDevice(), path, warnings, error)) {
                currentScenePath_ = path;
                selectedId_ = 0;                       // the old objects are gone, and so are their ids
                selectionKind_ = SelectionKind::Object;
                if (!warnings.empty()) ShowMessage("Scene opened, with warnings", JoinLines(warnings));
            } else {
                ShowMessage("Could not open the scene", error);
            }
        }
    }

    if (saveSceneRequested_ || saveSceneAsRequested_) {
        const bool askForName = saveSceneAsRequested_ || currentScenePath_.empty();
        saveSceneRequested_ = false;
        saveSceneAsRequested_ = false;
        const std::string path = askForName ? PickSceneFile(hwnd_, true) : currentScenePath_;
        if (!path.empty()) {
            std::vector<std::string> warnings;
            std::string error;
            if (SaveScene(ctx.scene, path, warnings, error)) {
                currentScenePath_ = path;
                lastSavedTime_ = ImGui::GetTime();
                if (!warnings.empty()) ShowMessage("Scene saved, with warnings", JoinLines(warnings));
            } else {
                ShowMessage("Could not save the scene", error);
            }
        }
    }
}

void EditorLayer::ImportModelWithDialog(LayerContext& ctx)
{
    const std::string path = PickModelFile(hwnd_);
    if (path.empty()) return;

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
}

void EditorLayer::DrawModelInspector(ModelObject* model)
{
    ImGui::SeparatorText("Model");
    ImGui::TextDisabled("%s", model->GetSourcePath().c_str());
    ImGui::Text("%zu parts, %zu materials, %u triangles", model->GetPartCount(), model->GetMaterialCount(), model->GetTriangleCount());
    if (ImGui::Button("Fit to 10 units")) model->FitToSize(10.0f);

    for (size_t i = 0; i < model->GetMaterialCount(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::TreeNode(model->GetMaterialName(i).c_str())) {
            DrawSurfaceInspector(model->GetMaterial(i));
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
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
    if (ImGui::DragFloat3("Rotation", rotationValues, 1.0f)) {
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

void EditorLayer::DrawTerrainInspector(Terrain* terrain)
{
    // Mesh-shaping values rebuild ~half a million vertices, so they are applied when you
    // RELEASE the control, not on every frame of a drag. While a control is being edited we keep
    // our own copy; otherwise we follow the terrain so changes made elsewhere show up.
    if (!ImGui::IsAnyItemActive()) pendingTerrainSettings_ = terrain->GetSettings();
    TerrainSettings& s = pendingTerrainSettings_;

    bool commit = false;
    auto commitOnRelease = [&commit] { if (ImGui::IsItemDeactivatedAfterEdit()) commit = true; };

    ImGui::SeparatorText("Shape");
    ImGui::SliderFloat("Width", &s.width, TerrainSettings::kWidthMin, TerrainSettings::kWidthMax, "%.0f");
    commitOnRelease();
    ImGui::SliderFloat("Height scale", &s.heightScale, TerrainSettings::kHeightMin, TerrainSettings::kHeightMax, "%.1f");
    commitOnRelease();
    ImGui::SliderInt("Mesh resolution", &s.maxCells, TerrainSettings::kCellsMin, TerrainSettings::kCellsMax);
    commitOnRelease();
    ImGui::SetItemTooltip("Grid cells across the terrain. Higher = smoother and slower to rebuild.");

    ImGui::SeparatorText("Noise");
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
        ImGui::TextUnformatted("Heightmap");
        ImGui::SameLine(90.0f);
        const int heightResult = fileButton("heightmap", terrain->GetHeightmapPath(), Terrain::DefaultHeightmapPath(), 160.0f);
        if (heightResult == 1) terrainFileRequest_ = { 2, 0, 0 };
        else if (heightResult == 2) terrain->SetHeightmap("");
        ImGui::SetItemTooltip("%s\nA 16-bit greyscale image gives much smoother slopes than 8-bit.", terrain->GetHeightmapPath().c_str());
        ImGui::TextUnformatted("Colour map");
        ImGui::SameLine(90.0f);
        const int colorResult = fileButton("colormap", terrain->GetColorMapPath(), Terrain::DefaultColorMapPath(), 160.0f);
        if (colorResult == 1) terrainFileRequest_ = { 3, 0, 0 };
        else if (colorResult == 2) terrain->SetColorMap("");
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
    layersChanged |= ImGui::SliderFloat("Colour map influence", &s.macroInfluence, 0.0f, 1.0f, "%.2f");
    ImGui::SetItemTooltip("How much the terrain-wide colour map (Diffuse Map.png) tints the layer textures.\n0 = pure layer textures, 1 = full large-scale colour variation.");
    layersChanged |= ImGui::SliderFloat("Height blend", &s.heightBlend, 0.0f, 1.0f, "%.2f");
    ImGui::SetItemTooltip("0 = layers fade into each other like a double exposure.\nHigher = the taller bumps of each layer win, so stones poke through sand.");
    layersChanged |= ImGui::SliderFloat("Normal strength", &s.normalStrength, 0.0f, 3.0f, "%.2f");
    layersChanged |= ImGui::Checkbox("Flip green (OpenGL maps)", &s.flipGreen);
    layersChanged |= ImGui::SliderFloat("AO strength", &s.aoStrength, 0.0f, 1.0f, "%.2f");
    layersChanged |= ImGui::SliderFloat("Roughness scale", &s.roughnessScale, 0.0f, 2.0f, "%.2f");
    if (ImGui::Button("Use original colours")) {
        const XMFLOAT3 white(1.0f, 1.0f, 1.0f);
        s.groundTint = white; s.rockTint = white; s.shoreColor = white; s.snowColor = white;
        s.macroInfluence = 0.0f;
        layersChanged = true;
    }
    ImGui::SetItemTooltip("Tints MULTIPLY the layer textures, and the colour map blends its own colours in.\nThis turns both off, so every layer shows exactly the colours of its image.");
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
    // no Apply button. We read the current settings, edit them, and write back if anything moved.
    WaterSettings s = water->GetSettings();
    bool changed = false;

    ImGui::SeparatorText("Surface");
    changed |= ImGui::SliderFloat("Level", &s.height, WaterSettings::kHeightMin, WaterSettings::kHeightMax, "%.1f");
    changed |= ImGui::SliderFloat("Size", &s.size, WaterSettings::kSizeMin, WaterSettings::kSizeMax, "%.0f");
    if (Terrain* terrain = scene_->GetTerrain()) {
        if (ImGui::Button("Fit size to terrain")) {
            const DirectX::XMFLOAT2 extent = terrain->GetWorldExtent();
            s.size = (std::max)(extent.x, extent.y) * 1.2f;   // a little larger: no gap at the shore
            changed = true;
        }
        float minY = 0.0f, maxY = 0.0f;
        terrain->GetHeightRange(minY, maxY);
        ImGui::SameLine();
        ImGui::TextDisabled("terrain Y %.1f to %.1f", minY, maxY);
    }
    float color[3] = { s.color.x, s.color.y, s.color.z };
    if (ImGui::ColorEdit3("Deep color", color)) {
        s.color = { color[0], color[1], color[2] };
        changed = true;
    }
    changed |= ImGui::SliderFloat("Opacity", &s.alpha, 0.05f, 1.0f);

    ImGui::SeparatorText("Depth and shoreline");
    float shallow[3] = { s.shallowColor.x, s.shallowColor.y, s.shallowColor.z };
    if (ImGui::ColorEdit3("Shallow color", shallow)) {
        s.shallowColor = { shallow[0], shallow[1], shallow[2] };
        changed = true;
    }
    changed |= ImGui::SliderFloat("Absorption", &s.absorption, WaterSettings::kAbsorptionMin,
                                  WaterSettings::kAbsorptionMax, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::SetItemTooltip("How quickly the colour goes from shallow to deep as the water gets thicker.");
    changed |= ImGui::SliderFloat("Shore softness", &s.fadeDistance, WaterSettings::kFadeMin,
                                  WaterSettings::kFadeMax, "%.1f");
    ImGui::SetItemTooltip("Depth at which the water becomes fully opaque. Smaller = sharper shoreline.");
    changed |= ImGui::SliderFloat("Foam width", &s.foamDistance, 0.0f, WaterSettings::kFoamDistanceMax, "%.2f");
    changed |= ImGui::SliderFloat("Foam strength", &s.foamStrength, 0.0f, 1.0f);

    ImGui::SeparatorText("Waves");
    if (ImGui::Button("Calm"))   { s.ApplyWavePreset(WavePreset::Calm);   changed = true; }
    ImGui::SameLine();
    if (ImGui::Button("Choppy")) { s.ApplyWavePreset(WavePreset::Choppy); changed = true; }
    ImGui::SameLine();
    if (ImGui::Button("Stormy")) { s.ApplyWavePreset(WavePreset::Stormy); changed = true; }
    changed |= ImGui::SliderFloat("Amplitude", &s.waveAmplitude, 0.0f, WaterSettings::kAmplitudeMax);
    changed |= ImGui::SliderFloat("Speed", &s.waveSpeed, 0.0f, WaterSettings::kSpeedMax);
    changed |= ImGui::SliderFloat("Frequency", &s.waveFrequency, WaterSettings::kFrequencyMin, WaterSettings::kFrequencyMax);
    ImGui::SetItemTooltip("Radians per world unit. Lower = longer, lazier waves.");

    ImGui::SeparatorText("Lighting");
    changed |= ImGui::SliderFloat("Reflection strength", &s.reflectivity, 0.0f, 1.0f);
    static const char* const kReflectionNames[] = { "Sky only (cubemap)", "Planar (half resolution)", "Planar (full resolution)" };
    int reflectionMode = static_cast<int>(s.reflectionMode);
    if (ImGui::Combo("Reflection", &reflectionMode, kReflectionNames, IM_ARRAYSIZE(kReflectionNames))) {
        s.reflectionMode = static_cast<ReflectionMode>(reflectionMode);
        changed = true;
    }
    ImGui::SetItemTooltip("Planar modes draw the scene a second time from below the water, so the water\nreflects the terrain and cube too. Costs roughly one extra scene draw.");
    if (s.reflectionMode != ReflectionMode::Sky) {
        changed |= ImGui::SliderFloat("Mirror distortion", &s.reflectionDistortion, 0.0f, WaterSettings::kDistortionMax, "%.3f");
    }
    changed |= ImGui::SliderFloat("Sun glint", &s.glintStrength, 0.0f, WaterSettings::kGlintMax);
    changed |= ImGui::SliderFloat("Glint sharpness", &s.glintSharpness, WaterSettings::kSharpnessMin,
                                  WaterSettings::kSharpnessMax, "%.0f", ImGuiSliderFlags_Logarithmic);

    ImGui::Separator();
    if (ImGui::Button("Reset water look")) {     // keeps level and size, resets everything else
        WaterSettings defaults;
        defaults.height = s.height;
        defaults.size = s.size;
        s = defaults;
        changed = true;
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
