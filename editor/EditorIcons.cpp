#include "EditorIcons.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265f;

// Coordinates inside an icon run from -1 to +1 on both axes (y points down, like the screen).
struct Frame {
    ImDrawList* list;
    ImVec2 c;      // centre in screen pixels
    float u;       // pixels per unit: half the icon size
    ImU32 col;
    float t;       // line thickness

    ImVec2 P(float x, float y) const { return ImVec2(c.x + x * u, c.y + y * u); }
    void Line(float x1, float y1, float x2, float y2) const { list->AddLine(P(x1, y1), P(x2, y2), col, t); }
    void Tri(float x1, float y1, float x2, float y2, float x3, float y3) const { list->AddTriangleFilled(P(x1, y1), P(x2, y2), P(x3, y3), col); }
    void Poly(const ImVec2* points, int count, bool closed) const { list->AddPolyline(points, count, col, closed ? ImDrawFlags_Closed : ImDrawFlags_None, t); }
};

void DrawMove(const Frame& f)
{
    f.Line(-0.75f, 0.0f, 0.75f, 0.0f);
    f.Line(0.0f, -0.75f, 0.0f, 0.75f);
    f.Tri(1.0f, 0.0f, 0.55f, -0.32f, 0.55f, 0.32f);
    f.Tri(-1.0f, 0.0f, -0.55f, -0.32f, -0.55f, 0.32f);
    f.Tri(0.0f, -1.0f, -0.32f, -0.55f, 0.32f, -0.55f);
    f.Tri(0.0f, 1.0f, -0.32f, 0.55f, 0.32f, 0.55f);
}

void DrawRotate(const Frame& f)
{
    // An arc most of the way round, with an arrow head where it ends.
    const float radius = 0.72f;
    const float start = 0.15f * kPi, end = 1.75f * kPi;
    f.list->PathArcTo(f.c, radius * f.u, start, end, 24);
    f.list->PathStroke(f.col, ImDrawFlags_None, f.t);
    const ImVec2 tip = f.P(radius * std::cos(end), radius * std::sin(end));
    const ImVec2 along(-std::sin(end), std::cos(end));    // the way the arc is heading
    const ImVec2 out(std::cos(end), std::sin(end));       // away from the centre
    const float head = 0.42f * f.u;
    f.list->AddTriangleFilled(ImVec2(tip.x + along.x * head, tip.y + along.y * head),
                              ImVec2(tip.x + out.x * head * 0.8f, tip.y + out.y * head * 0.8f),
                              ImVec2(tip.x - out.x * head * 0.8f, tip.y - out.y * head * 0.8f), f.col);
}

void DrawScale(const Frame& f)
{
    f.list->AddRect(f.P(-0.9f, -0.15f), f.P(0.15f, 0.9f), f.col, 0.0f, ImDrawFlags_None, f.t);
    f.Line(0.0f, 0.0f, 0.65f, -0.65f);
    f.Tri(1.0f, -1.0f, 0.45f, -1.0f, 1.0f, -0.45f);
}

void DrawBrush(const Frame& f)
{
    f.list->AddCircle(f.c, 0.9f * f.u, f.col, 0, f.t);
    f.list->AddCircle(f.c, 0.42f * f.u, f.col, 0, f.t * 0.8f);
    f.list->AddCircleFilled(f.c, 0.1f * f.u, f.col);
}

void DrawWorld(const Frame& f)
{
    f.list->AddCircle(f.c, 0.9f * f.u, f.col, 0, f.t);
    f.Line(-0.9f, 0.0f, 0.9f, 0.0f);
    ImVec2 meridian[24];
    for (int i = 0; i < 24; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / 24.0f;
        meridian[i] = f.P(0.42f * std::cos(a), 0.9f * std::sin(a));
    }
    f.Poly(meridian, 24, true);
}

void DrawLocal(const Frame& f)
{
    // Two axes from a corner: the object's own X and Y.
    f.Line(-0.65f, 0.65f, 0.75f, 0.65f);
    f.Line(-0.65f, 0.65f, -0.65f, -0.75f);
    f.Tri(1.0f, 0.65f, 0.55f, 0.35f, 0.55f, 0.95f);
    f.Tri(-0.65f, -1.0f, -0.95f, -0.55f, -0.35f, -0.55f);
    f.list->AddCircleFilled(f.P(-0.65f, 0.65f), 0.14f * f.u, f.col);
}

void DrawSky(const Frame& f)
{
    // A sun coming up over a horizon line: the whole sky, not a light in it (that is the Sun icon).
    f.Line(-1.0f, 0.35f, 1.0f, 0.35f);
    f.list->PathArcTo(f.P(0.0f, 0.35f), 0.5f * f.u, kPi, 2.0f * kPi, 16);
    f.list->PathStroke(f.col, ImDrawFlags_None, f.t);
    for (int i = 0; i < 5; ++i) {
        const float a = kPi + kPi * (static_cast<float>(i) + 0.5f) / 5.0f;
        f.Line(0.75f * std::cos(a), 0.35f + 0.75f * std::sin(a), 1.0f * std::cos(a), 0.35f + 1.0f * std::sin(a));
    }
}

void DrawSun(const Frame& f)
{
    f.list->AddCircleFilled(f.c, 0.42f * f.u, f.col);
    for (int i = 0; i < 8; ++i) {
        const float a = kPi * 0.25f * static_cast<float>(i);
        f.Line(0.68f * std::cos(a), 0.68f * std::sin(a), 0.98f * std::cos(a), 0.98f * std::sin(a));
    }
}

void DrawMoon(const Frame& f)
{
    // A disc with two craters.
    f.list->AddCircle(f.c, 0.85f * f.u, f.col, 0, f.t);
    f.list->AddCircleFilled(f.P(-0.28f, -0.2f), 0.17f * f.u, f.col);
    f.list->AddCircleFilled(f.P(0.3f, 0.28f), 0.11f * f.u, f.col);
}

void DrawBulb(const Frame& f)
{
    f.list->AddCircle(f.P(0.0f, -0.25f), 0.58f * f.u, f.col, 0, f.t);
    f.Line(-0.3f, 0.5f, 0.3f, 0.5f);
    f.Line(-0.22f, 0.78f, 0.22f, 0.78f);
    f.Line(-0.28f, 0.28f, -0.28f, 0.5f);
    f.Line(0.28f, 0.28f, 0.28f, 0.5f);
}

void DrawTerrain(const Frame& f)
{
    const ImVec2 ridge[] = { f.P(-1.0f, 0.8f), f.P(-0.35f, -0.55f), f.P(0.05f, 0.15f), f.P(0.42f, -0.25f), f.P(1.0f, 0.8f) };
    f.Poly(ridge, 5, true);
}

void DrawWater(const Frame& f)
{
    for (int row = 0; row < 3; ++row) {
        const float y = -0.55f + 0.55f * static_cast<float>(row);
        ImVec2 wave[9];
        for (int i = 0; i < 9; ++i) {
            const float x = -1.0f + 0.25f * static_cast<float>(i);
            wave[i] = f.P(x, y + 0.17f * std::sin(x * kPi * 1.5f + static_cast<float>(row)));
        }
        f.Poly(wave, 9, false);
    }
}

void DrawCubeShape(const Frame& f, bool withDiagonal)
{
    // A box seen slightly from above: a front square, a back square shifted up and right, and the four edges between.
    const float fx0 = -0.85f, fy0 = -0.15f, fx1 = 0.2f, fy1 = 0.9f;
    const float dx = 0.65f, dy = -0.65f;
    f.list->AddRect(f.P(fx0, fy0), f.P(fx1, fy1), f.col, 0.0f, ImDrawFlags_None, f.t);
    f.Line(fx0, fy0, fx0 + dx, fy0 + dy);
    f.Line(fx1, fy0, fx1 + dx, fy0 + dy);
    f.Line(fx1, fy1, fx1 + dx, fy1 + dy);
    f.Line(fx0 + dx, fy0 + dy, fx1 + dx, fy0 + dy);
    f.Line(fx1 + dx, fy0 + dy, fx1 + dx, fy1 + dy);
    if (withDiagonal) f.Line(fx0, fy1, fx1, fy0);   // a hint of triangles: a mesh, not a primitive
}

void DrawScatter(const Frame& f)
{
    f.Tri(0.0f, -1.0f, -0.62f, -0.1f, 0.62f, -0.1f);
    f.Tri(0.0f, -0.45f, -0.85f, 0.55f, 0.85f, 0.55f);
    f.Line(0.0f, 0.55f, 0.0f, 1.0f);
}

void DrawFolder(const Frame& f)
{
    f.list->AddRect(f.P(-1.0f, -0.5f), f.P(1.0f, 0.85f), f.col, 0.12f * f.u, ImDrawFlags_None, f.t);
    f.list->AddRectFilled(f.P(-1.0f, -0.85f), f.P(-0.15f, -0.5f), f.col, 0.1f * f.u);
}

void DrawImage(const Frame& f)
{
    f.list->AddRect(f.P(-1.0f, -0.8f), f.P(1.0f, 0.8f), f.col, 0.1f * f.u, ImDrawFlags_None, f.t);
    const ImVec2 hills[] = { f.P(-0.8f, 0.6f), f.P(-0.2f, -0.1f), f.P(0.2f, 0.35f), f.P(0.45f, 0.05f), f.P(0.8f, 0.6f) };
    f.Poly(hills, 5, false);
    f.list->AddCircleFilled(f.P(0.5f, -0.4f), 0.16f * f.u, f.col);
}

void DrawFilePage(const Frame& f, bool lines)
{
    const ImVec2 page[] = { f.P(-0.7f, -1.0f), f.P(0.25f, -1.0f), f.P(0.7f, -0.55f), f.P(0.7f, 1.0f), f.P(-0.7f, 1.0f) };
    f.Poly(page, 5, true);
    f.Line(0.25f, -1.0f, 0.25f, -0.55f);
    f.Line(0.25f, -0.55f, 0.7f, -0.55f);
    if (lines) {
        f.Line(-0.4f, 0.0f, 0.4f, 0.0f);
        f.Line(-0.4f, 0.4f, 0.4f, 0.4f);
    }
}

void DrawSearch(const Frame& f)
{
    f.list->AddCircle(f.P(-0.2f, -0.2f), 0.6f * f.u, f.col, 0, f.t);
    f.Line(0.25f, 0.25f, 0.95f, 0.95f);
}

void DrawGrid(const Frame& f)
{
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 2; ++column) {
            const float x = -0.9f + 1.0f * static_cast<float>(column), y = -0.9f + 1.0f * static_cast<float>(row);
            f.list->AddRect(f.P(x, y), f.P(x + 0.8f, y + 0.8f), f.col, 0.0f, ImDrawFlags_None, f.t);
        }
    }
}

void DrawList(const Frame& f)
{
    for (int row = 0; row < 3; ++row) {
        const float y = -0.7f + 0.7f * static_cast<float>(row);
        f.list->AddCircleFilled(f.P(-0.8f, y), 0.1f * f.u, f.col);
        f.Line(-0.4f, y, 0.95f, y);
    }
}
}  // namespace

void EditorIcons::Draw(ImDrawList* list, Icon icon, ImVec2 center, float size, ImU32 color)
{
    if (!list) return;
    Frame f{ list, center, size * 0.5f, color, (std::max)(1.2f, size * 0.085f) };
    switch (icon) {
    case Icon::Move:     DrawMove(f); break;
    case Icon::Rotate:   DrawRotate(f); break;
    case Icon::Scale:    DrawScale(f); break;
    case Icon::Brush:    DrawBrush(f); break;
    case Icon::World:    DrawWorld(f); break;
    case Icon::Local:    DrawLocal(f); break;
    case Icon::Sky:      DrawSky(f); break;
    case Icon::Sun:      DrawSun(f); break;
    case Icon::Moon:     DrawMoon(f); break;
    case Icon::Bulb:     DrawBulb(f); break;
    case Icon::Terrain:  DrawTerrain(f); break;
    case Icon::Water:    DrawWater(f); break;
    case Icon::Cube:     DrawCubeShape(f, false); break;
    case Icon::Model:    DrawCubeShape(f, true); break;
    case Icon::Scatter:  DrawScatter(f); break;
    case Icon::Folder:   DrawFolder(f); break;
    case Icon::Image:    DrawImage(f); break;
    case Icon::Scene:    DrawFilePage(f, true); break;
    case Icon::File:     DrawFilePage(f, false); break;
    case Icon::Search:   DrawSearch(f); break;
    case Icon::GridView: DrawGrid(f); break;
    case Icon::ListView: DrawList(f); break;
    }
}

bool EditorIcons::Button(const char* id, Icon icon, bool active, const char* tooltip)
{
    const float side = ImGui::GetFrameHeight();
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(side, side));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    if (tooltip && tooltip[0]) ImGui::SetItemTooltip("%s", tooltip);

    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImGuiCol background = (held || active) ? ImGuiCol_ButtonActive : (hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    list->AddRectFilled(position, ImVec2(position.x + side, position.y + side), ImGui::GetColorU32(background), ImGui::GetStyle().FrameRounding);
    const ImVec2 center(position.x + side * 0.5f, position.y + side * 0.5f);
    Draw(list, icon, center, side * 0.56f, ImGui::GetColorU32(ImGuiCol_Text));
    return clicked;
}
