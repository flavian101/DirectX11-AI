#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

class Window {
public:
    Window(HINSTANCE hInstance, int nCmdShow);
    ~Window();
    LRESULT MessageHandler(HWND, UINT, WPARAM, LPARAM);
    void Run();
    bool InitD3D(HWND hwnd);
private:
    HWND _hwnd;
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    IDXGISwapChain* m_swapChain;
    ID3D11RenderTargetView* m_renderTargetView;
};

static Window* windowHandle = 0;
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
