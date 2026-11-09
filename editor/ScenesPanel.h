#pragma once
#include "Project.h"
#include <string>

// What the user asked for in the Scenes panel. The panel only DRAWS and reports: EditorLayer carries it out at the start
// of the next frame, like every other request (so nothing changes while a list is being drawn).
struct ScenesAction {
    enum class Kind { None, Open, SetStart, Remove, AddCurrent, NewScene };
    Kind kind = Kind::None;
    std::string scene;   // the entry (relative path) it applies to, when it applies to one
};

struct ScenesState {
    std::string currentScene;   // the open scene as a project-relative path, or "" (unsaved, or outside the project)
    bool dirty = false;         // the open scene has unsaved changes
    bool canAddCurrent = false; // a saved scene inside the project that is not on the list yet
};

class ScenesPanel {
public:
    ScenesAction Draw(bool* open, const Project& project, const ScenesState& state);
};
