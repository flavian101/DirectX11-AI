#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "Direct3D11Renderer.h"
#include "Scene.h"

class Window {
public:
    Window();
    ~Window();
    LRESULT MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void Run();
    bool InitD3D(HWND hwnd);
private:
    HWND _hwnd = nullptr;
    HINSTANCE m_Hinstance = nullptr;
    Direct3D11Renderer* m_renderer = nullptr;
    Scene* m_scene = nullptr;

    // Input state collected from Windows messages
    InputState inputState_;
    bool cursorHidden_ = false;
    int lastMouseX_ = 0;
    int lastMouseY_ = 0;
    bool hasLastMousePosition_ = false;
    bool imguiInitialized_ = false;
};

static Window* windowHandle = 0;
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
