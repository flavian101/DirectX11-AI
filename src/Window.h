#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "Direct3D11Renderer.h"
#include "Scene.h"
#include <memory>

class ImGuiLayer;

class Window {
public:
    Window();
    ~Window();
    LRESULT MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void Run();
    bool InitD3D(HWND hwnd);

    // The Window creates the OS window, D3D11 and ImGui. It does NOT decide what is in the
    // world: whoever builds a Scene hands it over here (see main.cpp / DemoScene.cpp).
    bool IsInitialized() const { return m_renderer && imguiInitialized_; }
    ID3D11Device* GetDevice() const { return m_renderer ? m_renderer->GetDevice() : nullptr; }
    void SetScene(std::unique_ptr<Scene> scene);
private:
    void OnResize(UINT width, UINT height);

    HWND _hwnd = nullptr;
    HINSTANCE m_Hinstance = nullptr;
    Direct3D11Renderer* m_renderer = nullptr;
    std::unique_ptr<Scene> m_scene;
    std::unique_ptr<ImGuiLayer> imguiLayer_;

    // Input state collected from Windows messages
    InputState inputState_;
    bool cursorHidden_ = false;
    bool hasLastMousePosition_ = false;
    bool imguiInitialized_ = false;
    bool minimized_ = false;   // client area is 0x0: skip rendering
    bool resizing_ = false;    // user is dragging the window border
};

static Window* windowHandle = 0;
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
