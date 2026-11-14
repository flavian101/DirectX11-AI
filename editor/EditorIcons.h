#pragma once
#include "imgui.h"

// Small vector icons, drawn with ImGui's draw list. No image files and no icon font to ship: they scale with the UI,
// take any colour (so they follow the theme) and stay sharp on a high-DPI screen.
namespace EditorIcons {

enum class Icon {
    Move, Rotate, Scale, Brush, World, Local,          // viewport toolbar
    Sky, Terrain, Water, Cube, Model, Scatter,          // what a scene object is
    Sun, Moon, Bulb,                                    // lights: a directional light by day or by night, and a point light
    Folder, Image, Scene, File, Search, GridView, ListView   // assets and filtering
};

// Draws `icon` centred on `center`, fitting a square of `size` pixels.
void Draw(ImDrawList* list, Icon icon, ImVec2 center, float size, ImU32 color);

// A square, frame-height button that shows an icon. `active` keeps it lit (a tool that is on). The tooltip is the label:
// put the shortcut in it. Returns true when clicked.
bool Button(const char* id, Icon icon, bool active, const char* tooltip);

}  // namespace EditorIcons
