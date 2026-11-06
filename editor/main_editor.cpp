#define UNICODE
#include <windows.h>
#include <memory>
#include "DemoScene.h"
#include "EditorLayer.h"
#include "Logger.h"
#include "Window.h"

// The editor's entry point. Compare with src/main.cpp (the game): the ONLY difference is the line
// that plugs an EditorLayer into the Window.
int main()
{
    Window window(1600, 900, L"DirectX 11 Editor");
    if (!window.IsInitialized()) return 1;

    window.SetLayer(std::make_unique<EditorLayer>());
    if (!window.HasLayer()) return 1;

    auto scene = BuildStartScene(window.GetDevice());
    if (!scene) {
        LOG_ERROR("Failed to build the start scene");
        return 1;
    }
    window.SetScene(std::move(scene));
    window.Run();
    return 0;
}
