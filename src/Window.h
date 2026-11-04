#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "Direct3D11Renderer.h"
#include "Layer.h"
#include "Scene.h"
#include <memory>

class Window {
public:
    explicit Window(int width = 800, int height = 600, const wchar_t* title = L"DirectX 11 Demo");
    ~Window();
    LRESULT MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void Run();
    bool InitD3D(HWND hwnd);

    // The Window creates the OS window and D3D11. It does NOT decide what is in the world
    // (whoever builds a Scene hands it over here) and it does NOT know about any editor: an
    // optional Layer (see Layer.h) is plugged in by the host program.
    bool IsInitialized() const { return m_renderer != nullptr; }
    void SetLayer(std::unique_ptr<Layer> layer);
    bool HasLayer() const { return layer_ != nullptr; }
    ID3D11Device* GetDevice() const { return m_renderer ? m_renderer->GetDevice() : nullptr; }
    void SetScene(std::unique_ptr<Scene> scene);
private:
    void OnResize(UINT width, UINT height);

    HWND _hwnd = nullptr;
    HINSTANCE m_Hinstance = nullptr;
    Direct3D11Renderer* m_renderer = nullptr;
    std::unique_ptr<Scene> m_scene;
    std::unique_ptr<Layer> layer_;   // optional: the editor, or nothing in a shipped game

    // Input state collected from Windows messages
    InputState inputState_;
    bool cursorHidden_ = false;
    bool hasLastMousePosition_ = false;
    bool minimized_ = false;   // client area is 0x0: skip rendering
    bool resizing_ = false;    // user is dragging the window border
};

static Window* windowHandle = 0;
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
