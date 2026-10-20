#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

class D3D11Window {
public:
    D3D11Window(HINSTANCE hInstance, int nCmdShow);
    ~D3D11Window();
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

static D3D11Window* windowHandle = 0;
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
