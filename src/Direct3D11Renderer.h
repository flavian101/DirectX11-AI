#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

class Direct3D11Renderer {
public:
    Direct3D11Renderer(HWND hwnd);
    ~Direct3D11Renderer();

    bool Init();
    void Render() const;
    void Release() const;

private:
    HWND _hwnd;
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    IDXGISwapChain* m_swapChain;
    ID3D11RenderTargetView* m_renderTargetView;
};
