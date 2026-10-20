#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "Direct3D11Renderer.h"

class Window {
public:
    Window(HINSTANCE hInstance, int nCmdShow);
    ~Window();
    LRESULT MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void Run();
    bool InitD3D(HWND hwnd);
private:
    HWND _hwnd;
    Direct3D11Renderer* m_renderer;
};

static Window* windowHandle = 0;
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
