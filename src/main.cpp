#define UNICODE
#include <windows.h>
#include <memory>
#include "DemoScene.h"
#include "Logger.h"
#include "Window.h"

int main()
{
    Window window;                       // OS window + Direct3D 11 (no editor: see editor/main_editor.cpp)
    if (!window.IsInitialized()) return 1;

    auto scene = std::make_unique<Scene>();
    if (!BuildDemoScene(*scene, window.GetDevice())) {
        LOG_ERROR("Failed to build the demo scene");
        return 1;
    }
    window.SetScene(std::move(scene));
    window.Run();
    return 0;
}
