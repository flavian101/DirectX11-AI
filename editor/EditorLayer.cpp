#include "EditorLayer.h"
#include "WaterSelect.h"
#include "WaterTypeUi.h"
#include "EditorTheme.h"
#include "EditorIcons.h"
#include "SkyFaces.h"
#include "FileDialogs.h"
#include <algorithm>
#include <cctype>
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
#include "TerrainWorld.h"
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
// An old-style terrain (not a tile of a terrain world), if the scene has one: the brush works on it unless the world is selected.
Terrain* FindSingleTerrain(Scene& scene)
{
    for (const auto& object : scene.GetObjects()) {
        if (auto* terrain = dynamic_cast<Terrain*>(object.get()); terrain && !terrain->IsTile()) return terrain;
    }
    return nullptr;
}
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
// A world point on the screen. False when it is behind the camera (the divide would put it on the wrong side).
bool ProjectPoint(const XMFLOAT3& point, const XMFLOAT4X4& view, const XMFLOAT4X4& projection, float x, float y, float width, float height, ImVec2& out)
{
    const XMVECTOR clip = XMVector4Transform(XMVectorSet(point.x, point.y, point.z, 1.0f), XMLoadFloat4x4(&view) * XMLoadFloat4x4(&projection));
    const float w = XMVectorGetW(clip);
    if (w <= 0.01f) return false;
    out = ImVec2(x + (XMVectorGetX(clip) / w * 0.5f + 0.5f) * width, y + (0.5f - XMVectorGetY(clip) / w * 0.5f) * height);
    return true;
}

// A warm, strong directional light is a sun; a pale blue one is a moon; a point light is a bulb.
EditorIcons::Icon LightIconFor(const LightObject& light)
{
    if (!light.IsDirectional()) return EditorIcons::Icon::Bulb;
    return light.GetColor().z > light.GetColor().x ? EditorIcons::Icon::Moon : EditorIcons::Icon::Sun;
}

// The lights have no body to see, so each gets a marker in the viewport: its icon in its own colour. The selected one also
// shows what it does: a directional light the way it shines (an arrow), a point light how far it reaches (three circles).
void DrawLightMarkers(ImDrawList* list, const Scene& scene, SceneObjectId selectedId, const XMFLOAT4X4& view, const XMFLOAT4X4& projection,
                      float x, float y, float width, float height)
{
    list->PushClipRect(ImVec2(x, y), ImVec2(x + width, y + height), true);
    for (const auto& object : scene.GetObjects()) {
        const auto* light = dynamic_cast<const LightObject*>(object.get());
        if (!light) continue;
        ImVec2 center;
        if (!ProjectPoint(light->GetPosition(), view, projection, x, y, width, height, center)) continue;
        const bool selected = light->GetId() == selectedId;
        const Scene::EffectiveLight shining = scene.GetEffectiveLight(*light);   // follows the clock for a sun or moon
        const XMFLOAT3 c = shining.color;
        auto channel = [](float v) { return static_cast<int>((0.35f + 0.65f * v) * 255.0f); };   // pulled toward white so a dark colour still shows
        const ImU32 tint = IM_COL32(channel(c.x), channel(c.y), channel(c.z), 255);
        const float radius = 14.0f;
        list->AddCircleFilled(center, radius, IM_COL32(18, 18, 22, 175));
        list->AddCircle(center, radius, selected ? IM_COL32(255, 170, 40, 255) : IM_COL32(255, 255, 255, 80), 0, selected ? 2.0f : 1.0f);
        EditorIcons::Draw(list, LightIconFor(*light), center, radius * 1.3f, tint);
        if (!selected) continue;

        if (light->IsDirectional()) {
            const XMFLOAT3 d = shining.direction;
            const XMFLOAT3 p = light->GetPosition();
            ImVec2 tip;
            if (ProjectPoint({ p.x + d.x * 8.0f, p.y + d.y * 8.0f, p.z + d.z * 8.0f }, view, projection, x, y, width, height, tip)) {
                list->AddLine(center, tip, tint, 2.0f);
                const ImVec2 back(center.x - tip.x, center.y - tip.y);
                const float length = std::sqrt(back.x * back.x + back.y * back.y);
                if (length > 1.0f) {   // an arrow head: two short strokes back along the line
                    const ImVec2 u(back.x / length * 9.0f, back.y / length * 9.0f);
                    list->AddLine(tip, ImVec2(tip.x + u.x - u.y * 0.5f, tip.y + u.y + u.x * 0.5f), tint, 2.0f);
                    list->AddLine(tip, ImVec2(tip.x + u.x + u.y * 0.5f, tip.y + u.y - u.x * 0.5f), tint, 2.0f);
                }
            }
        } else {
            const XMFLOAT3 p = light->GetPosition();
            const float range = light->GetRange();
            constexpr int kSegments = 40;
            for (int plane = 0; plane < 3; ++plane) {   // a circle around each axis: together they read as a sphere
                ImVec2 previous;
                bool havePrevious = false;
                for (int i = 0; i <= kSegments; ++i) {
                    const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(kSegments);
                    const float ca = std::cos(a) * range, sa = std::sin(a) * range;
                    const XMFLOAT3 world = plane == 0 ? XMFLOAT3(p.x + ca, p.y + sa, p.z) : plane == 1 ? XMFLOAT3(p.x + ca, p.y, p.z + sa) : XMFLOAT3(p.x, p.y + ca, p.z + sa);
                    ImVec2 screen;
                    const bool ok = ProjectPoint(world, view, projection, x, y, width, height, screen);
                    if (ok && havePrevious) list->AddLine(previous, screen, IM_COL32(255, 200, 90, 110), 1.0f);
                    previous = screen;
                    havePrevious = ok;
                }
            }
        }
    }
    list->PopClipRect();
}

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

// A selected water body (lesson 42). A box would be wrong for it: a river is a ribbon across a much bigger rectangle, a pond
// is round. So: the edge of the plane (a rectangle or an ellipse, at the water's level), with a light tint inside it for a
// body that fills its plane; for a river that is a curve, the two edges of the WATER bright and the plane's edge faint.
void DrawWaterSelection(ImDrawList* list, const Water& water, const XMFLOAT4X4& view, const XMFLOAT4X4& projection,
                        float x, float y, float width, float height, ImU32 color)
{
    const WaterSettings& settings = water.GetSettings();
    XMFLOAT3 position, rotation, scale;
    water.GetEditorTransform(position, rotation, scale);
    const bool curve = settings.HasRiverPath() && water.GetRiverWorldPath().size() >= 2;
    const ImU32 faint = (color & 0x00FFFFFFu) | (90u << 24);
    const ImU32 tint = (color & 0x00FFFFFFu) | (30u << 24);

    const auto draw = [&](const std::vector<XMFLOAT3>& world, bool closed, ImU32 lineColor, float thickness, bool fill) {
        std::vector<ImVec2> screen(world.size());
        std::vector<char> ok(world.size());
        bool all = true;
        for (size_t i = 0; i < world.size(); ++i) {
            ok[i] = ProjectPoint(world[i], view, projection, x, y, width, height, screen[i]) ? 1 : 0;
            all = all && ok[i];
        }
        if (fill && all && screen.size() >= 3) {
            float area = 0.0f;   // ImGui wants a convex fill wound one way: turn it round if the camera sees it the other way
            for (size_t i = 0; i < screen.size(); ++i) {
                const ImVec2& a = screen[i];
                const ImVec2& b = screen[(i + 1) % screen.size()];
                area += a.x * b.y - b.x * a.y;
            }
            if (area < 0.0f) std::reverse(screen.begin(), screen.end());
            list->AddConvexPolyFilled(screen.data(), static_cast<int>(screen.size()), tint);
            if (area < 0.0f) std::reverse(screen.begin(), screen.end());
        }
        const size_t count = world.size();
        for (size_t i = 0; i + (closed ? 0 : 1) < count; ++i) {
            const size_t j = (i + 1) % count;
            if (ok[i] && ok[j]) list->AddLine(screen[i], screen[j], lineColor, thickness);
        }
    };

    // The plane's edge, at the water's level.
    const std::vector<WaterSelect::Spot> outline = WaterSelect::PlaneOutline(
        settings.shape == WaterShape::Ellipse, 0.5f * settings.size * std::fabs(scale.x), 0.5f * settings.sizeZ * std::fabs(scale.z));
    std::vector<XMFLOAT3> edge;
    for (const WaterSelect::Spot& spot : outline) {
        const XMFLOAT2 w = water.BodyToWorld(spot.x, spot.z);
        edge.push_back({ w.x, position.y, w.y });
    }
    draw(edge, true, curve ? faint : color, curve ? 1.0f : 1.5f, !curve);

    if (curve) {   // the river's water: the two banks
        std::vector<WaterSelect::BankPoint> left, right;
        WaterSelect::RiverBanks(water.GetRiverWorldPath(), (std::max)(1, static_cast<int>(water.GetRiverWorldPath().size() / 200)), left, right);
        for (const std::vector<WaterSelect::BankPoint>* bank : { &left, &right }) {
            std::vector<XMFLOAT3> line;
            for (const WaterSelect::BankPoint& p : *bank) line.push_back({ p.x, position.y + p.level + 0.05f, p.z });
            draw(line, false, color, 1.8f, false);
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
    ApplyEditorFont(ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd));   // Segoe UI, and bigger spacing on a high-DPI screen

    if (!ImGui_ImplWin32_Init(hwnd) || !ImGui_ImplDX11_Init(renderer.GetDevice(), renderer.GetContext())) {
        LOG_ERROR("Failed to initialize Dear ImGui");
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    hwnd_ = hwnd;
    attached_ = true;
    assetBrowser_.SetDevice(renderer.GetDevice());   // the Assets panel makes its thumbnails with it
    LoadRecentProjects();
    return true;
}

void EditorLayer::OnDetach()
{
    if (!attached_) return;   // safe to call twice (SetLayer, then the destructor)
    assetBrowser_.Release();  // its thumbnails are GPU textures: they must go before the device does
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
    if (!pendingSkyboxFolder_.empty()) {   // "Use as skybox" in the Assets panel, or a folder dropped on the sky picker
        const std::string folder = pendingSkyboxFolder_;
        pendingSkyboxFolder_.clear();
        UseSkyboxFolder(ctx, folder);
    }
    if (!pendingAddSkyFolder_.empty()) {   // "Add as new sky"
        const std::string folder = pendingAddSkyFolder_;
        pendingAddSkyFolder_.clear();
        AddSkybox(ctx, folder);
    }
    if (pendingActiveSkyId_ != 0) {        // "Use this sky"
        const SceneObjectId id = pendingActiveSkyId_;
        pendingActiveSkyId_ = 0;
        ActivateSky(dynamic_cast<Skybox*>(ctx.scene.Find(id)));
    }
    if (newProjectRequested_) {
        newProjectRequested_ = false;
        NewProject(ctx);
    }
    HandleSceneFileRequests(ctx);   // File > Open / Save, also before the UI frame starts
    HandleTerrainFileRequest(ctx);  // Terrain > Texture files
    HandleWorldPictureRequest(ctx); // Terrain world > Picture
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
    DrawSceneSettingsPanel();
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
    // The dock space fills what is left of the host window, minus a strip at the bottom for the status bar.
    const float statusHeight = ImGui::GetFrameHeight();
    ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, ImGui::GetContentRegionAvail().y - statusHeight), ImGuiDockNodeFlags_None);
    DrawStatusBar(statusHeight);
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
    ImGui::DockBuilderDockWindow("Scene Settings", right);   // a tab next to the Inspector
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
        if (ImGui::BeginMenu("Light")) {
            DrawAddLightItems();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Water body")) {
            DrawAddWaterItems();
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Model from file...")) importRequested_ = true;
        if (ImGui::MenuItem("Terrain world (streaming tiles)", nullptr, false, scene_ && !scene_->FindFirst<TerrainWorld>())) addTerrainWorldRequested_ = true;
        ImGui::SetItemTooltip("A world of 1 km terrain tiles whose heights come from one world function, so the tiles meet without seams.\nTiles load and unload around the camera. Select the world in the Hierarchy to change its seed, hills,\nheight and view distance. Sculpt and paint with the brush (4): strokes are stored in the world and cross tile borders.");
        {
            const Terrain* single = scene_ ? scene_->GetTerrain() : nullptr;
            const bool canRebuild = scene_ && single && !single->IsTile() && !scene_->FindFirst<TerrainWorld>();
            if (ImGui::MenuItem("Terrain world from this terrain (migrate)", nullptr, false, canRebuild)) addWorldFromTerrainRequested_ = true;
            ImGui::SetItemTooltip("Replaces the terrain with a small BOUNDED world standing where it stands: one tile as wide as the terrain, with the same\nheightmap, height, cell size, look and layer textures. Sculpting, noise and painting are carried over as the world's edits.\nThe old terrain stays in the scene until you delete it; rivers carved into it are part of its heights and come along.");
        }
        if (ImGui::MenuItem("Scatter on terrain (trees, rocks)", nullptr, false, scene_ && scene_->GetTerrain())) addScatterRequested_ = true;
        ImGui::SetItemTooltip("Grows trees and rocks over the terrain by rules (which layer, how steep, how high),\ndrawn with instancing, frustum culling and levels of detail. Needs a terrain.");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy_);
        ImGui::MenuItem("Viewport", nullptr, &showViewport_);
        ImGui::MenuItem("Inspector", nullptr, &showInspector_);
        ImGui::MenuItem("Scene Settings", nullptr, &showSceneSettings_);
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

void EditorLayer::DrawStatusBar(float height)
{
    // Drawn straight onto the host window with absolute coordinates, so it needs no layout and cannot disturb the dock space.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 min(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - height);
    const ImVec2 max(viewport->WorkPos.x + viewport->WorkSize.x, viewport->WorkPos.y + viewport->WorkSize.y);
    ImDrawList* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_MenuBarBg));
    list->AddLine(min, ImVec2(max.x, min.y), ImGui::GetColorU32(ImGuiCol_Border));

    const float textY = min.y + (height - ImGui::GetTextLineHeight()) * 0.5f;
    const ImU32 normal = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    float x = min.x + 10.0f;
    auto put = [&](const std::string& text, ImU32 color) {
        list->AddText(ImVec2(x, textY), color, text.c_str());
        x += ImGui::CalcTextSize(text.c_str()).x;
    };

    const float fps = ImGui::GetIO().Framerate;
    char frame[48];
    std::snprintf(frame, sizeof(frame), "%.0f FPS  (%.1f ms)", fps, fps > 0.0f ? 1000.0f / fps : 0.0f);
    put(frame, normal);
    put("   |   ", dim);
    put(std::to_string(scene_ ? scene_->GetObjects().size() : 0) + " objects", normal);
    put("   |   ", dim);
    if (const SceneObject* selected = SelectedObject()) {
        put("Selected: ", dim);
        put(selected->GetEditorName(), normal);
    } else {
        put("Nothing selected", dim);
    }

    // On the right: is there anything to lose?
    const bool dirty = IsDirty();
    const char* state = dirty ? "Unsaved changes" : "All changes saved";
    const float width = ImGui::CalcTextSize(state).x + 18.0f;
    const ImU32 dot = dirty ? IM_COL32(240, 180, 70, 255) : IM_COL32(110, 200, 120, 255);
    list->AddCircleFilled(ImVec2(max.x - width - 2.0f, min.y + height * 0.5f), 4.0f, dot);
    list->AddText(ImVec2(max.x - width + 8.0f, textY), dirty ? normal : dim, state);
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
    using EditorIcons::Icon;
    ImGui::SetCursorPos(ImVec2(6.0f, 4.0f));

    // A thin vertical line between groups of tools.
    auto groupGap = [&]() {
        ImGui::SameLine(0.0f, 10.0f);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + 2.0f), ImVec2(p.x, p.y + ImGui::GetFrameHeight() - 2.0f),
                                            ImGui::GetColorU32(ImGuiCol_Separator));
        ImGui::SameLine(0.0f, 10.0f);
    };

    // Icon buttons that stay lit while their tool is on. The tooltip is the label and carries the shortcut, so the strip stays quiet.
    // Transform tools: joined, because only one is on at a time.
    if (EditorIcons::Button("##toolMove", Icon::Move, gizmoTool_ == GizmoTool::Translate, "Move  (1)")) gizmoTool_ = GizmoTool::Translate;
    ImGui::SameLine(0.0f, 2.0f);
    if (EditorIcons::Button("##toolRotate", Icon::Rotate, gizmoTool_ == GizmoTool::Rotate, "Rotate  (2)")) gizmoTool_ = GizmoTool::Rotate;
    ImGui::SameLine(0.0f, 2.0f);
    if (EditorIcons::Button("##toolScale", Icon::Scale, gizmoTool_ == GizmoTool::Scale, "Scale  (3)")) gizmoTool_ = GizmoTool::Scale;

    groupGap();
    if (EditorIcons::Button("##toolSpace", gizmoLocal_ ? Icon::Local : Icon::World, false,
                            gizmoLocal_ ? "Local: handles follow the object  (G)" : "World: handles follow the world axes  (G)")) gizmoLocal_ = !gizmoLocal_;

    if (scene_ && scene_->GetTerrain()) {
        groupGap();
        // Read the mode ONCE, before the click. The click flips sculptMode_, and a push and its pop must agree on it.
        if (EditorIcons::Button("##toolBrush", Icon::Brush, sculptMode_, "Sculpt and paint the terrain  (4)")) ToggleSculptMode();
    }

    // The shortcuts for whatever mode is on, one hover away instead of a sentence that always takes up the room.
    ImGui::SameLine(0.0f, 14.0f);
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        if (sculptMode_) {
            ImGui::TextUnformatted("Left-drag: brush      Shift: invert\n[ and ]: brush radius      Esc: leave brush mode");
        } else {
            ImGui::TextUnformatted("Ctrl: snap while dragging      F: frame selected\nDel: delete      Ctrl+D: duplicate\nMiddle mouse: look around      WASD: move");
        }
        ImGui::EndTooltip();
    }
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
            if (ImGui::BeginDragDropTarget()) {   // a model dragged in from the Assets panel is imported, like a double-click there
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetPathPayload)) {
                    const std::string dropped = static_cast<const char*>(payload->Data);
                    std::string extension = std::filesystem::path(dropped).extension().string();
                    for (char& ch : extension) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    if (extension == ".gltf" || extension == ".glb" || extension == ".fbx" || extension == ".obj") pendingImportPath_ = dropped;
                }
                ImGui::EndDragDropTarget();
            }
            DrawSelectionAndGizmo(ctx, imageMin.x, imageMin.y, available.x, available.y);
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
    // The brush works on a terrain WORLD when the world (or one of its tiles) is selected or when it is the only terrain; strokes are then stored in
    // the world and carry on across tile borders. Otherwise it works on the old single terrain.
    TerrainWorld* sculptWorld = nullptr;
    Terrain* sculptTerrain = nullptr;
    if (sculptMode_) {
        TerrainWorld* anyWorld = ctx.scene.FindFirst<TerrainWorld>();
        Terrain* single = FindSingleTerrain(ctx.scene);
        const SceneObject* chosen = SelectedObject();
        const auto* chosenTerrain = dynamic_cast<const Terrain*>(chosen);
        const bool worldChosen = dynamic_cast<const TerrainWorld*>(chosen) != nullptr || (chosenTerrain && chosenTerrain->IsTile());
        if (anyWorld && (!single || worldChosen)) sculptWorld = anyWorld;
        else sculptTerrain = single;
    }
    const bool sculpting = sculptTerrain != nullptr || sculptWorld != nullptr;
    if (sculptMode_ && !sculpting) sculptMode_ = false;
    if (sculptStroking_ && !sculpting) {   // mode left in mid-stroke
        if (TerrainWorld* anyWorld = ctx.scene.FindFirst<TerrainWorld>(); anyWorld && strokeOnWorld_) FinishWorldStroke(anyWorld);
        else FinishSculptStroke(FindSingleTerrain(ctx.scene));
    }

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();                       // draw into the Viewport window, on top of the image
    ImGuizmo::SetRect(x, y, width, height);        // the gizmo must know which part of the screen is the 3D view

    DrawLightMarkers(ImGui::GetWindowDrawList(), ctx.scene, selectedId_, view, projection, x, y, width, height);

    SceneObject* selected = SelectedObject();
    // River drawing (lesson 38) works on the selected river and not while sculpting; it ends if either is no longer true.
    Water* riverWater = nullptr;
    if (riverEditMode_) {
        riverWater = dynamic_cast<Water*>(selected);
        if (sculpting || !riverWater || !riverWater->GetSettings().HasCurrent()) { riverEditMode_ = false; riverSpringMode_ = false; riverDragIndex_ = -1; riverWater = nullptr; }
    }
    if (selected) {
        XMFLOAT4X4 boundsWorld;
        XMFLOAT3 boundsMin, boundsMax;
        if (selected->GetOrientedBounds(boundsWorld, boundsMin, boundsMax)) {
            ImGui::GetWindowDrawList()->PushClipRect(ImVec2(x, y), ImVec2(x + width, y + height), true);
            DrawBoxOutline(ImGui::GetWindowDrawList(), boundsWorld, boundsMin, boundsMax, view, projection, x, y, width, height,
                           IM_COL32(255, 170, 40, 255));
            ImGui::GetWindowDrawList()->PopClipRect();
        } else if (const Water* selectedForOutline = dynamic_cast<const Water*>(selected)) {
            ImGui::GetWindowDrawList()->PushClipRect(ImVec2(x, y), ImVec2(x + width, y + height), true);
            DrawWaterSelection(ImGui::GetWindowDrawList(), *selectedForOutline, view, projection, x, y, width, height, IM_COL32(255, 170, 40, 255));
            ImGui::GetWindowDrawList()->PopClipRect();
        }

        XMFLOAT3 position, rotation, scale;
        Water* selectedWater = dynamic_cast<Water*>(selected);
        if (!sculpting && !riverWater && selected->GetEditorTransform(position, rotation, scale) &&   // no gizmo while sculpting or drawing a river
            (gizmoTool_ != GizmoTool::Rotate || selected->SupportsRotation()) &&
            (gizmoTool_ != GizmoTool::Scale || selected->SupportsScale() || selectedWater)) {
            // Water is sized by its Width and Length, so for it the Scale tool edits THOSE: the gizmo is composed at the plane's
            // real size in the world, and what it is dragged to becomes the new Width and Length. Its own scale stays as it was.
            const XMFLOAT3 bodyScale = scale;
            if (selectedWater) {
                const WaterSettings& ws = selectedWater->GetSettings();
                scale = { ws.size * std::fabs(bodyScale.x), 1.0f, ws.sizeZ * std::fabs(bodyScale.z) };
            }
            XMFLOAT4X4 matrix = ComposeTransform(position, rotation, scale);
            const ImGuizmo::OPERATION operation = gizmoTool_ == GizmoTool::Translate ? ImGuizmo::TRANSLATE
                                                : gizmoTool_ == GizmoTool::Rotate    ? (selectedWater ? ImGuizmo::ROTATE_Y : ImGuizmo::ROTATE)   // water stays level
                                                                                     : ImGuizmo::SCALE;
            const ImGuizmo::MODE mode = (gizmoLocal_ || gizmoTool_ == GizmoTool::Scale) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
            // Holding Ctrl snaps: half a unit, 15 degrees, or a tenth of the scale.
            const float step = gizmoTool_ == GizmoTool::Translate ? 0.5f : gizmoTool_ == GizmoTool::Rotate ? 15.0f : 0.1f;
            const float snap[3] = { step, step, step };
            ImGuizmo::Manipulate(&view._11, &projection._11, operation, mode, &matrix._11, nullptr,
                                 ImGui::GetIO().KeyCtrl ? snap : nullptr);
            if (ImGuizmo::IsUsing()) {
                XMFLOAT3 newScale;
                DecomposeTransform(matrix, position, rotation, newScale);
                if (selectedWater) {
                    if (gizmoTool_ == GizmoTool::Scale) {
                        WaterSettings ws = selectedWater->GetSettings();
                        ws.size = (std::clamp)(newScale.x / (std::max)(std::fabs(bodyScale.x), 1e-3f), WaterSettings::kSizeMin, WaterSettings::kSizeMax);
                        ws.sizeZ = (std::clamp)(newScale.z / (std::max)(std::fabs(bodyScale.z), 1e-3f), WaterSettings::kSizeMin, WaterSettings::kSizeMax);
                        selectedWater->SetSettings(ws);
                    }
                    selected->SetEditorTransform(position, rotation, bodyScale);
                } else {
                    selected->SetEditorTransform(position, rotation, newScale);
                }
            }
        }
    }

    // Click = pick. Only when the click is not on the gizmo's handles, or grabbing an arrow would also select
    // whatever object is behind it.
    if (!sculpting && !riverWater && viewportHovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
        const ImVec2 mouse = ImGui::GetMousePos();
        const float ndcX = ((mouse.x - x) / width) * 2.0f - 1.0f;
        const float ndcY = 1.0f - ((mouse.y - y) / height) * 2.0f;   // screen y grows downward, ndc y grows upward
        const SceneObjectId hit = PickObject(MakePickRay(view, projection, ndcX, ndcY));
        if (hit != 0) selectedId_ = hit;
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
            strokeOnWorld_ = false;
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

    // ---- Sculpting and painting a terrain WORLD: the same press / hold / release, but the brush works on the world's lattice, so a stroke
    // over a tile border is one stroke, and what it changes is stored per tile (src/WorldEdits.h). The touched tiles are rebuilt by the world. ----
    if (sculptWorld) {
        const ImGuiIO& io = ImGui::GetIO();
        const float ndcX = ((io.MousePos.x - x) / width) * 2.0f - 1.0f;
        const float ndcY = 1.0f - ((io.MousePos.y - y) / height) * 2.0f;
        float distance = 0.0f;
        XMFLOAT3 point(0.0f, 0.0f, 0.0f);
        const bool onGround = (viewportHovered_ || sculptStroking_) &&
                              sculptWorld->Raycast(ctx.scene, MakePickRay(view, projection, ndcX, ndcY), distance, &point);
        if (viewportHovered_ && onGround && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            strokeIsPaint_ = (brushTarget_ == 1);
            strokeOnWorld_ = true;
            sculptWorld->BeginStroke();
            if (!strokeIsPaint_) brush_.targetHeight = sculptWorld->GroundHeight01(point.x, point.z);   // Flatten works towards where the stroke began
            sculptStroking_ = true;
        }
        if (sculptStroking_ && strokeOnWorld_) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (onGround) {
                    const float seconds = (std::min)(io.DeltaTime, 0.05f);   // a long frame must not make the ground lurch
                    if (strokeIsPaint_) {
                        PaintBrush paint;
                        paint.layer = brushLayer_;
                        paint.erase = (brushErase_ != io.KeyShift);
                        paint.radius = brush_.radius;
                        paint.strength = brush_.strength;
                        paint.falloff = brush_.falloff;
                        sculptWorld->PaintStep(paint, point.x, point.z, seconds);
                    } else {
                        BrushSettings step = brush_;
                        if (io.KeyShift && step.mode == BrushMode::Raise) step.mode = BrushMode::Lower;
                        else if (io.KeyShift && step.mode == BrushMode::Lower) step.mode = BrushMode::Raise;
                        sculptWorld->SculptStep(step, point.x, point.z, seconds);
                    }
                }
            } else {
                FinishWorldStroke(sculptWorld);
            }
        }
        if (onGround) {
            // The ring is draped over the tile under the middle of the brush (the part of it over a neighbour is not drawn).
            if (const Terrain* ringTile = sculptWorld->TileAt(ctx.scene, point.x, point.z)) {
                ImGui::GetWindowDrawList()->PushClipRect(ImVec2(x, y), ImVec2(x + width, y + height), true);
                const bool paintBrush = sculptStroking_ ? strokeIsPaint_ : (brushTarget_ == 1);
                DrawBrushRing(ImGui::GetWindowDrawList(), *ringTile, point.x, point.z, brush_.radius, view, projection,
                              x, y, width, height, paintBrush ? IM_COL32(90, 200, 255, 255) : IM_COL32(255, 220, 80, 255));
                ImGui::GetWindowDrawList()->PopClipRect();
            }
        }
    }

    if (riverWater) HandleRiverEditing(riverWater, view, projection, x, y, width, height);
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
    // Water (lesson 42) has no box: it is picked where its SURFACE is, and only where that spot is really water. The terrain cannot
    // be clicked itself, but it can stand in front of the water: a hill between you and a lake hides it.
    float terrainDistance = bestDistance;
    if (const Terrain* terrain = scene_->GetTerrain()) {
        float hit = 0.0f;
        if (terrain->Raycast(ray, hit) && hit < terrainDistance) terrainDistance = hit;
    }
    for (const auto& object : scene_->GetObjects()) {
        const Water* water = dynamic_cast<const Water*>(object.get());
        float distance = 0.0f;
        // A water body that is already selected lets the click through to what lies behind it (a rock under the surface):
        // otherwise nothing below the water could ever be picked.
        if (water && water->GetId() == selectedId_ && best != 0) continue;
        if (water && water->RayHit(ray.origin, ray.direction, distance) && distance < terrainDistance) {
            terrainDistance = distance;   // (the nearest of the water bodies, the objects and the ground in front of them)
            best = water->GetId();
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
    if (scene_->GetUnderwaterPreview() != 0 && selectedId_ != scene_->GetUnderwaterPreview()) scene_->SetUnderwaterPreview(0, underwaterPreviewDepth_);   // the preview is for tuning the selected water
    if (addLightKind_ >= 0) {
        const int kind = addLightKind_;
        addLightKind_ = -1;
        if (LightObject* light = ctx.scene.Add<LightObject>(kind == 2 ? LightKind::Point : LightKind::Directional)) {
            if (kind == 0) { LightObject::ApplySunPreset(*light); light->SetName("Sun"); }
            else if (kind == 1) { LightObject::ApplyMoonPreset(*light); light->SetName("Moon"); }
            else light->SetName("Point light");
            PlaceInFrontOfCamera(light, kind == 2 ? 6.0f : 12.0f);   // where you are looking, so it is in view to grab
            Select(light);
            RecordAdded(light, std::string("Add ") + light->GetEditorName());
        }
    }
    if (addTerrainWorldRequested_) {
        addTerrainWorldRequested_ = false;
        if (!ctx.scene.FindFirst<TerrainWorld>()) {
            // Only the world is added (and undoable): its tiles are streamed in around the camera by Scene::Update.
            TerrainWorld* world = ctx.scene.Add<TerrainWorld>(ctx.renderer.GetDevice());
            if (world) { RecordAdded(world, "Add terrain world"); Select(world); }
        }
    }
    if (addWorldFromTerrainRequested_) {
        addWorldFromTerrainRequested_ = false;
        Terrain* single = FindSingleTerrain(ctx.scene);
        if (single && !ctx.scene.FindFirst<TerrainWorld>()) {
            TerrainWorld* world = ctx.scene.Add<TerrainWorld>(ctx.renderer.GetDevice());
            if (world) {
                world->SetSettings(WorldSettingsFromTerrain(*single));
                MigrationStats stats;
                const bool carried = world->AdoptFrom(*single, &stats);
                world->SetName("Terrain world (from terrain)");
                RecordAdded(world, "Migrate terrain to world");
                Select(world);
                if (carried) {
                    ShowMessage("Terrain migrated",
                                "The world stands where the terrain stood. " + std::to_string(stats.heightSamples) + " height samples and " + std::to_string(stats.paintCells) +
                                " painted cells were carried over as edits.\n\nThe old terrain is still in the scene: when the world looks right, select the old terrain and delete it (Delete key).\nUntil then both are drawn.");
                } else {
                    ShowMessage("Terrain migrated, with a warning", "The world was made, but its ground could not be built (see the Terrain world's status in the inspector), so the sculpting was not carried over.");
                }
            }
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
            if (scene_->GetGroundHeight(position.x, position.z, groundY)) {
                position.y = groundY + 0.4f;
            } else if (const Water* other = scene_->GetWater(); other && other != water) {
                position.y = other->GetSettings().height;
            }
            XMFLOAT3 rotation, scale, unusedPosition;
            water->GetEditorTransform(unusedPosition, rotation, scale);
            water->SetEditorTransform(position, rotation, scale);
            Select(water);
            RecordAdded(water, "Add water body");
            if (type == WaterBodyType::River) { riverEditMode_ = true; riverDragIndex_ = -1; riverStatus_.clear(); }   // a river starts as a curve to draw, not a plane to set up
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
    const bool interacting = ImGui::IsAnyItemActive() || ImGuizmo::IsUsing() || riverDragIndex_ >= 0;

    std::string before;
    const std::string globals = CaptureGlobalState(ctx.scene);
    if (globalDetector_.Observe(0, globals, interacting, before)) RecordGlobalChange(before, globals);

    SceneObject* selected = SelectedObject();
    const std::string state = selected ? CaptureObjectState(*selected) : std::string();
    if (selected && !state.empty()) {
        if (objectDetector_.Observe(selected->GetId(), state, interacting, before)) {
            RecordPropertyChange(selected->GetId(), std::string("Edit ") + selected->GetEditorName(), before, state);
        }
    } else {
        objectDetector_.Reset();
    }
    skyDetector_.Reset();   // the sky is edited only while selected (the Inspector), which the detector above already watches
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
        skyDetector_.Reset();
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

// The kinds of water, as menu items (Add menu and the Hierarchy's right-click menu share them).
void EditorLayer::DrawAddWaterItems()
{
    static const struct { const char* name; const char* hint; WaterBodyType type; } kTypes[] = {
        { "Lake", "Short wind chop, round by default", WaterBodyType::Lake },
        { "River", "A long thin current: everything on it flows one way", WaterBodyType::River },
        { "Pond", "Small and nearly still", WaterBodyType::Pond },
        { "Ocean", "Big swells rolling in with the wind", WaterBodyType::Ocean },
        { "Custom", "A blank slate that shows every setting", WaterBodyType::Custom } };
    for (const auto& entry : kTypes) {
        if (ImGui::MenuItem(entry.name)) addWaterType_ = static_cast<int>(entry.type);
        ImGui::SetItemTooltip("%s", entry.hint);
    }
}

namespace {
// The Hierarchy groups what is in the scene by kind, so a scene with a few dozen things stays readable.
enum class HierarchyGroup { Environment, Lights, Terrain, Water, Objects, Scatter };

HierarchyGroup GroupOf(const SceneObject& object)
{
    if (dynamic_cast<const Skybox*>(&object)) return HierarchyGroup::Environment;
    if (dynamic_cast<const LightObject*>(&object)) return HierarchyGroup::Lights;
    if (dynamic_cast<const Terrain*>(&object) || dynamic_cast<const TerrainWorld*>(&object)) return HierarchyGroup::Terrain;
    if (dynamic_cast<const Water*>(&object)) return HierarchyGroup::Water;
    if (dynamic_cast<const ScatterObject*>(&object)) return HierarchyGroup::Scatter;
    return HierarchyGroup::Objects;
}

EditorIcons::Icon IconFor(const SceneObject& object)
{
    if (dynamic_cast<const Skybox*>(&object)) return EditorIcons::Icon::Sky;
    if (const auto* light = dynamic_cast<const LightObject*>(&object)) return LightIconFor(*light);
    if (dynamic_cast<const Terrain*>(&object) || dynamic_cast<const TerrainWorld*>(&object)) return EditorIcons::Icon::Terrain;
    if (dynamic_cast<const Water*>(&object)) return EditorIcons::Icon::Water;
    if (dynamic_cast<const ScatterObject*>(&object)) return EditorIcons::Icon::Scatter;
    if (dynamic_cast<const ModelObject*>(&object)) return EditorIcons::Icon::Model;
    return EditorIcons::Icon::Cube;
}

std::string LowerCopy(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}
}

void EditorLayer::DrawHierarchyPanel()
{
    hierarchyFocused_ = false;
    if (!showHierarchy_) return;
    if (!ImGui::Begin("Hierarchy", &showHierarchy_)) { ImGui::End(); return; }
    hierarchyFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // The search box: matches the name or the kind ("water", "model", "sky"). While it has text every group opens.
    {
        const bool hadText = hierarchyFilter_[0] != '\0';
        const float clearWidth = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(hadText ? -(clearWidth + ImGui::GetStyle().ItemSpacing.x) : -1.0f);
        ImGui::InputTextWithHint("##hierarchyFilter", "Search by name or type...", hierarchyFilter_, sizeof(hierarchyFilter_));
        if (hadText) {
            ImGui::SameLine();
            if (ImGui::Button("x##clearHierarchyFilter", ImVec2(clearWidth, 0.0f))) hierarchyFilter_[0] = '\0';
            ImGui::SetItemTooltip("Clear the search");
        }
    }
    const std::string needle = LowerCopy(hierarchyFilter_);
    const bool filtering = !needle.empty();
    ImGui::Spacing();

    static const struct { HierarchyGroup group; const char* label; } kGroups[] = {
        { HierarchyGroup::Environment, "Environment" }, { HierarchyGroup::Lights, "Lights" }, { HierarchyGroup::Terrain, "Terrain" }, { HierarchyGroup::Water, "Water" },
        { HierarchyGroup::Objects, "Objects" }, { HierarchyGroup::Scatter, "Scatter" } };

    int shown = 0;
    for (const auto& group : kGroups) {
        std::vector<SceneObject*> members;
        for (const auto& owned : scene_->GetObjects()) {
            SceneObject* object = owned.get();
            if (!object || GroupOf(*object) != group.group) continue;
            if (const auto* tile = dynamic_cast<const Terrain*>(object); tile && tile->IsTile()) continue;   // the world's tiles come and go: the world stands for them
            if (filtering && LowerCopy(object->GetEditorName()).find(needle) == std::string::npos &&
                LowerCopy(object->GetTypeName()).find(needle) == std::string::npos) continue;
            members.push_back(object);
        }
        if (members.empty()) continue;   // a group with nothing in it takes no room
        shown += static_cast<int>(members.size());

        char header[64];
        std::snprintf(header, sizeof(header), "%s  (%d)###group%d", group.label, static_cast<int>(members.size()), static_cast<int>(group.group));
        if (filtering) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) continue;

        for (SceneObject* object : members) {
            ImGui::PushID(static_cast<int>(object->GetId()));   // two objects can share a name; the id keeps their rows distinct
            const float rowHeight = ImGui::GetTextLineHeight() + 4.0f;
            if (ImGui::Selectable("##row", selectedId_ == object->GetId(), ImGuiSelectableFlags_None, ImVec2(0.0f, rowHeight))) {
                Select(object);
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { Select(object); FrameSelected(); }   // double-click = go and look at it (as F does)
            const ImVec2 rowMin = ImGui::GetItemRectMin();
            const ImVec2 rowMax = ImGui::GetItemRectMax();

            // The row itself: an icon for the kind, the name, and for the sky that is showing a small "active" tag.
            const auto* sky = dynamic_cast<const Skybox*>(object);
            const bool dimmed = sky && !sky->IsActive();
            ImDrawList* list = ImGui::GetWindowDrawList();
            const ImU32 textColor = ImGui::GetColorU32(dimmed ? ImGuiCol_TextDisabled : ImGuiCol_Text);
            const float iconSize = ImGui::GetTextLineHeight();
            EditorIcons::Draw(list, IconFor(*object), ImVec2(rowMin.x + iconSize * 0.5f + 4.0f, (rowMin.y + rowMax.y) * 0.5f), iconSize * 0.95f, textColor);
            list->AddText(ImVec2(rowMin.x + iconSize + 12.0f, rowMin.y + 2.0f), textColor, object->GetEditorName());
            const auto* lightObject = dynamic_cast<const LightObject*>(object);
            const int rank = lightObject ? scene_->DirectionalRank(*lightObject) : -1;
            if (rank >= 0 && rank <= 2) {   // which directional light does what, at a glance
                const char* tag = rank == 0 ? "sun" : rank == 1 ? "2nd" : "unused";
                const float tagWidth = ImGui::CalcTextSize(tag).x;
                list->AddText(ImVec2(rowMax.x - tagWidth - 8.0f, rowMin.y + 2.0f), ImGui::GetColorU32(rank == 2 ? ImGuiCol_TextDisabled : ImGuiCol_CheckMark), tag);
            }
            if (const auto* waterObject = dynamic_cast<const Water*>(object)) {   // which kind of water: a lake and a river look alike in a list
                static const char* const kKinds[] = { "ocean", "lake", "river", "pond", "custom" };
                const int kind = static_cast<int>(waterObject->GetSettings().bodyType);
                const char* tag = (kind >= 0 && kind < 5) ? kKinds[kind] : "water";
                const float tagWidth = ImGui::CalcTextSize(tag).x;
                list->AddText(ImVec2(rowMax.x - tagWidth - 8.0f, rowMin.y + 2.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), tag);
            }
            if (sky && sky->IsActive()) {
                const char* tag = "active";
                const float tagWidth = ImGui::CalcTextSize(tag).x;
                list->AddText(ImVec2(rowMax.x - tagWidth - 8.0f, rowMin.y + 2.0f), ImGui::GetColorU32(ImGuiCol_CheckMark), tag);
            }

            if (ImGui::BeginPopupContextItem()) {   // right-click on a row
                Select(object);
                if (sky && ImGui::MenuItem("Use this sky", nullptr, false, !sky->IsActive())) pendingActiveSkyId_ = object->GetId();
                if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, object->CanClone())) duplicateRequested_ = true;
                if (ImGui::MenuItem("Delete", "Del", false, object->CanDelete())) deleteRequested_ = true;
                if (ImGui::MenuItem("Frame", "F")) FrameSelected();
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::Spacing();
    }
    if (shown == 0) ImGui::TextDisabled(filtering ? "Nothing matches the search." : "The scene is empty.");

    // Right-click on the empty part of the panel.
    if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Add cube")) addCubeRequested_ = true;
        if (ImGui::BeginMenu("Add light")) {
            DrawAddLightItems();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Add water body")) {
            DrawAddWaterItems();
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Add model from file...")) importRequested_ = true;
        if (ImGui::MenuItem("Add scatter on terrain", nullptr, false, scene_ && scene_->GetTerrain())) addScatterRequested_ = true;
        if (ImGui::BeginMenu("Add sky")) {
            EnsureSkyboxScan();
            for (const SkyboxChoice& choice : skyboxFolders_) {
                if (ImGui::MenuItem(choice.label.c_str())) pendingAddSkyFolder_ = choice.folder;
            }
            if (skyboxFolders_.empty()) ImGui::TextDisabled("No skybox folders found in assets/.");
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
    ImGui::End();
}

void EditorLayer::DrawInspectorPanel()
{
    if (!showInspector_) return;
    if (!ImGui::Begin("Inspector", &showInspector_)) { ImGui::End(); return; }

    if (SceneObject* selected = SelectedObject()) {
        DrawObjectInspector(selected);
    } else {
        ImGui::TextDisabled("Click an object in the Viewport or the Hierarchy.");
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Settings for the whole scene (sky, lights, shadows, exposure, wind, camera) are in the Scene Settings panel.");
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}

void EditorLayer::DrawObjectInspector(SceneObject* object)
{
    if (!object) return;
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));   // the accent colour: which object this is
    ImGui::TextUnformatted(object->GetEditorName());
    ImGui::PopStyleColor();
    ImGui::Separator();
    if (!dynamic_cast<Skybox*>(object)) DrawTransformEditor(object);   // a sky has no position: it is always around you

    if (auto* terrain = dynamic_cast<Terrain*>(object)) DrawTerrainInspector(terrain);
    if (auto* world = dynamic_cast<TerrainWorld*>(object)) DrawTerrainWorldInspector(world);
    if (auto* water = dynamic_cast<Water*>(object)) DrawWaterInspector(water);
    if (auto* light = dynamic_cast<LightObject*>(object)) DrawLightInspector(light);
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

void EditorLayer::HandleWorldPictureRequest(LayerContext& ctx)
{
    if (!worldPictureRequested_) return;
    worldPictureRequested_ = false;
    TerrainWorld* world = ctx.scene.FindFirst<TerrainWorld>();
    if (!world) return;
    const std::string path = PickImageFile(hwnd_);
    if (path.empty()) return;   // cancelled
    TerrainWorldSettings settings = world->GetSettings();
    settings.source = WorldSourceKind::Picture;
    settings.picturePath = path;
    world->SetSettings(settings);   // every tile is rebuilt; if the picture cannot be read the inspector says so
    if (!IsInsideAssets(path, AssetsRootText())) {
        ShowMessage("Image applied, with a warning", "That image is outside the assets folder, so it will not be shipped with the game.\nCopy it into assets/ and choose it again from there.");
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
    skyDetector_.Reset();
    currentScenePath_ = path;
    savedRevision_ = history_.Revision();  // what was just loaded is, by definition, what is saved
    selectedId_ = 0;                       // the old objects are gone, and so are their ids
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
    if (LightObject* sun = ctx.scene.Add<LightObject>(LightKind::Directional)) sun->SetName("Sun");   // ...and a sun, or it is dark
    history_.Clear();
    objectDetector_.Reset();
    globalDetector_.Reset();
    skyDetector_.Reset();
    currentScenePath_.clear();
    savedRevision_ = history_.Revision();
    selectedId_ = 0;
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
    // With a project open the browser offers two places: the project's assets/ (what ships; only scenes/ at first) and the
    // engine's own assets/ (the stock textures, skyboxes and models). Without one, just the engine's.
    std::vector<AssetBrowserPanel::AssetRoot> assetRoots;
    if (project_.IsOpen()) assetRoots.push_back({ "Project", project_.AssetsRoot() });
    assetRoots.push_back({ "Engine", std::filesystem::path("assets") });
    const AssetAction assetAction = assetBrowser_.Draw(&showAssets_, assetRoots);
    if (assetAction.kind == AssetAction::Kind::OpenScene) RequestNavigation(NavKind::OpenScene, assetAction.path);
    else if (assetAction.kind == AssetAction::Kind::ImportModel) pendingImportPath_ = assetAction.path;
    else if (assetAction.kind == AssetAction::Kind::UseAsSkybox) pendingSkyboxFolder_ = assetAction.path;
    else if (assetAction.kind == AssetAction::Kind::AddAsSkybox) pendingAddSkyFolder_ = assetAction.path;

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

void EditorLayer::DrawEnvironmentInspector()
{
    EnvironmentSettings& env = scene_->GetEnvironment();
    ImGui::SeparatorText("Sky lighting");
    ImGui::Checkbox("Light the scene from the sky", &env.enabled);
    ImGui::SetItemTooltip("On: the sky lights everything - soft light from every direction, plus reflections on glossy surfaces.\nOff: a flat Ambient colour (Lighting tab) instead, like a scene without a sky.");
    ImGui::BeginDisabled(!env.enabled);
    ImGui::SliderFloat("Diffuse", &env.diffuseIntensity, 0.0f, 3.0f);
    ImGui::SetItemTooltip("Strength of the soft light the sky gives every surface.");
    ImGui::SliderFloat("Specular", &env.specularIntensity, 0.0f, 3.0f);
    ImGui::SetItemTooltip("Strength of the sky's reflection on glossy surfaces.");
    ImGui::EndDisabled();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled(env.enabled ? "The sky's own Intensity, Tint and Rotation also change this light: select the sky to edit them."
                                    : "Sky lighting is off: the Ambient colour in the Lighting tab is what lights the shadows.");
    ImGui::PopTextWrapPos();

    // The things that only matter when checking how the sky was baked, out of the way until asked for.
    if (ImGui::CollapsingHeader("Debug")) {
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
    const Water* waterObject = dynamic_cast<const Water*>(object);
    if (waterObject) {   // water stays level: it can be turned about the vertical, not tilted
        if (ImGui::DragFloat("Rotation Y", &rotationValues[1], 1.0f)) {
            rotation = { 0.0f, XMConvertToRadians(rotationValues[1]), 0.0f };
            changed = true;
        }
    } else if (object->SupportsRotation() && ImGui::DragFloat3("Rotation", rotationValues, 1.0f)) {
        rotation = { XMConvertToRadians(rotationValues[0]), XMConvertToRadians(rotationValues[1]),
                     XMConvertToRadians(rotationValues[2]) };
        changed = true;
    }

    float scaleValues[3] = { scale.x, scale.y, scale.z };
    if (object->SupportsScale() && ImGui::DragFloat3("Scale", scaleValues, 0.01f, 0.01f, 100.0f)) {
        scale = { scaleValues[0], scaleValues[1], scaleValues[2] };
        changed = true;
    }
    if (waterObject) {
        const WaterSettings& ws = waterObject->GetSettings();
        ImGui::TextDisabled("Size  %.0f x %.0f", ws.size * std::fabs(scale.x), ws.sizeZ * std::fabs(scale.z));
        ImGui::SetItemTooltip("Set Width and Length in the water's settings, or drag with the Scale tool.");
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
    float fov = camera.GetFovDegrees();
    if (ImGui::SliderFloat("Field of view", &fov, Camera::kFovMin, Camera::kFovMax, "%.0f deg")) camera.SetFovDegrees(fov);
    ImGui::SetItemTooltip("How wide the lens is (vertical angle). 45 is the default; a wider angle sees more and exaggerates depth.\nThe editor camera only: it is not saved in the scene.");
}

void EditorLayer::DrawPostInspector()
{
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

void EditorLayer::DrawSceneSettingsPanel()
{
    if (!showSceneSettings_) return;
    if (!ImGui::Begin("Scene Settings", &showSceneSettings_)) { ImGui::End(); return; }
    if (ImGui::BeginTabBar("##sceneSettingsTabs")) {
        if (ImGui::BeginTabItem("Environment")) {   // which sky is showing, and the light it casts
            DrawSkiesSection();
            DrawEnvironmentInspector();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Lighting")) { DrawLightsInspector(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Time of Day")) { DrawTimeOfDayInspector(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Post")) { DrawPostInspector(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Weather")) { DrawWeatherInspector(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Camera")) { DrawCameraInspector(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void EditorLayer::DrawLightsInspector()
{
    LightSystem& lights = scene_->GetLightSystem();
    // With sky lighting on, the sky IS the ambient light, so a separate colour would do nothing: it only appears when the
    // sky is not lighting the scene (switched off, or no sky in the scene).
    if (scene_->GetEnvironment().enabled && scene_->GetSkybox()) {
        ImGui::TextDisabled("Ambient light comes from the sky (Environment tab).");
    } else {
        XMFLOAT3& ambient = lights.Ambient();
        float ambientValues[3] = { ambient.x, ambient.y, ambient.z };
        if (ImGui::ColorEdit3("Ambient (no sky)", ambientValues)) ambient = { ambientValues[0], ambientValues[1], ambientValues[2] };
        ImGui::SetItemTooltip("Used because the sky is not lighting the scene.");
    }

    // The lights are objects now: this tab lists them and adds more; each is edited in the Inspector and moved in the viewport.
    ImGui::SeparatorText("Lights");
    const std::vector<LightObject*> sceneLights = scene_->GetLights();
    if (sceneLights.empty()) ImGui::TextDisabled("No lights: the scene is lit only by the sky.");
    for (LightObject* light : sceneLights) {
        ImGui::PushID(static_cast<int>(light->GetId()));
        const int rank = scene_->DirectionalRank(*light);
        const char* role = rank == 0 ? "sun" : rank == 1 ? "2nd" : rank >= 2 ? "unused" : "point";
        std::string row = std::string(light->GetEditorName()) + "  (" + role + ")";
        if (ImGui::Selectable(row.c_str(), selectedId_ == light->GetId())) Select(light);
        ImGui::PopID();
    }
    if (ImGui::Button("+ Sun")) addLightKind_ = 0;
    ImGui::SameLine();
    if (ImGui::Button("+ Moon")) addLightKind_ = 1;
    ImGui::SameLine();
    if (ImGui::Button("+ Point light")) addLightKind_ = 2;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Select a light here or in the Hierarchy to edit it; drag it in the viewport. A sun and a moon are both directional lights: the strongest one casts the shadows, the next one lights without shadows.");
    ImGui::PopTextWrapPos();

    DrawShadowInspector();
}

void EditorLayer::DrawAddLightItems()
{
    if (ImGui::MenuItem("Sun")) addLightKind_ = 0;
    ImGui::SetItemTooltip("A strong, warm directional light high in the sky. It casts the shadows.");
    if (ImGui::MenuItem("Moon")) addLightKind_ = 1;
    ImGui::SetItemTooltip("A weak, pale blue directional light. Next to a sun it lights without shadows; alone (or stronger) it casts them.");
    if (ImGui::MenuItem("Point light")) addLightKind_ = 2;
    ImGui::SetItemTooltip("A lamp: light from one spot that fades out over its range. The 8 nearest the camera are lit.");
}

void EditorLayer::DrawLightInspector(LightObject* light)
{
    char name[64];
    std::snprintf(name, sizeof(name), "%s", light->GetName().c_str());
    if (ImGui::InputText("Name", name, sizeof(name))) light->SetName(name);

    static const char* const kKinds[] = { "Directional (sun, moon)", "Point (lamp)" };
    int kind = static_cast<int>(light->GetKind());
    if (ImGui::Combo("Kind", &kind, kKinds, IM_ARRAYSIZE(kKinds))) light->SetKind(static_cast<LightKind>(kind));

    const XMFLOAT3 color = light->GetColor();
    float colorValues[3] = { color.x, color.y, color.z };
    if (ImGui::ColorEdit3("Color", colorValues)) light->SetColor({ colorValues[0], colorValues[1], colorValues[2] });
    float intensity = light->GetIntensity();
    if (ImGui::DragFloat("Intensity", &intensity, 0.02f, 0.0f, LightObject::kIntensityMax, "%.2f")) light->SetIntensity(intensity);
    ImGui::SetItemTooltip("The scene is HDR: a sun is around 2.5, far above 1 = white. The tone mapper (Camera tab) brings it back.");

    if (light->IsDirectional()) {
        static const char* const kRoles[] = { "None (an ordinary light)", "Sun", "Moon" };
        int role = static_cast<int>(light->GetRole());
        if (ImGui::Combo("Time of day role", &role, kRoles, IM_ARRAYSIZE(kRoles))) light->SetRole(static_cast<LightRole>(role));
        ImGui::SetItemTooltip("Sun or Moon: while the time of day is on (Time of Day tab), this light follows the clock: it rises and sets, and fades and\nchanges colour with its height. Its own colour and intensity are then its NOON values.");

        const Scene::EffectiveLight shining = scene_->GetEffectiveLight(*light);
        float azimuth = light->GetAzimuth(), elevation = light->GetElevation();
        if (shining.driven) {
            float nowAzimuth = 0.0f, nowElevation = 0.0f;
            LightAngles::FromTravel({ shining.direction.x, shining.direction.y, shining.direction.z }, nowAzimuth, nowElevation);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "Following the clock (%s): azimuth %.0f, elevation %.0f deg, strength %.2f",
                               TimeOfDay::FormatHour(scene_->GetClockHour()).c_str(), nowAzimuth, nowElevation, shining.intensity);
            ImGui::PopTextWrapPos();
        }
        ImGui::BeginDisabled(shining.driven);   // the clock sets the aim: the sliders would do nothing
        bool aimed = ImGui::SliderFloat("Azimuth", &azimuth, 0.0f, 360.0f, "%.0f deg");
        ImGui::SetItemTooltip("Which way round the compass the light is, in the sky. 0 = toward +X, 90 = toward +Z (the wind's convention).");
        aimed = ImGui::SliderFloat("Elevation", &elevation, -90.0f, 90.0f, "%.0f deg") || aimed;
        ImGui::SetItemTooltip("How high above the horizon: 0 = on the horizon, 90 = straight overhead. Below 0 the light shines from under the ground.");
        ImGui::EndDisabled();
        if (aimed) light->SetAngles(azimuth, elevation);

        const int rank = scene_->DirectionalRank(*light);
        ImGui::PushTextWrapPos(0.0f);
        if (rank == 0) ImGui::TextDisabled("This is the sun: the strongest directional light. It casts the shadows.");
        else if (rank == 1) ImGui::TextDisabled("The second directional light: it lights the scene but casts no shadows.");
        else ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "Not used: only the two strongest directional lights shine.");
        ImGui::TextDisabled("Where it stands changes nothing: the handle is only there to grab. Rotate it with the gizmo, or use the sliders.");
        ImGui::PopTextWrapPos();
    } else {
        float range = light->GetRange();
        if (ImGui::SliderFloat("Range", &range, LightObject::kRangeMin, 100.0f, "%.1f")) light->SetRange(range);
        ImGui::SetItemTooltip("The distance at which the light has faded to nothing. Shown as three circles while the light is selected.");
        int points = 0;
        for (LightObject* other : scene_->GetLights()) points += other->IsDirectional() ? 0 : 1;
        if (points > static_cast<int>(LightSystem::kMaxPointLights)) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "%d point lights: only the %d nearest the camera are lit.", points, static_cast<int>(LightSystem::kMaxPointLights));
            ImGui::PopTextWrapPos();
        }
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
    TerrainWorld* world = scene_ ? scene_->FindFirst<TerrainWorld>() : nullptr;
    Terrain* single = scene_ ? FindSingleTerrain(*scene_) : nullptr;
    if (!world && !single) { sculptMode_ = false; return; }
    sculptMode_ = !sculptMode_;
    if (!sculptMode_) return;
    // The brush settings live in the inspector of what is sculpted: the world's, or the old terrain's. If something of the world is already
    // selected, or there is no old terrain, it is the world.
    const SceneObject* chosen = SelectedObject();
    const auto* chosenTerrain = dynamic_cast<const Terrain*>(chosen);
    const bool worldChosen = dynamic_cast<const TerrainWorld*>(chosen) != nullptr || (chosenTerrain && chosenTerrain->IsTile());
    if (world && (!single || worldChosen)) Select(world); else Select(single);
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

void EditorLayer::RecordSculptStroke(SceneObjectId terrainId, HeightfieldPatch patch, const char* name)
{
    // The command holds the patch (not the terrain, which may be rebuilt or reloaded) and looks the terrain up by id.
    const auto shared = std::make_shared<HeightfieldPatch>(std::move(patch));
    auto run = [this, terrainId, shared](bool redo) {
        if (!scene_) return;
        if (auto* terrain = dynamic_cast<Terrain*>(scene_->Find(terrainId))) terrain->ApplyPatch(*shared, redo);
    };
    history_.Push(std::make_unique<LambdaCommand>(name, [run]() { run(false); }, [run]() { run(true); }));
}

void EditorLayer::FinishWorldStroke(TerrainWorld* world)
{
    sculptStroking_ = false;
    strokeOnWorld_ = false;
    if (!world) return;
    WorldEditPatch patch = world->EndStroke();
    if (!patch.IsEmpty()) RecordWorldStroke(std::move(patch), strokeIsPaint_);
}

void EditorLayer::RecordWorldStroke(WorldEditPatch patch, bool paint)
{
    // The command holds the patch (not the world, which may be reloaded) and looks the world up when it runs.
    const auto shared = std::make_shared<WorldEditPatch>(std::move(patch));
    auto run = [this, shared](bool redo) {
        if (!scene_) return;
        if (auto* world = scene_->FindFirst<TerrainWorld>()) world->ApplyEditPatch(*shared, redo);
    };
    history_.Push(std::make_unique<LambdaCommand>(paint ? "Paint terrain world" : "Sculpt terrain world", [run]() { run(false); }, [run]() { run(true); }));
}

void EditorLayer::ClearWorldEdits(TerrainWorld* world, bool heights, bool paint)
{
    if (!world) return;
    const std::shared_ptr<WorldEdits> before = world->CopyEdits();
    world->ClearEdits(heights, paint);
    const std::shared_ptr<WorldEdits> after = world->CopyEdits();
    auto replace = [this](const std::shared_ptr<WorldEdits>& edits) {
        if (!scene_) return;
        if (auto* target = scene_->FindFirst<TerrainWorld>()) target->ReplaceEdits(*edits);
    };
    history_.Push(std::make_unique<LambdaCommand>(heights ? "Discard world sculpting" : "Clear world painting",
                                                  [replace, before]() { replace(before); }, [replace, after]() { replace(after); }));
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

void EditorLayer::DrawSculptSection(Terrain* terrain, TerrainWorld* world)
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

    if (world) {
        // A terrain world keeps what the brush did PER TILE (src/WorldEdits.h) in one file beside the scene.
        if (world->HasEdits()) {
            ImGui::TextDisabled("%d tiles carry hand-made edits. Saved next to the scene file (.worldedits).", static_cast<int>(world->Edits().BlockCount()));
            ImGui::TextDisabled("Metres per cell is locked while the world carries edits.");
        }
        if (world->Edits().HasHeightEdits()) {
            if (ImGui::Button("Discard sculpting")) ClearWorldEdits(world, true, false);
            ImGui::SetItemTooltip("Back to the picture or hills everywhere. Undo brings the sculpting back.");
        }
        if (world->Edits().HasPaint()) {
            if (world->Edits().HasHeightEdits()) ImGui::SameLine();
            if (ImGui::Button("Clear painting")) ClearWorldEdits(world, false, true);
            ImGui::SetItemTooltip("Back to the automatic rules everywhere. Undo brings the painting back.");
        }
    }
    if (terrain && terrain->IsSculpted()) {
        ImGui::TextDisabled("Shaped by hand. Saved next to the scene file as a 16-bit PNG.");
        ImGui::TextDisabled("Mesh resolution, noise and the heightmap are locked until you discard.");
        if (ImGui::Button("Discard sculpting")) DiscardSculpt(terrain);
        ImGui::SetItemTooltip("Go back to the heightmap and noise. Undo brings the sculpting back.");
    }
    if (terrain && terrain->IsPainted()) {
        ImGui::TextDisabled("Painted by hand. Saved next to the scene file as a PNG.");
        ImGui::TextDisabled("Mesh resolution and the heightmap are locked until you clear the painting.");
        if (ImGui::Button("Clear painting")) ClearPaint(terrain);
        ImGui::SetItemTooltip("Back to the automatic rules everywhere. Undo brings the painting back.");
    }
    ImGui::PopID();
}

void EditorLayer::DrawTerrainWorldInspector(TerrainWorld* world)
{
    DrawSculptSection(nullptr, world);
    ImGui::SeparatorText("Terrain world");
    // Changing the world rebuilds every tile, so a value is applied when you RELEASE its control, not on every frame of a drag.
    // While a control is being edited we keep our own copy (re-reading the world every frame would throw the edit away before it is
    // applied); otherwise we follow the world, so changes made elsewhere (undo, a loaded scene) show up. Same as the terrain's inspector.
    if (!ImGui::IsAnyItemActive() || pendingWorldId_ != world->GetId()) {
        pendingWorldSettings_ = world->GetSettings();
        pendingWorldId_ = world->GetId();
    }
    TerrainWorldSettings& s = pendingWorldSettings_;
    bool commit = false;
    auto commitOnRelease = [&commit] { if (ImGui::IsItemDeactivatedAfterEdit()) commit = true; };

    // WHERE THE GROUND COMES FROM (roadmap phase 1): endless fractal hills, or a heightmap picture placed in the world.
    int sourceIndex = static_cast<int>(s.source);
    if (ImGui::Combo("Ground", &sourceIndex, "Fractal hills (endless)\0Heightmap picture (bounded)\0")) {
        s.source = static_cast<WorldSourceKind>(sourceIndex);
        commit = true;
    }
    ImGui::SetItemTooltip("Fractal hills: endless noise, the same everywhere. Heightmap picture: the ground is an image placed in the world,\nand the world ends where the picture does. Either way the tiles meet without seams.");

    if (s.source == WorldSourceKind::Picture) {
        const std::string shownPath = s.picturePath.empty() ? std::string(Terrain::DefaultHeightmapPath()) + " (default)" : s.picturePath;
        ImGui::TextUnformatted("Picture");
        ImGui::SameLine(90.0f);
        if (ImGui::Button("Browse...")) worldPictureRequested_ = true;
        ImGui::SameLine();
        ImGui::TextDisabled("%s", shownPath.c_str());
        ImGui::SetItemTooltip("%s\nA 16-bit greyscale image gives much smoother slopes than 8-bit.", shownPath.c_str());
        float widthKm = s.pictureWidth / 1000.0f;
        if (ImGui::SliderFloat("Width (km)", &widthKm, TerrainWorldSettings::kPictureWidthMin / 1000.0f, 20.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) s.pictureWidth = widthKm * 1000.0f;
        commitOnRelease();
        ImGui::SetItemTooltip("How wide the picture is in the world. Its depth follows its shape.");
        ImGui::DragFloat("Corner X (m)", &s.pictureOffsetX, 1.0f, -TerrainWorldSettings::kPictureOffsetMax, TerrainWorldSettings::kPictureOffsetMax, "%.0f");
        commitOnRelease();
        ImGui::DragFloat("Corner Z (m)", &s.pictureOffsetZ, 1.0f, -TerrainWorldSettings::kPictureOffsetMax, TerrainWorldSettings::kPictureOffsetMax, "%.0f");
        commitOnRelease();
        ImGui::SetItemTooltip("Where the picture's top-left corner is, in world metres.");
        if (!world->StatusMessage().empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", world->StatusMessage().c_str());
    } else {
        ImGui::InputInt("Seed", &s.fractal.seed);
        commitOnRelease();
        ImGui::SliderInt("Detail layers", &s.fractal.octaves, 1, 8);
        commitOnRelease();
        ImGui::SetItemTooltip("How many sizes of bump are added together: more layers, finer detail on the hills.");
        ImGui::SliderFloat("Hill size (m)", &s.fractal.baseWavelength, 200.0f, 20000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
        commitOnRelease();
        ImGui::SetItemTooltip("The width of the biggest hills and valleys.");
        // The edge: endless, or a rectangle of tiles starting at tile (0, 0).
        bool bounded = s.bounds.bounded;
        if (ImGui::Checkbox("Bounded (a rectangle of tiles)", &bounded)) {
            s.bounds.bounded = bounded;
            if (bounded && s.bounds.maxX < s.bounds.minX) { s.bounds.minX = 0; s.bounds.minZ = 0; s.bounds.maxX = 3; s.bounds.maxZ = 3; }
            commit = true;
        }
        ImGui::SetItemTooltip("Give the world an edge: only the tiles inside the rectangle exist. Unticked, the world is endless.");
        if (s.bounds.bounded) {
            int across[2] = { s.bounds.TilesAcrossX(), s.bounds.TilesAcrossZ() };
            if (ImGui::DragInt2("Tiles (X, Z)", across, 0.2f, 1, WorldBounds::kMaxTilesAcross)) {
                s.bounds.maxX = s.bounds.minX + (std::max)(across[0], 1) - 1;
                s.bounds.maxZ = s.bounds.minZ + (std::max)(across[1], 1) - 1;
            }
            commitOnRelease();
            int first[2] = { s.bounds.minX, s.bounds.minZ };
            if (ImGui::DragInt2("First tile", first, 0.2f, -1000, 1000)) {
                const int sizeX = s.bounds.TilesAcrossX(), sizeZ = s.bounds.TilesAcrossZ();   // moving the rectangle keeps its size
                s.bounds.minX = first[0]; s.bounds.maxX = first[0] + sizeX - 1;
                s.bounds.minZ = first[1]; s.bounds.maxZ = first[1] + sizeZ - 1;
            }
            commitOnRelease();
            ImGui::SetItemTooltip("The tile at the top-left of the rectangle (tile 0, 0 starts at the world origin). Changing it moves the rectangle.");
        }
    }
    ImGui::SliderFloat("Height (m)", &s.heightScale, 20.0f, 500.0f, "%.0f");
    commitOnRelease();
    ImGui::SliderFloat("View distance (m)", &s.viewDistance, TerrainWorldSettings::kViewDistanceMin, TerrainWorldSettings::kViewDistanceMax, "%.0f", ImGuiSliderFlags_Logarithmic);
    commitOnRelease();
    ImGui::SetItemTooltip("Tiles nearer than this to the camera are loaded. Far tiles are built with fewer cells (every level has a quarter of\nthe cells of the one before), so a long view is cheap. At most %d tiles are kept.", TerrainWorldSettings::kMaxTiles);
    ImGui::BeginDisabled(world->HasEdits());   // the edits sit on the grid's cells: a new grid would lose them
    ImGui::SliderFloat("Metres per cell", &s.grid.metresPerCell, 1.0f, 16.0f, "%.1f");
    commitOnRelease();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("The resolution of the whole world. A tile always has 256 cells (8 chunks), so this sets its size too.");
    ImGui::TextDisabled("Tile: %d cells of %.1f m = %.0f m.   %d tiles loaded.", s.grid.TileCells(), s.grid.metresPerCell, s.grid.TileSize(), world->LoadedTileCount());
    if (world->ActiveBounds().bounded) {
        const WorldBounds& edge = world->ActiveBounds();
        ImGui::TextDisabled("Bounded: %d x %d tiles = %.0f x %.0f m.", edge.TilesAcrossX(), edge.TilesAcrossZ(), edge.TilesAcrossX() * s.grid.TileSize(), edge.TilesAcrossZ() * s.grid.TileSize());
    } else {
        ImGui::TextDisabled("Endless: no edge.");
    }
    ImGui::TextDisabled("By detail (full, 1/4, 1/16, 1/64 of the cells): %d, %d, %d, %d", world->LoadedTileCountAtLod(0), world->LoadedTileCountAtLod(1),
                        world->LoadedTileCountAtLod(2), world->LoadedTileCountAtLod(3));
    // ---- GROUND SHAPE (roadmap, heightmap customisation): the controls that turn a picture into good-looking terrain ----
    ImGui::SeparatorText("Ground shape");
    if (const HeightImageStats* stats = world->PictureStats()) {
        const float slope = SlopeDegrees(stats->meanGradient, s.heightScale, world->PictureMetresPerPixel());
        ImGui::TextDisabled("Picture %d px wide, %.2f m per pixel. It uses %.0f%% of its grey range (%.2f to %.2f); typical slope %.1f degrees.",
                            world->PictureWidthPixels(), world->PictureMetresPerPixel(), stats->UsedRange() * 100.0f, stats->low, stats->high, slope);
        if (stats->UsedRange() < 0.6f && s.shape.blackPoint == 0.0f && s.shape.whitePoint == 1.0f) {
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.35f, 1.0f), "This picture only uses part of its grey range, so the terrain spans only part of the height scale. Auto levels stretches it.");
        }
        if (ImGui::Button("Auto levels")) world->AutoLevels();
        ImGui::SetItemTooltip("Sets black and white point from what the picture really spans (ignoring the darkest and brightest half percent),\nso the lowest ground is at the bottom of the height scale and the highest at the top.");
        ImGui::SameLine();
    }
    if (ImGui::BeginCombo("Preset", "Choose...")) {
        for (int preset = 0; preset < kHeightShapePresetCount; ++preset) {
            if (ImGui::Selectable(HeightShapePresetName(preset))) {
                const float black = s.shape.blackPoint, white = s.shape.whitePoint;   // a preset sets the character, not the levels
                const bool prefilter = s.shape.prefilter;                              // nor how the picture is read
                s.shape = HeightShapePreset(preset);
                s.shape.blackPoint = black;
                s.shape.whitePoint = white;
                s.shape.prefilter = prefilter;
                commit = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SliderFloat("Black point", &s.shape.blackPoint, 0.0f, 0.99f, "%.3f");
    commitOnRelease();
    ImGui::SetItemTooltip("Picture values at or below this become the lowest ground.");
    ImGui::SliderFloat("White point", &s.shape.whitePoint, 0.01f, 1.0f, "%.3f");
    commitOnRelease();
    ImGui::SetItemTooltip("Picture values at or above this become the highest ground.");
    ImGui::SliderFloat("Curve", &s.shape.curve, HeightShapeSettings::kCurveMin, HeightShapeSettings::kCurveMax, "%.2f", ImGuiSliderFlags_Logarithmic);
    commitOnRelease();
    ImGui::SetItemTooltip("Above 1: flat lowlands and sharper peaks (mountains). Below 1: broad highlands. 1: as the picture is.");
    if (ImGui::Checkbox("Invert", &s.shape.invert)) commit = true;
    if (s.source == WorldSourceKind::Picture) {
        ImGui::SliderFloat("Smoothing (px)", &s.shape.smoothing, 0.0f, HeightShapeSettings::kSmoothingMax, "%.0f");
        commitOnRelease();
        ImGui::SetItemTooltip("Blurs the picture. Removes the steps of an 8-bit image and the faceting of a small picture stretched over a big world.");
        if (ImGui::Checkbox("Smooth interpolation (bicubic)", &s.shape.bicubic)) commit = true;
        ImGui::SetItemTooltip("A smooth curve through the pixels instead of straight lines between them. Off matches the old terrain exactly.");
        if (ImGui::Checkbox("Prefilter the picture", &s.shape.prefilter)) commit = true;
        ImGui::SetItemTooltip("When one cell covers several pixels, average the picture over the cell before sampling it. Removes the stairs and streaks on\nthin cliffs. Off matches the old terrain exactly.");
        const float perCell = world->PictureCellPixels();
        if (perCell > 0.0f) {
            const bool aliasing = perCell > 2.0f && !world->PictureIsPrefiltered();
            const ImVec4 color = aliasing ? ImVec4(1.0f, 0.75f, 0.3f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            ImGui::TextColored(color, "%.1f picture pixels per cell%s", perCell,
                               world->PictureIsPrefiltered() ? " (prefiltered)" : (aliasing ? ": thin cliffs will look like stairs. Turn the prefilter on." : ""));
        }
    }
    ImGui::SliderFloat("Detail strength (m)", &s.shape.detailStrength, 0.0f, 60.0f, "%.1f");
    commitOnRelease();
    ImGui::SetItemTooltip("Fine bumps added on top, in METRES (they do not change with the height scale). A picture cannot hold detail finer than its pixels.");
    if (s.shape.detailStrength > 0.0f) {
        ImGui::SliderFloat("Detail size (m)", &s.shape.detailWavelength, HeightShapeSettings::kDetailWavelengthMin, HeightShapeSettings::kDetailWavelengthMax, "%.0f", ImGuiSliderFlags_Logarithmic);
        commitOnRelease();
        ImGui::SliderInt("Detail layers##shape", &s.shape.detailOctaves, 1, 8);
        commitOnRelease();
        if (ImGui::Checkbox("Ridged (sharp creases)", &s.shape.detailRidged)) commit = true;
        ImGui::SliderFloat("Keep low ground smooth", &s.shape.detailValleyMask, 0.0f, 1.0f, "%.2f");
        commitOnRelease();
        ImGui::SetItemTooltip("0: detail everywhere. 1: none in the lowest ground, so shores and riverbeds stay clean.");
        ImGui::InputInt("Detail seed", &s.shape.detailSeed);
        commitOnRelease();
    }
    if (s.customMaterial) {
        ImGui::SeparatorText("Look");
        ImGui::TextDisabled("The slope and height rules, tints and layer images carried over from the terrain this world came from.");
        if (ImGui::Button("Use the automatic look")) { s.customMaterial = false; commit = true; }
        ImGui::SetItemTooltip("The automatic rules, scaled to this world's height. Layer textures are the default ones.");
    }
    if (commit) world->SetSettings(s);   // every tile is rebuilt from the new settings

    // Beyond the view distance there is no land at all. Fog is what makes that look like distance instead of an edge.
    if (ImGui::Button("Fog: hide the edge")) {
        FogSettings& fog = scene_->GetWeather().fog;
        fog.enabled = true;
        fog.visibility = s.viewDistance * 0.8f;   // 95% hidden a little before the last tile
        fog.Clamp();
    }
    ImGui::SetItemTooltip("Turns the scene's fog on and sets its visibility to 80%% of the view distance, so the far tiles fade out before the\nworld ends. You can tune it in the Weather panel; the world never changes the fog by itself.");
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

    if (terrain->IsTile()) {
        ImGui::SeparatorText("Terrain world");
        ImGui::TextWrapped("Tile (%d, %d) of a terrain world. Its heights, size and resolution come from the world (select the Terrain world in the Hierarchy to change them). Sculpt and paint it with the brush (4): strokes are stored in the world, carry on across tile borders and survive the tile being unloaded. The layer settings below change this tile only.",
                           terrain->GetTile().coord.x, terrain->GetTile().coord.z);
    } else {
        DrawSculptSection(terrain);
    }

    ImGui::BeginDisabled(terrain->IsTile());   // the world decides these
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

    ImGui::EndDisabled();   // (end of the world-decided Shape and Noise sections)

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

// ---- Rivers as curves (lesson 38) ----------------------------------------------------------------------------------------

// Clicks in the viewport edit the river's control points: click on empty water level = add a point at the end, press on a point
// and drag = move it, right-click a point = delete it, Esc = done. The points live in the body's frame, so everything goes
// through Water::WorldToBody / BodyToWorld. The mouse is aimed at the GROUND you see (lesson 45), or at the plane of the water when
// it meets no ground (or the option is off): a click lands under the cursor, whatever the height of the land.
void EditorLayer::HandleRiverEditing(Water* water, const XMFLOAT4X4& view, const XMFLOAT4X4& projection,
                                     float x, float y, float width, float height)
{
    const ImGuiIO& io = ImGui::GetIO();
    const XMMATRIX viewProjection = XMLoadFloat4x4(&view) * XMLoadFloat4x4(&projection);
    auto project = [&](float wx, float wy, float wz, ImVec2& out) {
        const XMVECTOR clip = XMVector4Transform(XMVectorSet(wx, wy, wz, 1.0f), viewProjection);
        const float w = XMVectorGetW(clip);
        if (w <= 0.001f) return false;   // behind the camera
        out = ImVec2(x + (XMVectorGetX(clip) / w * 0.5f + 0.5f) * width, y + (1.0f - (XMVectorGetY(clip) / w * 0.5f + 0.5f)) * height);
        return true;
    };
    WaterSettings edited = water->GetSettings();
    const float level = edited.height;

    // Where does the mouse point on the plane of the water?
    const float ndcX = ((io.MousePos.x - x) / width) * 2.0f - 1.0f;
    const float ndcY = 1.0f - ((io.MousePos.y - y) / height) * 2.0f;
    const Ray ray = MakePickRay(view, projection, ndcX, ndcY);
    // Where the ray meets the ground (lesson 45). Aiming at the water's plane instead put a click far from the cursor wherever the
    // land stands above or below the water: the river is drawn where you point, on what you see.
    const Terrain* terrain = scene_ ? scene_->GetTerrain() : nullptr;
    XMFLOAT3 groundPoint(0.0f, 0.0f, 0.0f);
    float groundDistance = 0.0f;
    const bool aimAtGround = riverAimAtGround_ && terrain && terrain->Raycast(ray, groundDistance, &groundPoint);
    // The overlay is drawn ON the ground (or on the water where that is higher), a hair above so it does not flicker.
    auto overlayY = [&](float worldX, float worldZ, float surfaceY) {
        float groundY = 0.0f;
        if (terrain && riverAimAtGround_ && terrain->GetHeightAt(worldX, worldZ, groundY)) return (std::max)(groundY, surfaceY) + 0.15f;
        return surfaceY + 0.15f;
    };
    // The mouse's ray meets a horizontal plane at some height: the water's own level, or (lesson 41) the level of a river point
    // that falls, so a point on a lower stretch lands under the cursor and not beside it. (On the ground the height is not needed.)
    auto bodySpotAt = [&](float planeY, float& bodyX, float& bodyZ) {
        if (aimAtGround) return water->WorldToBody(groundPoint.x, groundPoint.z, bodyX, bodyZ);
        if (std::fabs(ray.direction.y) <= 1e-5f) return false;
        const float t = (planeY - ray.origin.y) / ray.direction.y;
        if (t <= 0.0f) return false;
        return water->WorldToBody(ray.origin.x + ray.direction.x * t, ray.origin.z + ray.direction.z * t, bodyX, bodyZ);
    };
    float mouseBodyX = 0.0f, mouseBodyZ = 0.0f;   // the mouse on the body's own level, in the body's frame, where the control points live
    const bool mouseOnBody = bodySpotAt(level, mouseBodyX, mouseBodyZ);
    // What a click here would do. A new point starts at the level its neighbour or the curve has, so it is placed on THAT plane:
    // a first guess on the body's level says which point it is, a second on its own level says where.
    auto placeUnderMouse = [&](RiverPath::Placement& out) {
        if (!mouseOnBody) return false;
        const std::vector<RiverPoint>& points = edited.riverPath;
        const std::vector<RiverPath::Sample> bodyPath = RiverPath::Build(points, 1.0f);
        out = RiverPath::Place(points, bodyPath, mouseBodyX, mouseBodyZ);
        float bx = 0.0f, bz = 0.0f;
        if (out.point.level != 0.0f && bodySpotAt(level + out.point.level, bx, bz)) out = RiverPath::Place(points, bodyPath, bx, bz);
        return true;
    };

    // The control points on the screen, and which one the mouse is near (within 14 pixels).
    const size_t count = edited.riverPath.size();
    std::vector<ImVec2> handle(count);
    std::vector<char> visible(count, 0);
    int hovered = -1;
    float nearest = 14.0f * 14.0f;
    for (size_t i = 0; i < count; ++i) {
        const XMFLOAT2 world = water->BodyToWorld(edited.riverPath[i].x, edited.riverPath[i].z);
        visible[i] = project(world.x, overlayY(world.x, world.y, level + edited.riverPath[i].level), world.y, handle[i]) ? 1 : 0;
        if (!visible[i]) continue;
        const float dx = handle[i].x - io.MousePos.x, dy = handle[i].y - io.MousePos.y;
        if (dx * dx + dy * dy < nearest) { nearest = dx * dx + dy * dy; hovered = static_cast<int>(i); }
    }

    // The overlay: the middle line (yellow) and both banks (cyan), then the handles.
    ImDrawList* list = ImGui::GetWindowDrawList();
    list->PushClipRect(ImVec2(x, y), ImVec2(x + width, y + height), true);
    const std::vector<RiverPath::Sample>& path = water->GetRiverWorldPath();
    ImVec2 previous[3] = {};
    bool previousOk[3] = { false, false, false };
    const ImU32 colors[3] = { IM_COL32(255, 220, 80, 255), IM_COL32(90, 200, 255, 200), IM_COL32(90, 200, 255, 200) };
    for (const RiverPath::Sample& s : path) {
        const float sideX = -s.tz, sideZ = s.tx;
        const float px[3] = { s.x, s.x + sideX * s.halfWidth, s.x - sideX * s.halfWidth };
        const float pz[3] = { s.z, s.z + sideZ * s.halfWidth, s.z - sideZ * s.halfWidth };
        for (int k = 0; k < 3; ++k) {
            ImVec2 now;
            const bool ok = project(px[k], overlayY(px[k], pz[k], level + s.level), pz[k], now);
            if (ok && previousOk[k]) list->AddLine(previous[k], now, colors[k], k == 0 ? 2.5f : 1.5f);
            previous[k] = now;
            previousOk[k] = ok;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        if (!visible[i]) continue;
        const bool active = static_cast<int>(i) == hovered || static_cast<int>(i) == riverDragIndex_;
        list->AddCircleFilled(handle[i], active ? 8.0f : 6.0f, active ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 150, 40, 255));
        list->AddCircle(handle[i], active ? 8.0f : 6.0f, IM_COL32(0, 0, 0, 255), 0, 1.5f);
        char label[8];
        std::snprintf(label, sizeof(label), "%d", static_cast<int>(i) + 1);
        list->AddText(ImVec2(handle[i].x + 10.0f, handle[i].y - 8.0f), IM_COL32(255, 255, 255, 230), label);
    }

    // What the next click would do, drawn before it is made (lesson 39): on the water it adds a bend point to the curve, anywhere
    // else it extends whichever end of the river is nearer. The order the points are clicked in no longer matters.
    RiverPath::Placement next;
    if (riverSpringMode_ && aimAtGround) {   // lesson 45: the next click is a spring
        const ImU32 spring = IM_COL32(120, 220, 255, 235);
        list->AddCircle(io.MousePos, 10.0f, spring, 0, 2.5f);
        list->AddCircleFilled(io.MousePos, 3.0f, spring);
        list->AddText(ImVec2(io.MousePos.x + 14.0f, io.MousePos.y + 10.0f), spring, "spring: click, and the river finds its way down");
    }
    if (!riverSpringMode_ && hovered < 0 && riverDragIndex_ < 0 && count < static_cast<size_t>(RiverPath::kMaxPoints) && placeUnderMouse(next)) {
        const ImU32 ghost = IM_COL32(255, 255, 255, 170);
        const char* what = "start the river here";
        if (count >= 1) {
            if (next.kind == RiverPath::Placement::Kind::Insert) {
                const XMFLOAT2 on = water->BodyToWorld(next.point.x, next.point.z);
                ImVec2 at;
                if (project(on.x, overlayY(on.x, on.y, level + next.point.level), on.y, at)) list->AddCircle(at, 9.0f, ghost, 0, 2.0f);
                what = "add a bend point here";
            } else {
                const bool upstream = next.kind == RiverPath::Placement::Kind::Prepend;
                const size_t endIndex = upstream ? 0 : count - 1;
                if (visible[endIndex]) list->AddLine(handle[endIndex], io.MousePos, ghost, 1.5f);
                list->AddCircle(io.MousePos, 7.0f, ghost, 0, 2.0f);
                what = upstream ? "extend upstream" : "extend downstream";
            }
        } else {
            list->AddCircle(io.MousePos, 7.0f, ghost, 0, 2.0f);
        }
        list->AddText(ImVec2(io.MousePos.x + 14.0f, io.MousePos.y + 10.0f), ghost, what);
    }

    // Problems with the curve, marked where they are while the points are still being placed.
    const RiverPath::Problems problems = RiverPath::Analyse(path);
    auto ring = [&](int sampleIndex, const char* text) {
        const RiverPath::Sample& sample = path[static_cast<size_t>(sampleIndex)];
        ImVec2 at;
        if (!project(sample.x, overlayY(sample.x, sample.z, level + sample.level), sample.z, at)) return;
        list->AddCircle(at, 16.0f, IM_COL32(255, 70, 60, 255), 0, 3.0f);
        if (text) list->AddText(ImVec2(at.x + 20.0f, at.y - 8.0f), IM_COL32(255, 110, 100, 255), text);
    };
    if (problems.tightIndex >= 0) ring(problems.tightIndex, "bend too tight");
    if (problems.overlapA >= 0) { ring(problems.overlapA, "river overlaps itself"); ring(problems.overlapB, nullptr); }
    if (problems.Any())
        list->AddText(ImVec2(x + 8.0f, y + height - 42.0f), IM_COL32(255, 110, 100, 240),
                      problems.overlapA >= 0 ? "The river lies on top of itself: drag the marked points apart or delete one."
                                             : "A bend is tighter than the river is wide: space the points out or narrow the river.");
    list->AddText(ImVec2(x + 8.0f, y + height - 22.0f), IM_COL32(255, 220, 80, 230),
                  riverSpringMode_ ? "Spring: click the terrain where the river should start. It runs downhill from there | Esc = cancel"
                  : count == 0 ? "River: click the ground where it should start (or press Pick a spring) | Esc or Enter = done"
                               : "River: click = extend (or bend the curve on the water) | drag a point = move | right-click a point = delete | Enter / Esc = done");
    list->PopClipRect();

    // Input.
    bool modified = false;
    bool refit = false;   // the set of points changed shape: re-centre the body and size the plane to cover them
    if (viewportHovered_ && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (riverSpringMode_) {
                float springX = 0.0f, springZ = 0.0f;   // a spring must be ON the ground: that is what the river runs down
                if (aimAtGround && bodySpotAt(level, springX, springZ)) {
                    if (FlowDownhillFrom(water, edited, springX, springZ, false)) { riverSpringMode_ = false; modified = true; refit = true; }
                } else {
                    riverStatus_ = "Click on the terrain to place the spring.";
                }
            } else if (hovered >= 0) {
                riverDragIndex_ = hovered;
            } else if (RiverPath::Placement placement; edited.riverPath.size() < static_cast<size_t>(RiverPath::kMaxPoints) && placeUnderMouse(placement)) {
                // Where the new point goes in the list is decided by RiverPath::Place (tested in tests/river_test.cpp).
                edited.riverPath.insert(edited.riverPath.begin() + static_cast<std::ptrdiff_t>(placement.index), placement.point);
                riverDragIndex_ = static_cast<int>(placement.index);   // keep the button down and drag to fine-tune the new point
                modified = true;
                refit = true;
            }
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hovered >= 0) {
            edited.riverPath.erase(edited.riverPath.begin() + hovered);
            riverDragIndex_ = -1;
            modified = true;
            refit = true;
        }
    }
    if (riverDragIndex_ >= 0) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && riverDragIndex_ < static_cast<int>(edited.riverPath.size())) {
            RiverPoint& point = edited.riverPath[static_cast<size_t>(riverDragIndex_)];
            // Only once the mouse has really moved: a plain click on the water must not push the point it just inserted off the
            // line (and a click on a handle must not nudge it).
            float bx = 0.0f, bz = 0.0f;
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f) && bodySpotAt(level + point.level, bx, bz)) { point.x = bx; point.z = bz; modified = true; }
        } else {
            if (riverDragIndex_ < static_cast<int>(edited.riverPath.size())) { modified = true; refit = true; }   // released: fit once, not every frame of the drag
            riverDragIndex_ = -1;
        }
    }
    if (riverSpringMode_ && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        riverSpringMode_ = false;   // the first Esc only cancels the spring
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape) || (viewportHovered_ && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
        riverEditMode_ = false;
        riverSpringMode_ = false;
        riverDragIndex_ = -1;
    }
    if (modified) {
        if (riverFollowGround_ && terrain) FollowTerrainLevels(water, edited);   // the water runs downhill from point to point
        if (refit && riverAutoFit_) FitRiverPlane(water, edited);
        water->SetSettings(edited);
    }
}

// Centres the water body on its river and sizes the plane to cover it: the water exists only where the plane does. The points
// are in the body's frame, so moving the body by the middle of their box and shifting every point the other way leaves the river
// exactly where it was in the world. (Before, "Fit plane" grew the plane around wherever the body happened to be.)
void EditorLayer::FitRiverPlane(Water* water, WaterSettings& settings)
{
    if (settings.riverPath.size() < 2) return;
    float minX = 1e30f, maxX = -1e30f, minZ = 1e30f, maxZ = -1e30f, widest = 0.0f;
    for (const RiverPoint& p : settings.riverPath) {
        minX = (std::min)(minX, p.x); maxX = (std::max)(maxX, p.x);
        minZ = (std::min)(minZ, p.z); maxZ = (std::max)(maxZ, p.z);
        widest = (std::max)(widest, p.width);
    }
    const float centreX = 0.5f * (minX + maxX), centreZ = 0.5f * (minZ + maxZ);
    const XMFLOAT2 centreInWorld = water->BodyToWorld(centreX, centreZ);   // asked before anything moves
    const float margin = 0.5f * widest + settings.riverBank + 10.0f;
    for (RiverPoint& p : settings.riverPath) { p.x -= centreX; p.z -= centreZ; }
    settings.size = (std::clamp)(maxX - minX + 2.0f * margin, WaterSettings::kSizeMin, WaterSettings::kSizeMax);
    settings.sizeZ = (std::clamp)(maxZ - minZ + 2.0f * margin, WaterSettings::kSizeMin, WaterSettings::kSizeMax);
    XMFLOAT3 position, rotation, scale;
    water->GetEditorTransform(position, rotation, scale);
    water->SetSettings(settings);                      // the new size and the shifted points...
    position.x = centreInWorld.x;
    position.z = centreInWorld.y;                      // (BodyToWorld's second value is world z)
    water->SetEditorTransform(position, rotation, scale);   // ...then the body to the middle of them (the level, y, stays)
}

// Digs the channel along the curve into the terrain: ONE undo step, the same kind as a sculpt stroke.
void EditorLayer::CarveRiverChannel(Water* water)
{
    Terrain* terrain = scene_ ? scene_->GetTerrain() : nullptr;
    if (!terrain || !water) return;
    const WaterSettings& s = water->GetSettings();
    const std::vector<RiverPath::Sample>& path = water->GetRiverWorldPath();
    if (path.size() < 2) { riverStatus_ = "Add at least two points first."; return; }

    // In a terrain world the river is dug into the WORLD, not into one tile: tiles are rebuilt from the world as they stream, so
    // a carve in a tile's heights would be lost with it, and one river crosses many tiles (lesson 51).
    if (auto* world = scene_->FindFirst<TerrainWorld>()) {
        const float unit = world->GetSettings().heightScale;   // metres of a sample value of 1 (a tile's ground starts at height 0)
        WorldRiver river;
        river.path = path;
        for (RiverPath::Sample& sample : river.path) sample.level /= unit;   // the river's own fall, in sample values like the level
        river.carve.level = s.height / unit;
        river.carve.depth = s.riverDepth / unit;
        river.carve.bankWidth = s.riverBank;
        river.carve.bankHeight = s.riverBankHeight / unit;
        const int id = world->AddRiver(river);
        if (id == 0) { riverStatus_ = "The world cannot hold another river (or this one has no length)."; return; }
        river.id = id;
        const SceneObjectId worldId = world->GetId();
        history_.Push(std::make_unique<LambdaCommand>("Carve river",
            [this, worldId, id]() { if (scene_) if (auto* w = dynamic_cast<TerrainWorld*>(scene_->Find(worldId))) w->RemoveRiver(id); },
            [this, worldId, river]() { if (scene_) if (auto* w = dynamic_cast<TerrainWorld*>(scene_->Find(worldId))) w->AddRiver(river); }));
        riverStatus_ = "Channel carved into the world: the tiles it crosses are rebuilt. Ctrl+Z takes it out.";
        return;
    }
    HeightfieldPatch patch = terrain->CarveRiver(path, s.height, s.riverDepth, s.riverBank, s.riverBankHeight);
    if (patch.IsEmpty()) { riverStatus_ = "Nothing to dig: the ground already has this shape."; return; }
    RecordSculptStroke(terrain->GetId(), std::move(patch), "Carve river");
    riverStatus_ = "Channel carved. Ctrl+Z puts the ground back.";
}

// Sets each point's water level from the ground under it, never higher than the point before: a river runs downhill (lesson 41).
// The same thing the "Follow terrain" button does; now also done as the river is drawn (lesson 45).
void EditorLayer::FollowTerrainLevels(Water* water, WaterSettings& s)
{
    Terrain* terrain = scene_ ? scene_->GetTerrain() : nullptr;
    if (!terrain || !water || s.riverPath.empty()) return;
    // The ground under each point; a point beyond the terrain keeps the height of the one before it.
    std::vector<float> ground(s.riverPath.size(), s.height);
    for (size_t i = 0; i < s.riverPath.size(); ++i) {
        const XMFLOAT2 w = water->BodyToWorld(s.riverPath[i].x, s.riverPath[i].z);
        float groundY = 0.0f;
        if (scene_->GetGroundHeight(w.x, w.y, groundY)) ground[i] = groundY;   // any tile of a world, or the terrain
        else if (i > 0) ground[i] = ground[i - 1];
    }
    RiverPath::FollowGround(s.riverPath, ground, s.height);
}

// Water runs downhill, so the river does not have to be drawn: from a spring, walk toward the steepest way down until the sea, the
// edge of the terrain or a basin (RiverPath::FlowDownhill, tested in tests/river_test.cpp), then keep a few control points the
// author can still move. `extend` keeps the existing points and continues from the last one.
bool EditorLayer::FlowDownhillFrom(Water* water, WaterSettings& s, float bodyX, float bodyZ, bool extend)
{
    Terrain* terrain = scene_ ? scene_->GetTerrain() : nullptr;
    if (!terrain || !water) { riverStatus_ = "A river can only find its way over terrain: add one first."; return false; }

    RiverPath::RouteParams params;
    params.stopHeight = riverStopHeight_;
    params.startWidth = riverStartWidth_;
    params.endWidth = riverEndWidth_;
    std::vector<RiverPoint> kept;   // the points that stay when this is a continuation
    if (extend && !s.riverPath.empty()) {
        kept = s.riverPath;
        kept.pop_back();   // the route starts exactly where the last point is, and gives that point back
        params.startWidth = s.riverPath.back().width;
        params.endWidth = (std::max)(params.startWidth, riverEndWidth_);
        params.maxPoints = (std::clamp)(RiverPath::kMaxPoints - static_cast<int>(kept.size()), 2, params.maxPoints);
        if (static_cast<int>(kept.size()) + 2 > RiverPath::kMaxPoints) { riverStatus_ = "Too many points to continue: delete some first."; return false; }
    }

    // The walk happens in the body's own frame, where the control points live; each question about the ground is turned into
    // world coordinates first. (Everything moves and turns with the body, so the river keeps its place when the body moves.)
    const Scene* scene = scene_;
    const RiverPath::GroundFn ground = [water, scene](float bodyPointX, float bodyPointZ, float& groundY) {
        const XMFLOAT2 w = water->BodyToWorld(bodyPointX, bodyPointZ);
        return scene->GetGroundHeight(w.x, w.y, groundY);   // any tile of a world, or the terrain
    };
    const RiverPath::Route route = RiverPath::FlowDownhill(ground, bodyX, bodyZ, params);
    if (route.points.size() < 2) {
        riverStatus_ = route.end == RiverPath::RouteEnd::NoGround ? "That spot is not on the terrain."
                     : route.end == RiverPath::RouteEnd::Sea     ? "That spot is already at the sea: pick a higher one, or lower 'Stop at height'."
                                                                   : "The ground has no way down from there (a basin). Pick a spot on a slope.";
        return false;
    }
    s.riverPath = std::move(kept);
    s.riverPath.insert(s.riverPath.end(), route.points.begin(), route.points.end());

    const char* ending = "";
    switch (route.end) {
        case RiverPath::RouteEnd::Sea:           ending = "It reaches the sea."; break;
        case RiverPath::RouteEnd::EdgeOfTerrain: ending = "It runs off the edge of the terrain."; break;
        case RiverPath::RouteEnd::Pit:           ending = "It ends in a basin: a lake would form there."; break;
        case RiverPath::RouteEnd::TooLong:       ending = "It stopped at the length limit: press Continue downhill for more."; break;
        default: break;
    }
    char text[200];
    std::snprintf(text, sizeof(text), "Found a river %.0f long that falls %.1f. %s", route.length, route.fall, ending);
    riverStatus_ = text;
    return true;
}

// Everything that still has to be done to a drawn river, in the one order that works: the water's fall from the ground, the plane
// fitted around the curve, the channel dug. Each of the three is still a button of its own for when only one is wanted.
void EditorLayer::FinishRiver(Water* water, WaterSettings& s)
{
    if (!water || s.riverPath.size() < 2) { riverStatus_ = "Add at least two points first."; return; }
    Terrain* terrain = scene_ ? scene_->GetTerrain() : nullptr;
    FollowTerrainLevels(water, s);
    FitRiverPlane(water, s);   // sets the settings and moves the body, so the curve in the world is new...
    if (!terrain) { riverStatus_ = "The plane is fitted. (There is no terrain to carve.)"; return; }
    water->RefreshRiverFlow();   // ...and is measured again now, not at the next frame: the carve reads it
    CarveRiverChannel(water);
}

// The part of the Current section that makes a river a curve: the points, the carve, and the plane that has to cover it.
void EditorLayer::DrawRiverPathInspector(Water* water, WaterSettings& s, bool& changed)
{
    ImGui::SeparatorText("River curve");
    if (s.HasRiverPath()) {
        ImGui::TextDisabled("%d points, %.0f units long. The current follows the curve (Flow direction is ignored).",
                            static_cast<int>(s.riverPath.size()), RiverPath::Length(water->GetRiverWorldPath()));
    } else {
        ImGui::TextDisabled("No curve: the river is a straight rectangle. Place two or more points to make it wind.");
    }
    if (s.HasRiverPath() && RiverPath::TightestBendRatio(water->GetRiverWorldPath()) < 1.0f) {
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.3f, 1.0f), "A bend is tighter than the river is wide: ripples pinch on its inside.");
        ImGui::SetItemTooltip("Space the points further apart, make the turn gentler, or narrow the river there.");
    }
    if (ImGui::Checkbox("Draw in viewport", &riverEditMode_)) riverDragIndex_ = -1;
    ImGui::SetItemTooltip("Click on the water's level: on the river it adds a bend point, anywhere else it extends the nearer end.\nDrag a point to move it, right-click one to delete it. Enter or Esc = done.\nThe river runs from point 1 (upstream) to the last point; Reverse flips it.");
    ImGui::Checkbox("Fit the plane as I draw", &riverAutoFit_);
    ImGui::SetItemTooltip("Re-centres the body on the river and sizes the plane to cover it whenever a point is added, moved or deleted.");
    ImGui::Checkbox("Aim at the ground", &riverAimAtGround_);
    ImGui::SetItemTooltip("On: a click lands on the terrain under the cursor, so the point is where you see it.\nOff: the click is aimed at the water's own level, which is far from the cursor wherever the land is higher or lower.");
    ImGui::Checkbox("Water follows the ground", &riverFollowGround_);
    ImGui::SetItemTooltip("While you draw, every point's water level is set from the ground under it (never rising downstream), so the river runs downhill.\nSets the 'lvl' of every point each time you change the points. Turn it off to set the levels yourself.");

    ImGui::SeparatorText("Let the river find its way");
    {
        Terrain* findTerrain = scene_ ? scene_->GetTerrain() : nullptr;
        ImGui::BeginDisabled(!findTerrain);
        if (ImGui::Button(riverSpringMode_ ? "Click the spring in the viewport..." : "Pick a spring")) {
            riverSpringMode_ = !riverSpringMode_;
            if (riverSpringMode_) riverEditMode_ = true;
            riverDragIndex_ = -1;
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Then click the terrain where the river should start. Water runs downhill, so the river is found from there:\nit follows the valley to the sea, the edge of the terrain or a basin. You can still move its points.\nReplaces the current points (Ctrl+Z brings them back). Esc cancels.");
        ImGui::SameLine();
        ImGui::BeginDisabled(!findTerrain || s.riverPath.empty());
        if (ImGui::Button("Continue downhill")) {
            const RiverPoint last = s.riverPath.back();
            if (FlowDownhillFrom(water, s, last.x, last.z, true)) {
                if (riverFollowGround_) FollowTerrainLevels(water, s);
                if (riverAutoFit_) FitRiverPlane(water, s);
                changed = true;
            }
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("From the last point, keep going downhill to the sea (or the edge of the terrain), keeping the points you placed.");
        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderFloat("Stop at height", &riverStopHeight_, -50.0f, 200.0f, "%.1f");
        ImGui::SetItemTooltip("The river ends when the ground drops to this height (world Y): the level of your sea or lake.");
        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderFloat("Width at the spring", &riverStartWidth_, RiverPath::kMinWidth, 60.0f, "%.1f");
        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderFloat("Width at the mouth", &riverEndWidth_, RiverPath::kMinWidth, 120.0f, "%.1f");
        ImGui::SetItemTooltip("Rivers widen as they go: the width grows from the spring to the mouth.");
        if (!s.HasRiverPath() && !riverStatus_.empty()) ImGui::TextWrapped("%s", riverStatus_.c_str());
    }

    ImGui::SeparatorText("Points");
    int remove = -1;
    for (size_t i = 0; i < s.riverPath.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        RiverPoint& p = s.riverPath[i];
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%d", static_cast<int>(i) + 1);
        ImGui::SameLine(28.0f);
        float xz[2] = { p.x, p.z };
        ImGui::SetNextItemWidth(130.0f);
        if (ImGui::DragFloat2("##position", xz, 0.5f, -WaterSettings::kSizeMax, WaterSettings::kSizeMax, "%.1f")) { p.x = xz[0]; p.z = xz[1]; changed = true; }
        ImGui::SetItemTooltip("Position in the body's own frame: world units from its centre, before it is turned (x, z).");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(66.0f);
        changed |= ImGui::DragFloat("##width", &p.width, 0.2f, RiverPath::kMinWidth, RiverPath::kMaxWidth, "wide %.1f");
        ImGui::SetItemTooltip("The width of the water here, bank to bank.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        changed |= ImGui::DragFloat("##level", &p.level, 0.1f, -500.0f, 500.0f, "lvl %.1f");
        ImGui::SetItemTooltip("The height of the water here, relative to the body's own level (negative = lower).\nThe river falls from one point's level to the next. 0 at every point = a level river.");
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) remove = static_cast<int>(i);
        ImGui::PopID();
    }
    if (remove >= 0) { s.riverPath.erase(s.riverPath.begin() + remove); changed = true; }

    if (ImGui::Button("Add point") && s.riverPath.size() < static_cast<size_t>(RiverPath::kMaxPoints)) {
        RiverPoint point;
        if (!s.riverPath.empty()) {   // 20 units further along the way the river was already going
            const RiverPoint& last = s.riverPath.back();
            float dx = 0.0f, dz = 20.0f;
            if (s.riverPath.size() >= 2) {
                const RiverPoint& before = s.riverPath[s.riverPath.size() - 2];
                const float length = std::hypot(last.x - before.x, last.z - before.z);
                if (length > 1e-3f) { dx = (last.x - before.x) / length * 20.0f; dz = (last.z - before.z) / length * 20.0f; }
            }
            point = { last.x + dx, last.z + dz, last.width, last.level };
        } else {
            point = { 0.0f, -20.0f, 8.0f };
        }
        s.riverPath.push_back(point);
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reverse") && s.riverPath.size() >= 2) { std::reverse(s.riverPath.begin(), s.riverPath.end()); changed = true; }
    ImGui::SetItemTooltip("Swap upstream and downstream: the water runs the other way.");
    ImGui::SameLine();
    if (ImGui::Button("Clear") && !s.riverPath.empty()) { s.riverPath.clear(); riverEditMode_ = false; riverDragIndex_ = -1; changed = true; }

    if (s.HasRiverPath()) {
        if (ImGui::Button("Finish river")) {
            FinishRiver(water, s);
            changed = true;
        }
        ImGui::SetItemTooltip("Does what is left, in the right order: the water's fall from the ground, the plane fitted around the river, the channel carved into the terrain.\nThe carve is its own undo step (Ctrl+Z).");
        ImGui::SameLine();
        // The plane has to cover the river: the water exists only where the plane does (and the ground is below it).
        if (ImGui::Button("Fit plane to river")) {
            float halfX = 0.0f, halfZ = 0.0f, widest = 0.0f;
            for (const RiverPoint& p : s.riverPath) { halfX = (std::max)(halfX, std::fabs(p.x)); halfZ = (std::max)(halfZ, std::fabs(p.z)); widest = (std::max)(widest, p.width); }
            const float margin = 0.5f * widest + s.riverBank + 10.0f;
            s.size = (std::clamp)(2.0f * (halfX + margin), WaterSettings::kSizeMin, WaterSettings::kSizeMax);
            s.sizeZ = (std::clamp)(2.0f * (halfZ + margin), WaterSettings::kSizeMin, WaterSettings::kSizeMax);
            changed = true;
        }
        ImGui::SetItemTooltip("Resizes the plane (around the body's centre) to cover every point, the banks and a margin.");

        ImGui::SeparatorText("Fall");
        {
            const float fall = s.riverPath.front().level - s.riverPath.back().level;
            bool uphill = false;
            for (size_t i = 1; i < s.riverPath.size(); ++i) if (s.riverPath[i].level > s.riverPath[i - 1].level + 0.01f) uphill = true;
            if (fall == 0.0f && !uphill) ImGui::TextDisabled("Level: the water is at the same height all along.");
            else ImGui::TextDisabled("The water falls %.1f from the first point to the last.", fall);
            if (uphill) {
                ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.3f, 1.0f), "The water rises going downstream somewhere.");
                ImGui::SetItemTooltip("Follow terrain makes it run downhill, or lower a point's level. (Reverse flips the flow but not the levels.)");
            }
            Terrain* fallTerrain = scene_ ? scene_->GetTerrain() : nullptr;
            ImGui::BeginDisabled(!fallTerrain);
            if (ImGui::Button("Follow terrain") && fallTerrain) {
                FollowTerrainLevels(water, s);
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("Sets each point's level to the ground under it, never higher than the point before:\nthe river runs downhill. Where the ground rises the water stays level, and Carve digs it through.\nThe water's slope shows only where the terrain has a depth map (a body over terrain).");
            ImGui::SameLine();
            if (ImGui::Button("Make level")) { for (RiverPoint& p : s.riverPath) p.level = 0.0f; changed = true; }
            ImGui::SetItemTooltip("Every point back to the body's own level.");
        }

        ImGui::SeparatorText("Carve the channel");
        changed |= ImGui::SliderFloat("Bed depth", &s.riverDepth, 0.1f, 12.0f, "%.1f below the water");
        ImGui::SetItemTooltip("How far below the surface the bed lies in the middle of the river. It rises to meet the surface exactly at the water's edge.");
        changed |= ImGui::SliderFloat("Bank width", &s.riverBank, 0.5f, 30.0f, "%.1f");
        ImGui::SetItemTooltip("How far beyond the water's edge the bank climbs.");
        changed |= ImGui::SliderFloat("Bank height", &s.riverBankHeight, 0.0f, 30.0f, "%.1f above the water");
        ImGui::SetItemTooltip("How high the bank has climbed by the end of its width. Ground higher than that stays as a steep wall,\nso raise this where the land beside the river is high.");
        Terrain* terrain = scene_ ? scene_->GetTerrain() : nullptr;
        if (terrain) {
            // How far above the water is the ground along the middle line? (A level river through high ground is a canyon.)
            float above = 0.0f;
            const std::vector<RiverPath::Sample>& path = water->GetRiverWorldPath();
            const size_t stride = (std::max<size_t>)(1, path.size() / 128);
            for (size_t i = 0; i < path.size(); i += stride) {
                float groundY = 0.0f;
                if (terrain->GetHeightAt(path[i].x, path[i].z, groundY)) above = (std::max)(above, groundY - (s.height + path[i].level));
            }
            ImGui::TextDisabled("Ground on the middle line: up to %.1f above the water.", above);
        }
        ImGui::BeginDisabled(!terrain || water->GetRiverWorldPath().size() < 2);
        if (ImGui::Button("Carve channel into terrain")) CarveRiverChannel(water);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Digs the terrain along the curve so the water fills a real channel with banks. The water level stays where it is:\nthe river is level, so it works best across flat or gently sloping ground. One undo step.");
        if (!riverStatus_.empty()) ImGui::TextDisabled("%s", riverStatus_.c_str());
    }
}

void EditorLayer::DrawWaterInspector(Water* water)
{
    // Every water value is just a shader constant, so changes apply live - no pending copy,
    // no Apply button (the one exception, Mesh detail, rebuilds the grid in a blink).
    // We read the current settings, edit them, and write back if anything moved.
    //
    // The layout (lesson 43): who the water is (name, type, presets), then where it is and how it moves (Shape, Current), then
    // how it looks (Appearance, Waves, Reflection, Refraction, Lighting, Foam, Shoreline, Underwater), then what it costs
    // (Performance) and Debug. Each section is a group of WaterSettings (WaterSettings::VisitGroup): right-click its header
    // to put the whole section back to its defaults; right-click a control to reset that one. A dot beside a control, and a
    // * on a header, mark what differs from the default of this kind of water.
    WaterSettings s = water->GetSettings();
    const WaterSettings defaults = WaterSettings::Defaults(s.bodyType);
    using Group = WaterSettings::Group;
    bool changed = false;
    const ImU32 accent = IM_COL32(255, 170, 40, 255);

    // Per-control extras: the tooltip, a dot when the value is not the default, and "Reset to default" on right-click.
    auto decorate = [&](auto& value, const auto& def, const char* tip, const char* popupId = nullptr) {
        if (tip) ImGui::SetItemTooltip("%s", tip);
        if (!WaterSettingsDetail::Same(value, def)) {
            const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(ImGui::GetCursorScreenPos().x - 6.0f, 0.5f * (lo.y + hi.y)), 2.5f, accent);
        }
        // (A colour editor is several widgets in one and has no single id of its own, so it passes one.)
        if (ImGui::BeginPopupContextItem(popupId)) {
            if (ImGui::MenuItem("Reset to default")) { value = def; changed = true; }
            ImGui::EndPopup();
        }
    };
    auto colorEdit = [&](const char* label, DirectX::XMFLOAT3& value, const DirectX::XMFLOAT3& def, const char* tip) {
        float rgb[3] = { value.x, value.y, value.z };
        if (ImGui::ColorEdit3(label, rgb)) {
            value = { rgb[0], rgb[1], rgb[2] };
            changed = true;
        }
        decorate(value, def, tip, label);
    };
    // A section: a header that says (with a *) when something in it is not the default, and resets the section on right-click.
    // `group` Count = not a group of settings (Shape, Presets, Debug): no mark, no reset.
    auto section = [&](const char* title, const char* id, Group group, ImGuiTreeNodeFlags flags) {
        std::string label = title;
        const bool hasGroup = group != Group::Count;
        if (hasGroup && s.GroupModified(group, defaults)) label += "  *";
        label += "###";
        label += id;
        const bool open = ImGui::CollapsingHeader(label.c_str(), flags);
        if (hasGroup) {
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Right-click: put this section back to its defaults.\nA * means something in it is changed.");
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Reset section to defaults")) { s.ResetGroup(group, defaults); changed = true; }
                ImGui::EndPopup();
            }
        }
        return open;
    };

    // A scene can hold several water bodies (a lake, a pool, a river): the name tells them apart in the Hierarchy.
    {
        char nameBuffer[64];
        std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", water->GetName().c_str());
        if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) water->SetName(nameBuffer);
    }

    // The TYPE comes first: it decides how the water moves, and which of the sections below apply.
    static const char* const kTypeNames[] = { "Ocean", "Lake", "River", "Pond", "Custom" };
    int typeIndex = static_cast<int>(s.bodyType);
    if (ImGui::Combo("Type", &typeIndex, kTypeNames, IM_ARRAYSIZE(kTypeNames))) {
        s.ApplyBodyType(static_cast<WaterBodyType>(typeIndex));
        changed = true;
    }
    ImGui::SetItemTooltip("Ocean: big swells rolling with the wind.\nLake: short wind chop.\nRiver: a current - waves and ripples all travel downstream.\nPond: nearly still.\nCustom: a blank slate that shows every setting.\n\nChanging the type loads that type's starting values (waves, ripples, foam, colours) and shows only the settings that\nmatter for it. Level, size and shape stay. Ctrl+Z brings the old values back.");
    const bool river = s.HasCurrent();   // a current: a river, or a Custom body with a flow speed
    ImGui::Checkbox("Show all settings", &showAllWaterSettings_);
    ImGui::SetItemTooltip("Each type shows only what matters for it (a river needs a current, not a swell).\nTick this to see every setting for this body.");
    using WaterTypeUi::Control;
    const auto shows = [&](Control control) { return WaterTypeUi::IsShown(s.bodyType, control, showAllWaterSettings_); };


    // --- Presets: a whole look in one click, copy and paste between bodies, and the looks you saved ------------------------
    if (section("Presets", "presets", Group::Count, 0)) {
        ImGui::PushID("presets");
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

        if (ImGui::SmallButton("Copy look")) waterClipboard_ = s;
        ImGui::SetItemTooltip("Remembers this body's look (appearance, waves, reflection, refraction, lighting, foam, shoreline,\nunderwater) so you can paste it onto another body. Not its place, kind or river.");
        ImGui::SameLine();
        ImGui::BeginDisabled(!waterClipboard_.has_value());
        if (ImGui::SmallButton("Paste look") && waterClipboard_) { s.ApplyPreset(*waterClipboard_); changed = true; }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset all sections")) { for (int g = 0; g < static_cast<int>(Group::Count); ++g) s.ResetGroup(static_cast<Group>(g), defaults); changed = true; }
        ImGui::SetItemTooltip("Every section back to the defaults of this kind of water. Its level, size, shape and river (the curve you drew)\nstay as they are.");

        ImGui::SeparatorText("My presets");
        const std::string folder = (std::filesystem::path(AssetsRootText()) / "presets" / "water").string();
        if (waterPresetsDirty_ || ImGui::GetTime() - waterPresetsScanTime_ > 2.0) {
            waterPresets_ = ListWaterPresets(folder);
            waterPresetsScanTime_ = ImGui::GetTime();
            waterPresetsDirty_ = false;
            if (waterPresetSelected_ >= static_cast<int>(waterPresets_.size())) waterPresetSelected_ = -1;
        }
        auto presetName = [](const std::string& path) { return std::filesystem::path(path).stem().string(); };
        if (waterPresets_.empty()) {
            ImGui::TextDisabled("None yet. Name the current look below and save it.");
        } else {
            const std::string preview = waterPresetSelected_ >= 0 ? presetName(waterPresets_[static_cast<size_t>(waterPresetSelected_)]) : std::string("Choose...");
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::BeginCombo("##mypreset", preview.c_str())) {
                for (size_t i = 0; i < waterPresets_.size(); ++i)
                    if (ImGui::Selectable(presetName(waterPresets_[i]).c_str(), static_cast<int>(i) == waterPresetSelected_)) waterPresetSelected_ = static_cast<int>(i);
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(waterPresetSelected_ < 0);
            if (ImGui::SmallButton("Apply")) {
                WaterSettings loaded;
                std::string error;
                if (LoadWaterPreset(waterPresets_[static_cast<size_t>(waterPresetSelected_)], loaded, error)) { s.ApplyPreset(loaded); changed = true; }
                else ShowMessage("Water preset", error);
            }
            ImGui::SetItemTooltip("Takes this preset's look. The body keeps its level, size, shape, kind and river.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) ImGui::OpenPopup("Delete water preset?");
            ImGui::EndDisabled();
            if (ImGui::BeginPopupModal("Delete water preset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                const bool valid = waterPresetSelected_ >= 0 && waterPresetSelected_ < static_cast<int>(waterPresets_.size());
                ImGui::Text("Delete \"%s\"? This cannot be undone.", valid ? presetName(waterPresets_[static_cast<size_t>(waterPresetSelected_)]).c_str() : "");
                if (ImGui::Button("Delete") && valid) {
                    std::error_code ec;
                    std::filesystem::remove(waterPresets_[static_cast<size_t>(waterPresetSelected_)], ec);
                    waterPresetSelected_ = -1;
                    waterPresetsDirty_ = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        ImGui::SetNextItemWidth(150.0f);
        ImGui::InputTextWithHint("##presetname", "Name", waterPresetName_, sizeof(waterPresetName_));
        ImGui::SameLine();
        if (ImGui::SmallButton("Save current look")) {
            const std::string file = WaterPresetFileName(waterPresetName_);
            std::string error;
            if (file.empty()) ShowMessage("Water preset", "Type a name for the preset first (letters, digits, spaces, - and _).");
            else if (SaveWaterPreset(s, (std::filesystem::path(folder) / (file + kWaterPresetExtension)).string(), error)) { waterPresetsDirty_ = true; waterPresetName_[0] = '\0'; }
            else ShowMessage("Water preset", error);
        }
        ImGui::SetItemTooltip("Saves this body's look in assets/presets/water. A name that exists is replaced.");
        ImGui::PopID();
    }

    // --- Shape: where the water is and how big ---------------------------------------------------------------------------
    if (section("Shape", "shape", Group::Count, ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("shape");
        ImGui::TextDisabled("Level  %.1f   (the transform's Position Y: move the water with the gizmo)", s.height);
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
        ImGui::PopID();
    }

    // --- Current: a river's reason to exist --------------------------------------------------------------------------------
    if (shows(Control::Current) && section("Current", "current", Group::Current, ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("current");
        changed |= ImGui::SliderFloat("Flow speed", &s.flowSpeed, 0.0f, WaterSettings::kFlowSpeedMax, "%.1f units/s");
        ImGui::SetItemTooltip("How fast the waves and ripples are carried downstream.");
        decorate(s.flowSpeed, defaults.flowSpeed, nullptr);
        changed |= ImGui::SliderFloat("Flow direction", &s.flowAngle, -180.0f, 180.0f, "%.0f deg");
        ImGui::SetItemTooltip("Measured from the river's own length: 0 = toward the +Z end of the body, 180 = the other way.\nTurning the river with Rotation Y turns the current with it.");
        decorate(s.flowAngle, defaults.flowAngle, nullptr);
        ImGui::TextDisabled("Flows toward %.0f deg in the world (0 = +X, 90 = +Z)", water->GetTravelDegrees());
        ImGui::SeparatorText("How it runs");
        changed |= ImGui::SliderFloat("Bank drag", &s.bankDrag, 0.0f, 1.0f);
        ImGui::SetItemTooltip("The water drags on its bed: where the ground rises toward a bank (needs a terrain under the river)\nthe current slows down. 0 = the same speed everywhere. 1 = it crawls along the banks.");
        decorate(s.bankDrag, defaults.bankDrag, nullptr);
        changed |= ImGui::SliderFloat("Slower at the sides", &s.channelProfile, 0.0f, 1.0f);
        ImGui::SetItemTooltip("Also slows the water toward the two long edges of the plane, whatever the ground does.\nUseful on flat ground, or when the plane is wider than the channel.");
        decorate(s.channelProfile, defaults.channelProfile, nullptr);
        changed |= ImGui::SliderFloat("Ripple stretch", &s.rippleStretch, 1.0f, WaterSettings::kRippleStretchMax, "x %.1f");
        ImGui::SetItemTooltip("Flowing water streams: its ripples are longer than they are wide. 1 = round blobs.");
        decorate(s.rippleStretch, defaults.rippleStretch, nullptr);
        changed |= ImGui::SliderFloat("Rapids", &s.rapids, 0.0f, 1.0f);
        ImGui::SetItemTooltip("How far foam spreads into fast, shallow water (over rocks, along the banks). 0 = only the very edge.");
        decorate(s.rapids, defaults.rapids, nullptr);
        DrawRiverPathInspector(water, s, changed);
        ImGui::PopID();
    }

    // --- Appearance: what the water itself looks like ----------------------------------------------------------------------
    if (section("Appearance", "appearance", Group::Appearance, ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("appearance");
        colorEdit("Deep colour", s.color, defaults.color, "The colour of deep water, where the bottom can no longer be seen.");
        colorEdit("Shallow colour", s.shallowColor, defaults.shallowColor, "The colour of thin water over the bottom. The water goes from this to the deep colour as it gets deeper.");
        changed |= ImGui::SliderFloat("Deep opacity", &s.alpha, 0.05f, 1.0f);
        decorate(s.alpha, defaults.alpha, "How much of the bottom still shows through deep water. 1 = fully opaque, lower = you see further down.");
        changed |= ImGui::SliderFloat("Colour fade", &s.absorption, WaterSettings::kAbsorptionMin, WaterSettings::kAbsorptionMax, "%.2f", ImGuiSliderFlags_Logarithmic);
        decorate(s.absorption, defaults.absorption, "How quickly the colour goes from shallow to deep as the water gets thicker.");
        ImGui::PopID();
    }

    // --- Waves ----------------------------------------------------------------------------------------------------------------
    if (section("Waves", "waves", Group::Waves, ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("waves");
        if (!river && shows(Control::WavePresets)) {   // the three presets are open-water swells: a river's waves come from its type
            if (ImGui::Button("Calm"))   { s.ApplyWavePreset(WavePreset::Calm);   changed = true; }
            ImGui::SameLine();
            if (ImGui::Button("Choppy")) { s.ApplyWavePreset(WavePreset::Choppy); changed = true; }
            ImGui::SameLine();
            if (ImGui::Button("Stormy")) { s.ApplyWavePreset(WavePreset::Stormy); changed = true; }
        }
        changed |= ImGui::SliderFloat("Amplitude", &s.waveAmplitude, 0.0f, WaterSettings::kAmplitudeMax);
        decorate(s.waveAmplitude, defaults.waveAmplitude, nullptr);
        changed |= ImGui::SliderFloat("Scale", &s.waveScale, WaterSettings::kWaveScaleMin, WaterSettings::kWaveScaleMax, "%.2fx", ImGuiSliderFlags_Logarithmic);
        decorate(s.waveScale, defaults.waveScale, "Makes the whole sea bigger or smaller: every wave's length AND height times this, so the waves keep their shape.\nA big stretch of water wants big waves: raise this to make them roll instead of ripple.");
        if (!river) {
            changed |= ImGui::Checkbox("Scale waves to the water's size", &s.autoWaveScale);
            decorate(s.autoWaveScale, defaults.autoWaveScale, "A big body of water is drawn with a coarse mesh, and a coarse mesh cannot follow short waves: they fade away and the sea goes flat.\nWith this on, a big body gets bigger waves (longer and taller, the same steepness) so that the mesh can show them. A small body is not touched.");
            if (s.autoWaveScale && water->AutoWaveScale() > 1.02f) {
                const float cell = water->FinestCell();
                ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.3f, 1.0f), "Drawn %.1fx bigger to fit this size.", water->AutoWaveScale());
                ImGui::SetItemTooltip("The mesh cells of this body are %.1f units wide, so waves shorter than about %.0f units would fade out.\nThe sea is drawn %.1fx bigger on top of Scale instead. Make the water smaller, or turn the switch above off to see it flat.",
                                      cell, 6.0f * cell, water->AutoWaveScale());
            }
        }
        if (!river && shows(Control::Speed)) {
            changed |= ImGui::SliderFloat("Speed", &s.waveSpeed, 0.0f, WaterSettings::kSpeedMax);
            decorate(s.waveSpeed, defaults.waveSpeed, nullptr);
            decorate(s.waveSpeed, defaults.waveSpeed, nullptr);
        }
        if (shows(Control::Frequency)) {
            changed |= ImGui::SliderFloat("Frequency", &s.waveFrequency, WaterSettings::kFrequencyMin, WaterSettings::kFrequencyMax);
            ImGui::SetItemTooltip("Radians per world unit. Lower = longer, lazier waves.");
            decorate(s.waveFrequency, defaults.waveFrequency, nullptr);
        }
        if (!river && shows(Control::WindSource)) {   // a river's direction is its current (above)
            int source = static_cast<int>(s.windSource);
            static const char* const kSourceNames[] = { "Scene wind", "Own direction" };
            if (ImGui::Combo("Wind", &source, kSourceNames, IM_ARRAYSIZE(kSourceNames))) { s.windSource = static_cast<WindSource>(source); changed = true; }
            ImGui::SetItemTooltip("Scene wind: this body follows the Weather entry in the Hierarchy, like everything else that the wind blows.\nOwn direction: it ignores the scene wind and uses the direction below.");
            decorate(s.windSource, defaults.windSource, nullptr);
            if (s.windSource == WindSource::Custom) {
                changed |= ImGui::SliderFloat("Wind direction", &s.windDirection, 0.0f, 360.0f, "%.0f deg");
                ImGui::SetItemTooltip("The way the waves travel. 0 = toward +X, 90 = toward +Z.");
                decorate(s.windDirection, defaults.windDirection, nullptr);
            } else {
                const WindSettings& wind = scene_->GetWeather().wind;
                ImGui::TextDisabled("Following the scene wind: toward %.0f deg, strength %.1f", wind.direction, wind.strength);
            }
        }
        if (shows(Control::Spread)) {
            changed |= ImGui::SliderFloat("Direction spread", &s.directionalSpread, 0.0f, 1.0f);
            ImGui::SetItemTooltip(river ? "0 = every wave runs with the current. A river caps this low: nothing runs upstream."
                                        : "0 = every wave runs with the wind (a clean swell). 1 = waves cross from many directions.");
            decorate(s.directionalSpread, defaults.directionalSpread, nullptr);
        }
        if (shows(Control::Choppiness)) {
            changed |= ImGui::SliderFloat("Choppiness", &s.choppiness, 0.0f, 1.0f);
            ImGui::SetItemTooltip("0 = rounded sine waves. 1 = sharp crests and wide troughs (Gerstner waves). Never folds over.");
            decorate(s.choppiness, defaults.choppiness, nullptr);
        }
        if (shows(Control::WaveCount)) {
            changed |= ImGui::SliderInt("Wave count", &s.waveCount, WaterSettings::kWaveCountMin, WaterSettings::kWaveCountMax);
            ImGui::SetItemTooltip("How many waves are added together. Fewer = cheaper and simpler. Each costs a little in the\nvertex AND pixel shader.");
            decorate(s.waveCount, defaults.waveCount, nullptr);
        }
        {   // the sea model (lesson 34)
            int style = static_cast<int>(s.waveStyle);
            static const char* const kStyleNames[] = { "Classic (fixed waves)", "Spectrum (sea model)" };
            if (shows(Control::WaveModel)) {
                if (ImGui::Combo("Wave model", &style, kStyleNames, IM_ARRAYSIZE(kStyleNames))) { s.waveStyle = static_cast<WaterWaves::Style>(style); changed = true; }
                ImGui::SetItemTooltip("Classic: the original eight fixed waves. Spectrum: waves built from a sea model: they get finer and lower\ntogether, with an optional swell and patches of calmer and rougher water.");
                decorate(s.waveStyle, defaults.waveStyle, nullptr);
            }
            if (s.waveStyle == WaterWaves::Style::Spectrum) {
                if (shows(Control::Roughness)) {
                    changed |= ImGui::SliderFloat("Roughness", &s.waveRoughness, 0.0f, 1.0f);
                    ImGui::SetItemTooltip("0 = glassy: the small waves die away fast. 1 = rough: the small waves stay almost as tall.");
                    decorate(s.waveRoughness, defaults.waveRoughness, nullptr);
                }
                if (!river && shows(Control::Swell)) {   // a current has one direction: there is no cross swell on a river
                    changed |= ImGui::SliderFloat("Swell", &s.swell, 0.0f, 1.0f);
                    ImGui::SetItemTooltip("Long, low waves from their own direction under the small ones. This is what a boat heaves on.");
                    decorate(s.swell, defaults.swell, nullptr);
                    if (s.swell > 0.0f) {
                        changed |= ImGui::SliderFloat("Swell angle", &s.swellAngle, -90.0f, 90.0f, "%.0f deg from wind");
                        decorate(s.swellAngle, defaults.swellAngle, nullptr);
                        changed |= ImGui::SliderFloat("Swell length", &s.swellLength, WaterSettings::kSwellLengthMin, WaterSettings::kSwellLengthMax, "x %.1f");
                        ImGui::SetItemTooltip("The swell's wavelength as a multiple of the biggest wind wave's.");
                        decorate(s.swellLength, defaults.swellLength, nullptr);
                    }
                }
                if (shows(Control::WaveGroups)) {
                    changed |= ImGui::SliderFloat("Wave groups", &s.waveGroups, 0.0f, WaterSettings::kWaveGroupsMax);
                    ImGui::SetItemTooltip("The sea breathes: patches of calm and rough water that drift. Only ever turns waves DOWN,\nso the Amplitude above is the average and the roughest patches are a little taller.");
                    decorate(s.waveGroups, defaults.waveGroups, nullptr);
                }
                if (shows(Control::Seed)) {
                    changed |= ImGui::SliderInt("Seed", &s.waveSeed, 0, 9999);
                    decorate(s.waveSeed, defaults.waveSeed, nullptr);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Re-roll")) { s.waveSeed = (s.waveSeed * 7919 + 104729) % 10000; changed = true; }
                    ImGui::SetItemTooltip("Another seed is another sea with the same character: different angles, wavelengths and phases.");
                }
                ImGui::TextDisabled("Biggest wind wave %.1f units long", 6.2831853f / (std::max)(s.waveFrequency, 0.001f));
                if (!river && shows(Control::Swell) && s.swell > 0.0f)
                    ImGui::TextDisabled("Swell %.1f units long", 6.2831853f / (std::max)(s.waveFrequency, 0.001f) * s.swellLength);
            }
        }
        ImGui::SeparatorText("Ripples");
        changed |= ImGui::SliderFloat("Ripple strength", &s.detailNormal, 0.0f, WaterSettings::kDetailMax);
        ImGui::SetItemTooltip("Small ripples on top of the waves. 0 turns them off and skips their cost.");
        decorate(s.detailNormal, defaults.detailNormal, nullptr);
        changed |= ImGui::SliderFloat("Ripple scale", &s.detailScale, WaterSettings::kDetailScaleMin, WaterSettings::kDetailScaleMax,
                                      "%.2f", ImGuiSliderFlags_Logarithmic);
        decorate(s.detailScale, defaults.detailScale, nullptr);
        changed |= ImGui::SliderFloat("Ripple distance", &s.detailDistance, WaterSettings::kDetailDistanceMin,
                                      WaterSettings::kDetailDistanceMax, "%.0f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("Ripples fade out by this distance (also hides shimmer on far water).");
        decorate(s.detailDistance, defaults.detailDistance, nullptr);
        ImGui::PopID();
    }

    // --- Reflection: what the water mirrors -----------------------------------------------------------------------------------
    if (section("Reflection", "reflection", Group::Reflection, ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("reflection");
        // The enum's values are saved in scene files, so they cannot be reordered or split: Mode and Quality are two views of it.
        static const char* const kModeNames[] = { "Sky only (cubemap)", "Mirror (planar)" };
        static const char* const kQualityNames[] = { "Low (quarter resolution)", "Medium (half resolution)", "High (full resolution)" };
        static const ReflectionMode kQualityOrder[] = { ReflectionMode::PlanarQuarter, ReflectionMode::PlanarHalf, ReflectionMode::PlanarFull };
        const bool mirror = s.reflectionMode != ReflectionMode::Sky;
        int modeIndex = mirror ? 1 : 0;
        if (ImGui::Combo("Mode", &modeIndex, kModeNames, IM_ARRAYSIZE(kModeNames))) {
            s.reflectionMode = modeIndex == 0 ? ReflectionMode::Sky : (defaults.reflectionMode != ReflectionMode::Sky ? defaults.reflectionMode : ReflectionMode::PlanarHalf);
            changed = true;
        }
        decorate(s.reflectionMode, defaults.reflectionMode, "Planar modes draw the scene a second time from below the water, so the water\nreflects the terrain and cube too. That pass is skipped while no water is on screen.\nBodies at the same level share one image; at most 2 planar images are drawn per frame (nearest bodies first),\nthe others use the sky. Quarter resolution is the cheapest; the waves blur it anyway.");
        if (s.reflectionMode != ReflectionMode::Sky) {
            int qualityIndex = 1;
            for (int i = 0; i < 3; ++i) if (kQualityOrder[i] == s.reflectionMode) qualityIndex = i;
            if (ImGui::Combo("Quality", &qualityIndex, kQualityNames, IM_ARRAYSIZE(kQualityNames))) { s.reflectionMode = kQualityOrder[qualityIndex]; changed = true; }
            ImGui::SetItemTooltip("The size of the mirror image, relative to the screen. Higher = sharper reflections, and it costs more.\nMedium is half the width and height (a quarter of the pixels).");
        }
        changed |= ImGui::SliderFloat("Strength", &s.reflectivity, 0.0f, 1.0f);
        decorate(s.reflectivity, defaults.reflectivity, "0 = no reflection, 1 = the full reflection a water surface gives (strongest when seen at a low angle).");
        if (s.reflectionMode != ReflectionMode::Sky) {
            changed |= ImGui::SliderFloat("Distortion", &s.reflectionDistortion, 0.0f, WaterSettings::kDistortionMax, "%.3f");
            decorate(s.reflectionDistortion, defaults.reflectionDistortion, "How far the waves wobble the mirror image (a fraction of the screen).");
        }
        ImGui::PopID();
    }

    // --- Refraction: how the bottom is seen through the water ------------------------------------------------------------------
    if (section("Refraction", "refraction", Group::Refraction, ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("refraction");
        changed |= ImGui::SliderFloat("Strength", &s.refraction, 0.0f, WaterSettings::kRefractionMax, "%.3f");
        ImGui::SetItemTooltip("How much the waves bend the view of the bottom. 0 = off, and the screen copy it needs is skipped.");
        decorate(s.refraction, defaults.refraction, nullptr);
        ImGui::PopID();
    }

    // --- Lighting: the sun on the water ---------------------------------------------------------------------------------------
    if (section("Lighting", "lighting", Group::Lighting, ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("lighting");
        changed |= ImGui::SliderFloat("Crest glow", &s.scatter, 0.0f, WaterSettings::kScatterMax);
        ImGui::SetItemTooltip("Light shining through wave crests when you look toward the sun.");
        decorate(s.scatter, defaults.scatter, nullptr);
        changed |= ImGui::SliderFloat("Sun highlight", &s.glintStrength, 0.0f, WaterSettings::kGlintMax);
        decorate(s.glintStrength, defaults.glintStrength, nullptr);
        changed |= ImGui::SliderFloat("Highlight tightness", &s.glintSharpness, WaterSettings::kSharpnessMin,
                                      WaterSettings::kSharpnessMax, "%.0f", ImGuiSliderFlags_Logarithmic);
        decorate(s.glintSharpness, defaults.glintSharpness, nullptr);
        ImGui::PopID();
    }

    // --- Foam ----------------------------------------------------------------------------------------------------------------
    if (section("Foam", "foam", Group::Foam, 0)) {
        ImGui::PushID("foam");
        changed |= ImGui::SliderFloat("Intensity", &s.foamIntensity, 0.0f, WaterSettings::kFoamIntensityMax);
        decorate(s.foamIntensity, defaults.foamIntensity, "The master control: scales the shore foam and the crest foam together. 0 = no foam at all, 1 = as set below.");
        ImGui::SeparatorText("Shore foam");
        changed |= ImGui::SliderFloat("Width", &s.foamDistance, 0.0f, WaterSettings::kFoamDistanceMax, "%.2f");
        decorate(s.foamDistance, defaults.foamDistance, nullptr);
        changed |= ImGui::SliderFloat("Strength", &s.foamStrength, 0.0f, 1.0f);
        decorate(s.foamStrength, defaults.foamStrength, nullptr);
        ImGui::SeparatorText("Bubbles");
        changed |= ImGui::SliderFloat("Size", &s.foamScale, WaterSettings::kFoamScaleMin, WaterSettings::kFoamScaleMax, "x %.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("Higher = finer bubbles. Foam is a pattern of bubbles that only the strong parts of the foam fill completely:\nthe weak edges show up as thin, lacy islands.");
        decorate(s.foamScale, defaults.foamScale, nullptr);
        ImGui::SeparatorText("Crest foam");
        changed |= ImGui::SliderFloat("Amount", &s.crestFoam, 0.0f, 1.0f);
        ImGui::SetItemTooltip("White caps where a wave is squeezed to its sharpest. Needs Choppiness above 0.");
        decorate(s.crestFoam, defaults.crestFoam, nullptr);
        changed |= ImGui::SliderFloat("Threshold", &s.crestFoamThreshold, 0.0f, 1.0f);
        ImGui::SetItemTooltip("Higher = only the very sharpest crests get foam.");
        decorate(s.crestFoamThreshold, defaults.crestFoamThreshold, nullptr);
        changed |= ImGui::SliderFloat("Trail", &s.foamTrail, 0.0f, WaterSettings::kFoamTrailMax, "%.1f s");
        ImGui::SetItemTooltip("How long the foam of a breaking crest lingers behind it, thinning out. 0 = only while it breaks (the old look).\nCosts three extra looks at the wave table per foamy pixel, close to the camera only.");
        decorate(s.foamTrail, defaults.foamTrail, nullptr);
        ImGui::PopID();
    }

    // --- Shoreline: where the water meets the land -----------------------------------------------------------------------------
    if (section("Shoreline", "shoreline", Group::Shoreline, 0)) {
        ImGui::PushID("shoreline");
        changed |= ImGui::SliderFloat("Edge softness", &s.fadeDistance, WaterSettings::kFadeMin,
                                      WaterSettings::kFadeMax, "%.1f");
        ImGui::SetItemTooltip("Depth at which the water becomes fully opaque. Smaller = sharper shoreline.");
        decorate(s.fadeDistance, defaults.fadeDistance, nullptr);
        if (shows(Control::ShoreSection)) {   // rivers and ponds do not surf
            changed |= ImGui::SliderFloat("Shelter", &s.shoreShelter, 0.0f, 1.0f);
            ImGui::SetItemTooltip("Waves cannot come from the land. A wave running AWAY from a shore it is close to (the lee of an island,\nthe far bank of a lake) is turned down instead of rising and breaking on a beach it is leaving.\nWaves running toward the shore are untouched. 0 = off (the old look).");
            decorate(s.shoreShelter, defaults.shoreShelter, nullptr);
            changed |= ImGui::SliderFloat("Shore waves", &s.shoreWaves, 0.0f, 1.0f);
            ImGui::SetItemTooltip("How much the waves react to the ground under them. In shallow water they grow taller,\nthen cannot be taller than the water is deep, so they break and die at the waterline.\n0 = identical everywhere (the old look).");
            decorate(s.shoreWaves, defaults.shoreWaves, nullptr);
            changed |= ImGui::SliderFloat("Swash", &s.swash, 0.0f, WaterSettings::kSwashMax, "%.2f");
            ImGui::SetItemTooltip("How far the waterline (foam and the see-through edge) slides up and down the beach with each wave.");
            decorate(s.swash, defaults.swash, nullptr);
            int mapWidth = 0, mapHeight = 0;
            water->GetShoreMapSize(mapWidth, mapHeight);
            if (water->HasShoreDepth()) ImGui::TextDisabled("Depth map: %d x %d texels. See Advanced > Debug > Shore depth.", mapWidth, mapHeight);
            else ImGui::TextDisabled("No terrain under this body: the shore effects are off.");
        }
        ImGui::PopID();
    }

    // --- Underwater: how the world looks with the camera under this water ---------------------------------------------------------
    if (section("Underwater", "underwater", Group::Underwater, 0)) {
        ImGui::PushID("underwater");
        UnderwaterSettings& u = s.underwater;
        UnderwaterBody body;
        body.shallowColor = { s.shallowColor.x, s.shallowColor.y, s.shallowColor.z };
        body.deepColor = { s.color.x, s.color.y, s.color.z };
        body.absorption = s.absorption;

        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("How the world looks with the camera UNDER this water: the fog, its colour, and the light. Automatic matches the water's colours and absorption.");
        ImGui::PopTextWrapPos();
        bool custom = u.custom;
        if (ImGui::Checkbox("Customize", &custom)) {
            if (custom) u = UnderwaterMath::Derived(body, u);   // start from exactly what you were seeing
            else u.custom = false;                              // the custom numbers are kept for next time
            changed = true;
        }
        ImGui::SetItemTooltip("Off: derived from this water's colours and Absorption. On: you set them here (they start at the automatic values).");

        ImGui::BeginDisabled(!u.custom);
        changed |= ImGui::ColorEdit3("Near the surface", u.shallowColor.data());
        changed |= ImGui::ColorEdit3("Far down", u.deepColor.data());
        changed |= ImGui::SliderFloat("Visibility", &u.visibility, 1.0f, 200.0f, "%.1f m", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("How far you can see. The fog hides 95%% of what is this far away: murky pond about 3, clear ocean 30 or more.");
        changed |= ImGui::SliderFloat("Colour depth", &u.colorDepth, 0.5f, 100.0f, "%.1f m", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("The depth over which the near-the-surface colour turns into the far-down one.");
        changed |= ImGui::SliderFloat("Light depth", &u.lightDepth, 1.0f, 200.0f, "%.1f m", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("The depth at which the light has dimmed to about a third. Smaller = it gets dark quickly as you dive.");
        ImGui::EndDisabled();

        ImGui::SeparatorText("Light and surface");
        changed |= ImGui::SliderFloat("Brightness", &u.brightness, 0.0f, 3.0f, "%.2f");
        ImGui::SetItemTooltip("How much light reaches under the surface in daylight.");
        changed |= ImGui::SliderFloat("Night brightness", &u.nightBrightness, 0.0f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("The light level when no sun or moon is up. 0 = pitch dark.");
        changed |= ImGui::SliderFloat("Sun glow", &u.sunGlow, 0.0f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("Looking up toward the sun glows with its colour.");
        changed |= ImGui::SliderFloat("Clear radius", &u.startDistance, 0.0f, 30.0f, "%.1f m");
        ImGui::SetItemTooltip("Nothing nearer than this is fogged: a bubble of clear water around the camera.");
        changed |= ImGui::SliderFloat("Surface from below", &u.surfaceMirror, 0.0f, 2.0f, "%.2f");
        ImGui::SetItemTooltip("How bright the surface looks from underneath, outside the round window onto the sky.");

        // Tune it without diving: look at the whole scene as if the camera were under this water.
        bool previewing = scene_->GetUnderwaterPreview() == water->GetId();
        if (ImGui::Checkbox("Preview from above", &previewing)) scene_->SetUnderwaterPreview(previewing ? water->GetId() : 0, underwaterPreviewDepth_);
        ImGui::SetItemTooltip("Shows the whole view as if the camera were under this water, without moving it. Not saved; it ends when you select something else.");
        if (previewing) {
            if (ImGui::SliderFloat("Preview depth", &underwaterPreviewDepth_, 0.0f, 60.0f, "%.1f m")) scene_->SetUnderwaterPreview(water->GetId(), underwaterPreviewDepth_);
        }
        u.Clamp();
        ImGui::PopID();
    }

    // --- Performance and Debug -----------------------------------------------------------------------------------------------------
    if (section("Performance", "performance", Group::Performance, 0)) {
        ImGui::PushID("performance");
        {
            const float cell = water->FinestCell();
            ImGui::TextDisabled("Finest mesh cell  %.2f units", cell);
            ImGui::SetItemTooltip("One grid cell of the water's mesh at its finest. A wave is shown in full when it is at least 6 cells long (here %.0f units) and fades out below 3 cells.\nA bigger body has bigger cells: the mesh has a fixed number of vertices to spend.", 6.0f * cell);
        }
        static const char* const kMeshNames[] = { "Low (16 cells per tile)", "Medium (32 cells per tile)", "High (64 cells per tile)" };
        changed |= ImGui::Combo("Mesh detail", &s.meshDetail, kMeshNames, IM_ARRAYSIZE(kMeshNames));
        ImGui::SetItemTooltip("Grid cells along one tile edge at the finest level. The water is cut into square tiles:\n8 x 8 for a square body, long rows for a river.");
        decorate(s.meshDetail, defaults.meshDetail, nullptr);
        changed |= ImGui::SliderFloat("Full detail until", &s.lodDistance, WaterSettings::kLodDistanceMin,
                                      WaterSettings::kLodDistanceMax, "%.0f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("Tiles farther than this use fewer triangles. 4 levels, switching at 1x, 2.2x and 4.5x this distance.");
        decorate(s.lodDistance, defaults.lodDistance, nullptr);
        changed |= ImGui::Checkbox("Skip tiles off screen", &s.frustumCulling);
        decorate(s.frustumCulling, defaults.frustumCulling, nullptr);
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
        if (stats.tilesOffRiver > 0) ImGui::TextDisabled("%d tiles skipped: away from the river", stats.tilesOffRiver);
        if (gpuTimings_.valid) {
            using P = GpuPass;
            const float* ms = gpuTimings_.ms;
            ImGui::TextDisabled("GPU ms: shadows %.2f  mirrors %.2f  opaque %.2f  water %.2f  post %.2f  (frame %.2f)",
                                ms[static_cast<int>(P::Shadows)], ms[static_cast<int>(P::Reflections)], ms[static_cast<int>(P::Opaque)],
                                ms[static_cast<int>(P::Water)], ms[static_cast<int>(P::PostProcess)], ms[static_cast<int>(P::Total)]);
        }
        ImGui::PopID();
    }
    if (section("Debug", "debug", Group::Count, 0)) {
        ImGui::PushID("debug");
        static const char* const kDebugNames[] = { "Off", "Normals", "Depth (shore / deep)", "Foam", "Fresnel", "Crest height", "Shore depth (red land, green depth, blue surf)" };
        int debugView = static_cast<int>(s.debugView);
        if (ImGui::Combo("View", &debugView, kDebugNames, IM_ARRAYSIZE(kDebugNames))) {
            s.debugView = static_cast<WaterDebugView>(debugView);
            changed = true;
        }
        ImGui::SetItemTooltip("Shows one ingredient of the water on its own. Not saved with the scene.");
        changed |= ImGui::Checkbox("Wireframe", &s.wireframe);
        ImGui::SetItemTooltip("Draws the water mesh as wires so you can SEE the tiles and their detail levels.");
        ImGui::PopID();
    }

    if (changed) water->SetSettings(s);
}

void EditorLayer::DrawWeatherInspector()
{
    // The scene's weather: one wind for everything that is blown about. Water bodies follow it unless they are told not to;
    // fog, rain, grass and smoke will read it from here too.
    WeatherSettings& weather = scene_->GetWeather();
    ImGui::SeparatorText("Wind");
    ImGui::SliderFloat("Direction", &weather.wind.direction, 0.0f, 360.0f, "%.0f deg");
    ImGui::SetItemTooltip("The way the wind blows TOWARD. 0 = +X, 90 = +Z. Water bodies set to follow the scene wind turn their\nwaves to match.");
    ImGui::SliderFloat("Strength", &weather.wind.strength, 0.0f, WindSettings::kStrengthMax, "%.2f");
    ImGui::SetItemTooltip("0 = dead calm, 1 = the sea each water body was set up for, 2 = a much rougher one.\nOnly the HEIGHT of the waves follows it; wavelength and speed stay as the body's settings say.");
    weather.Clamp();
    int followers = 0;
    for (const auto& object : scene_->GetObjects()) {
        if (const auto* water = dynamic_cast<const Water*>(object.get())) {
            if (water->GetSettings().windSource == WindSource::Scene && !water->GetSettings().HasCurrent()) ++followers;
        }
    }
    ImGui::TextDisabled("%d water %s following the scene wind.", followers, followers == 1 ? "body is" : "bodies are");

    ImGui::SeparatorText("Fog");
    FogSettings& fog = weather.fog;
    ImGui::Checkbox("Fog", &fog.enabled);
    ImGui::SetItemTooltip("The farther away something is, the more it takes the fog's colour: far hills melt into the sky.");
    ImGui::BeginDisabled(!fog.enabled);
    ImGui::Checkbox("Follow the sky colour", &fog.followSky);
    ImGui::SetItemTooltip("With a procedural sky: the fog takes the colour of the sky's horizon, so it is dark at night and orange at sunset.\nWith any other sky, the Colour below is used.");
    ImGui::ColorEdit3("Colour##fog", fog.color.data());
    ImGui::SetItemTooltip("Pick something close to the sky's horizon colour: the sky's horizon is blended toward it (Horizon blend).");
    ImGui::SliderFloat("Brightness##fog", &fog.brightness, 0.0f, 4.0f, "%.2f");
    ImGui::SetItemTooltip("The scene is HDR: in strong sun the fog may need to be brighter than 1 to look like lit mist.");
    ImGui::SliderFloat("Visibility", &fog.visibility, FogSettings::kVisibilityMin, 5000.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);
    ImGui::SetItemTooltip("The distance at which the fog hides 95%% of what is behind it. Smaller = thicker fog.");
    ImGui::SliderFloat("Start distance", &fog.startDistance, 0.0f, 500.0f, "%.0f m");
    ImGui::SetItemTooltip("Nothing nearer than this is fogged at all.");
    ImGui::SliderFloat("Height falloff", &fog.heightFalloff, 0.0f, 0.2f, "%.3f");
    ImGui::SetItemTooltip("0 = the same thickness at every height. Larger = the fog thins out faster going up: a mist lying in the valleys.");
    ImGui::DragFloat("Base height", &fog.baseHeight, 0.5f, -1000.0f, 1000.0f, "%.1f");
    ImGui::SetItemTooltip("The height where the fog is at full thickness (only matters with a height falloff).");
    ImGui::SliderFloat("Sun glow", &fog.sunGlow, 0.0f, 1.0f, "%.2f");
    ImGui::SetItemTooltip("Fog toward the sun glows with the sun's colour.");
    ImGui::SliderFloat("Horizon blend", &fog.skyBlend, 0.0f, 1.0f, "%.2f");
    ImGui::SetItemTooltip("How much the sky near the horizon takes the fog's colour, so the far ground fades into it with no seam.");
    ImGui::SliderFloat("Max opacity", &fog.maxOpacity, 0.0f, 1.0f, "%.2f");
    ImGui::SetItemTooltip("Fog never hides more than this: below 1 the far distance stays faintly visible.");
    ImGui::EndDisabled();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Under water the water body takes over: its own colours and absorption set how far you can see.");
    ImGui::PopTextWrapPos();
    weather.Clamp();
}

void EditorLayer::RescanSkyboxes()
{
    namespace fs = std::filesystem;
    skyboxFolders_.clear();
    skyboxScanRoot_ = AssetsRootText();

    struct Root { fs::path path; const char* tag; };
    std::vector<Root> roots = { { fs::path("assets"), "engine" } };
    if (project_.IsOpen()) roots.push_back({ project_.AssetsRoot(), "project" });

    for (const Root& root : roots) {
        std::error_code error;
        if (!fs::is_directory(root.path, error)) continue;
        for (fs::recursive_directory_iterator it(root.path, fs::directory_options::skip_permission_denied, error), end;
             !error && it != end; it.increment(error)) {
            std::error_code entryError;
            if (!it->is_directory(entryError)) continue;
            const std::string folder = Skybox::NormalizeFolder(it->path().string());
            if (!Skybox::IsSkyboxFolder(folder)) continue;
            it.disable_recursion_pending();   // a skybox folder holds six images: nothing worth looking inside
            SkyboxChoice choice;
            choice.folder = folder;
            choice.label = it->path().lexically_relative(root.path).generic_string() + "  (" + root.tag + ")";
            skyboxFolders_.push_back(std::move(choice));
        }
    }
}

void EditorLayer::EnsureSkyboxScan()
{
    if (skyboxScanRoot_ != AssetsRootText()) RescanSkyboxes();   // the first time, or another project was opened
}

// One undo step for "the sky was swapped". The state is captured before and after because the folder is part of the
// skybox's saved state; objectDetector_ is reset so the property detector does not report the same change a second time.
bool EditorLayer::ChangeSkybox(Skybox* skybox, const std::string& folder)
{
    if (!skybox) return false;
    const std::string before = CaptureObjectState(*skybox);
    if (!skybox->LoadFromFolder(folder)) {
        ShowMessage("Could not load skybox", SkyFaces::Hint() + "\n\n" + folder);
        return false;
    }
    const std::string after = CaptureObjectState(*skybox);
    if (before != after) {
        RecordPropertyChange(skybox->GetId(), "Change skybox", before, after);
        objectDetector_.Reset();
        skyDetector_.Reset();
    }
    return true;
}

void EditorLayer::UseSkyboxFolder(LayerContext& ctx, const std::string& folder)
{
    const std::string normalised = Skybox::NormalizeFolder(folder);
    if (Skybox* existing = ctx.scene.GetSkybox()) {
        ChangeSkybox(existing, normalised);
        return;
    }
    AddSkybox(ctx, normalised);   // a scene without a sky (an old file): make one
}

// Switching which sky is showing is ONE undo step that remembers which sky it was before.
void EditorLayer::ActivateSky(Skybox* sky)
{
    if (!sky || !scene_) return;
    Skybox* previous = scene_->GetSkybox();
    if (previous == sky && sky->IsActive()) return;
    const SceneObjectId previousId = previous ? previous->GetId() : 0;
    const SceneObjectId newId = sky->GetId();
    const std::string name = sky->GetEditorName();
    if (!scene_->SetActiveSkybox(sky)) return;
    auto setActive = [this](SceneObjectId id) {
        if (auto* target = dynamic_cast<Skybox*>(scene_->Find(id))) scene_->SetActiveSkybox(target);
    };
    history_.Push(std::make_unique<LambdaCommand>("Use sky " + name, [setActive, previousId]() { setActive(previousId); },
                                                  [setActive, newId]() { setActive(newId); }));
    objectDetector_.Reset();   // the "active" flag changed under the detectors: not an edit by hand
    skyDetector_.Reset();
}

// Adds another sky made from `folder` and makes it the active one. Undo removes it again AND brings the previous sky back.
void EditorLayer::AddSkybox(LayerContext& ctx, const std::string& folder)
{
    const std::string normalised = Skybox::NormalizeFolder(folder);
    Skybox* previous = ctx.scene.GetSkybox();
    const SceneObjectId previousId = previous ? previous->GetId() : 0;
    Skybox* created = ctx.scene.Add<Skybox>(ctx.renderer.GetDevice(), normalised);
    if (!created) return;
    if (!created->GetCubemapView()) {   // the folder could not be loaded: do not leave a blank sky behind
        ctx.scene.Remove(created->GetId());
        ShowMessage("Could not load skybox", SkyFaces::Hint() + "\n\n" + normalised);
        return;
    }
    const std::string name = std::filesystem::path(normalised).filename().string();
    if (!name.empty()) created->SetName(name);
    ctx.scene.SetActiveSkybox(created);

    auto holder = std::make_shared<std::unique_ptr<SceneObject>>();
    auto position = std::make_shared<size_t>(0);
    const SceneObjectId id = created->GetId();
    history_.Push(std::make_unique<LambdaCommand>(
        "Add sky " + name,
        [this, holder, position, id, previousId]() {
            *holder = scene_->Detach(id, position.get());
            if (selectedId_ == id) selectedId_ = 0;
            if (auto* before = dynamic_cast<Skybox*>(scene_->Find(previousId))) scene_->SetActiveSkybox(before);
        },
        [this, holder, position]() {
            if (!*holder) return;
            SceneObject* attached = scene_->Attach(std::move(*holder), *position);
            if (auto* sky = dynamic_cast<Skybox*>(attached)) scene_->SetActiveSkybox(sky);
            Select(attached);
        }));
    Select(created);
    objectDetector_.Reset();
}

// The Environment tab: which sky is showing, and a way to add another. The sky's own settings (cubemap, rotation, tint,
// intensity) are in the Inspector when the sky is selected: one place for them, not two.
void EditorLayer::DrawSkiesSection()
{
    ImGui::SeparatorText("Sky");
    Skybox* active = scene_->GetSkybox();
    if (!active) {
        ImGui::TextDisabled("This scene has no sky.");
        if (ImGui::Button("Add the default sky")) pendingAddSkyFolder_ = Skybox::DefaultFolder();
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float buttons = ImGui::CalcTextSize("Edit").x + ImGui::GetFrameHeight() + style.FramePadding.x * 2.0f + style.ItemSpacing.x * 2.0f;
    ImGui::SetNextItemWidth(-buttons);
    if (ImGui::BeginCombo("##activeSky", active->GetEditorName())) {
        for (Skybox* sky : scene_->GetSkyboxes()) {
            const std::string row = std::string(sky->GetEditorName()) + "###sky" + std::to_string(sky->GetId());
            if (ImGui::Selectable(row.c_str(), sky == active) && sky != active) pendingActiveSkyId_ = sky->GetId();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("The sky that is drawn and lights the scene. Switching is one undo step.");
    ImGui::SameLine();
    if (ImGui::Button("Edit")) Select(active);
    ImGui::SetItemTooltip("Select this sky: its cubemap, rotation, tint and intensity are in the Inspector.");
    ImGui::SameLine();
    if (ImGui::Button("+##addSky")) ImGui::OpenPopup("##addSkyPopup");
    ImGui::SetItemTooltip("Add another sky to the scene (a day and a night one, say) and switch between them.");
    if (ImGui::BeginPopup("##addSkyPopup")) {
        EnsureSkyboxScan();
        ImGui::TextDisabled("Add a sky from:");
        for (const SkyboxChoice& choice : skyboxFolders_) {
            if (ImGui::Selectable(choice.label.c_str())) pendingAddSkyFolder_ = choice.folder;
        }
        if (skyboxFolders_.empty()) ImGui::TextDisabled("No skybox folders found under assets/.");
        ImGui::EndPopup();
    }
    const size_t count = scene_->GetSkyboxes().size();
    ImGui::TextDisabled("%d %s in this scene.", static_cast<int>(count), count == 1 ? "sky" : "skies");
}

bool EditorLayer::SetSkyMode(Skybox* sky, SkyMode mode)
{
    if (!sky || sky->GetMode() == mode) return true;
    const std::string before = CaptureObjectState(*sky);
    if (!sky->SetMode(mode)) {
        ShowMessage("Could not switch the sky", "The procedural sky could not be created. See the log for the Direct3D error.");
        return false;
    }
    const std::string after = CaptureObjectState(*sky);
    RecordPropertyChange(sky->GetId(), mode == SkyMode::Procedural ? "Procedural sky" : "Cubemap sky", before, after);
    objectDetector_.Reset();
    scene_->InvalidateEnvironment();   // a different sky: the lighting is baked from the new one next frame
    return true;
}

void EditorLayer::DrawTimeOfDayInspector()
{
    TimeOfDaySettings& tod = scene_->GetWeather().timeOfDay;
    ImGui::SeparatorText("Clock");
    ImGui::Checkbox("Follow the clock", &tod.enabled);
    ImGui::SetItemTooltip("On: the lights that have a Sun or Moon role rise and set with the time, and fade and change colour with their height.\nA procedural sky shows the same sun, moon and stars.\nOff: every light stays exactly as you set it.");

    // The time itself. While the clock runs this shows where it is; dragging jumps it.
    float hour = (tod.enabled && tod.animate) ? scene_->GetClockHour() : tod.hour;
    const LightAngles::Vec3 sunNow = TimeOfDay::SunToLight(tod, hour);
    const std::string timeText = TimeOfDay::FormatHour(hour);
    if (ImGui::SliderFloat("Time", &hour, 0.0f, 24.0f, timeText.c_str())) {
        tod.hour = TimeOfDay::WrapHour(hour);
        scene_->SetClockHour(tod.hour);
    }
    ImGui::SetItemTooltip("Hours, 0 to 24. 6:00 is sunrise and 18:00 sunset (day and night are always 12 hours).");
    static const struct { const char* label; float hour; } kPresets[] = { { "Dawn", 6.0f }, { "Morning", 9.0f }, { "Noon", 12.0f }, { "Dusk", 18.0f }, { "Midnight", 0.0f } };
    for (size_t i = 0; i < IM_ARRAYSIZE(kPresets); ++i) {
        if (i > 0) ImGui::SameLine();
        if (ImGui::SmallButton(kPresets[i].label)) { tod.hour = kPresets[i].hour; scene_->SetClockHour(tod.hour); }
    }

    ImGui::BeginDisabled(!tod.enabled);
    if (tod.animate) {
        if (ImGui::Button("Pause")) { tod.hour = scene_->GetClockHour(); tod.animate = false; }   // freeze where it is
    } else if (ImGui::Button("Play")) {
        tod.animate = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##dayLength", &tod.dayLengthMinutes, 0.2f, 60.0f, "a day lasts %.1f min", ImGuiSliderFlags_Logarithmic);
    ImGui::SetItemTooltip("Real minutes for a whole 24 hours. Play runs the clock by itself, in the editor and in the game.");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Sun and moon");
    ImGui::SliderFloat("Noon direction", &tod.noonAzimuth, 0.0f, 360.0f, "%.0f deg");
    ImGui::SetItemTooltip("The compass direction of the sun at noon (the wind's convention: 0 = +X, 90 = +Z). It rises 90 degrees one side and sets 90 degrees the other.");
    ImGui::SliderFloat("Noon height", &tod.maxElevation, 10.0f, 90.0f, "%.0f deg");
    ImGui::SetItemTooltip("How high the sun climbs at noon. 90 = straight overhead (the tropics); lower = a northern or winter sun.");
    ImGui::TextDisabled("The sun is %.0f deg %s the horizon; the moon is opposite.", std::abs(LightAngles::Degrees(std::asin(std::clamp(sunNow.y, -1.0f, 1.0f)))),
                        sunNow.y >= 0.0f ? "above" : "below");

    int sunLights = 0, moonLights = 0;
    for (LightObject* light : scene_->GetLights()) {
        if (!light->IsDirectional()) continue;
        sunLights += light->GetRole() == LightRole::Sun ? 1 : 0;
        moonLights += light->GetRole() == LightRole::Moon ? 1 : 0;
    }
    ImGui::PushTextWrapPos(0.0f);
    if (tod.enabled && sunLights == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "No light has the Sun role, so nothing lights the day. Add one (Add > Light > Sun) or set a directional light's \"Time of day role\" in the Inspector.");
    }
    if (tod.enabled && moonLights == 0) ImGui::TextDisabled("No Moon light: the nights are lit only by the sky. Add > Light > Moon adds one.");
    ImGui::TextDisabled("%d sun light%s and %d moon light%s follow the clock.", sunLights, sunLights == 1 ? "" : "s", moonLights, moonLights == 1 ? "" : "s");
    ImGui::PopTextWrapPos();

    ImGui::SeparatorText("Sky");
    if (Skybox* sky = scene_->GetSkybox()) {
        ImGui::PushTextWrapPos(0.0f);
        if (sky->IsProcedural()) {
            ImGui::TextDisabled("The active sky (%s) is procedural: it draws the sun, the moon and the stars, and lights the scene to match.", sky->GetEditorName());
        } else {
            ImGui::TextDisabled("The active sky (%s) is a cubemap: its picture never changes with the time of day.", sky->GetEditorName());
        }
        ImGui::PopTextWrapPos();
        if (!sky->IsProcedural() && ImGui::Button("Make the active sky procedural")) SetSkyMode(sky, SkyMode::Procedural);
        if (sky->IsProcedural() && ImGui::Button("Back to the cubemap")) SetSkyMode(sky, SkyMode::Cubemap);
    } else {
        ImGui::TextDisabled("This scene has no sky.");
    }
    ImGui::BeginDisabled(!(scene_->GetSkybox() && scene_->GetSkybox()->IsProcedural()));
    ImGui::SliderFloat("Sun size", &tod.sunSize, 0.2f, 6.0f, "%.2f");
    ImGui::SliderFloat("Moon size", &tod.moonSize, 0.2f, 6.0f, "%.2f");
    ImGui::SliderFloat("Stars", &tod.starBrightness, 0.0f, 4.0f, "%.2f");
    ImGui::SliderFloat("Sunset glow", &tod.sunsetStrength, 0.0f, 3.0f, "%.2f");
    ImGui::SetItemTooltip("The warm colour along the horizon at sunrise and sunset.");
    ImGui::SliderFloat("Sky brightness", &tod.skyBrightness, 0.1f, 4.0f, "%.2f");
    ImGui::SetItemTooltip("Brightens or darkens the whole sky AND the light it casts on the scene.");
    ImGui::SliderFloat("Lighting update", &tod.skyUpdateSeconds, 0.1f, 5.0f, "every %.1f s", ImGuiSliderFlags_Logarithmic);
    ImGui::SetItemTooltip("While the sky changes, the light it casts on the scene is recalculated at most this often. Raise it if a running day\ncosts frames; lower it for smoother changes in the light. The sky you SEE is always exact.");
    if (scene_->GetLastSkyUpdateMs() > 0.0f) ImGui::TextDisabled("Last update: %.1f ms of CPU time", scene_->GetLastSkyUpdateMs());
    ImGui::EndDisabled();
    tod.Clamp();
}

void EditorLayer::DrawSkyboxPicker(Skybox* skybox)
{
    namespace fs = std::filesystem;
    EnsureSkyboxScan();

    const std::string current = skybox->GetFolder();
    std::string preview = current;   // a skybox from outside the assets folders is shown by its path
    for (const SkyboxChoice& choice : skyboxFolders_) {
        if (choice.folder == current) { preview = choice.label; break; }
    }

    ImGui::TextDisabled("Cubemap folder");
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SetNextItemWidth(-(ImGui::CalcTextSize("Rescan").x + style.FramePadding.x * 2.0f + style.ItemSpacing.x));
    if (ImGui::BeginCombo("##skyCubemap", preview.c_str())) {
        for (const SkyboxChoice& choice : skyboxFolders_) {
            const std::string row = choice.label + "###" + choice.folder;
            if (ImGui::Selectable(row.c_str(), choice.folder == current) && choice.folder != current) ChangeSkybox(skybox, choice.folder);
        }
        if (skyboxFolders_.empty()) ImGui::TextDisabled("No skybox folders found.");
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", current.c_str());
    if (ImGui::BeginDragDropTarget()) {   // a skybox folder dragged here from the Assets panel
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetPathPayload)) {
            const std::string dropped = Skybox::NormalizeFolder(static_cast<const char*>(payload->Data));
            if (Skybox::IsSkyboxFolder(dropped)) ChangeSkybox(skybox, dropped);
            else ShowMessage("Not a skybox folder", SkyFaces::Hint());
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine();
    if (ImGui::Button("Rescan")) RescanSkyboxes();
    ImGui::SetItemTooltip("%s\nPut new ones anywhere under assets/ and press this.", SkyFaces::Hint().c_str());

    // With a project open, only ITS assets folder is shipped. Say so here, where the choice is made, instead of at save time.
    if (project_.IsOpen() && !IsInsideAssets(current, AssetsRootText())) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "This sky is outside the project's assets folder, so it will not be shipped with the game.");
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Copy into project")) {
            const fs::path destination = project_.AssetsRoot() / "skyboxes" / fs::path(current).filename();
            std::error_code error;
            fs::create_directories(destination, error);
            std::array<std::string, 6> faces;
            if (!Skybox::ResolveFaces(current, faces)) error = std::make_error_code(std::errc::no_such_file_or_directory);
            for (const std::string& face : faces) {   // the files keep their own names: the folder is read the same way afterwards
                if (error) break;
                fs::copy_file(face, destination / fs::path(face).filename(), fs::copy_options::overwrite_existing, error);
            }
            if (error) {
                ShowMessage("Could not copy the skybox", "Copying into " + destination.string() + " failed:\n" + error.message());
            } else {
                RescanSkyboxes();
                ChangeSkybox(skybox, Skybox::NormalizeFolder(destination.string()));
            }
        }
        ImGui::SetItemTooltip("Copies the six images into the project's assets/skyboxes folder and switches to that copy.");
    }
}

void EditorLayer::DrawSkyboxInspector(Skybox* skybox)
{
    char name[64];
    std::snprintf(name, sizeof(name), "%s", skybox->GetName().c_str());
    if (ImGui::InputText("Name", name, sizeof(name))) skybox->SetName(name);

    if (skybox->IsActive()) {
        ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "Active sky");
        ImGui::SetItemTooltip("This sky is drawn and lights the scene. To switch, use another sky's \"Use this sky\" (right-click it in the Hierarchy).");
    } else if (ImGui::Button("Use this sky")) {
        pendingActiveSkyId_ = skybox->GetId();
    }

    ImGui::SeparatorText("Source");
    static const char* const kModes[] = { "Cubemap (six images)", "Procedural (follows the time of day)" };
    int mode = static_cast<int>(skybox->GetMode());
    if (ImGui::Combo("Sky", &mode, kModes, IM_ARRAYSIZE(kModes))) SetSkyMode(skybox, static_cast<SkyMode>(mode));
    ImGui::SetItemTooltip("Cubemap: a fixed picture. Procedural: computed from the time of day (Time of Day tab): a gradient, sunrise and sunset glow, the sun, the moon and stars.");
    if (skybox->IsProcedural()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Drawn from the time of day: set the clock, the sun's path and how it looks in the Time of Day tab. Its sun is where the Sun light is, so it never turns with a rotation.");
        ImGui::PopTextWrapPos();
    } else {
        DrawSkyboxPicker(skybox);
        if (!skybox->GetFaceScheme().empty()) ImGui::TextDisabled("Face names: %s", skybox->GetFaceScheme().c_str());
    }

    ImGui::SeparatorText("Look");
    float intensity = skybox->GetIntensity();
    if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 3.0f)) skybox->SetIntensity(intensity);
    ImGui::SetItemTooltip("Brightness of the sky. Dimming it dims the light it casts on the scene as well.");

    float yaw = skybox->GetYawDegrees();
    ImGui::BeginDisabled(skybox->IsProcedural());   // a procedural sky lives in world space
    if (ImGui::SliderFloat("Rotation", &yaw, 0.0f, 360.0f, "%.0f deg")) skybox->SetYawDegrees(yaw);
    ImGui::SetItemTooltip("Turns the sky around the vertical axis: the background, its light and its reflections together.\nThe sun (Lighting tab) does not turn with it: aim it separately.");
    ImGui::EndDisabled();

    const XMFLOAT3 tint = skybox->GetTint();
    float tintValues[3] = { tint.x, tint.y, tint.z };
    if (ImGui::ColorEdit3("Tint", tintValues)) skybox->SetTint({ tintValues[0], tintValues[1], tintValues[2] });
    ImGui::SetItemTooltip("Multiplies the sky's colours, and the light it gives. White leaves it as it is.");
    if (tint.x != 1.0f || tint.y != 1.0f || tint.z != 1.0f) {
        if (ImGui::SmallButton("Reset tint")) skybox->SetTint({ 1.0f, 1.0f, 1.0f });
    }
}
