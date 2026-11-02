#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "Logger.h"
#include "Scene.h"

class Direct3D11Renderer {
public:
    explicit Direct3D11Renderer(HWND hwnd);
    ~Direct3D11Renderer();

    bool Init();
    // Draws one frame. const Scene&: the renderer READS the world, it never advances it.
    void Render(const Scene& scene);
    void Release();

    // Call when the window's client area changes size. Rebuilds everything that is
    // sized like the back buffer (render target + depth buffer). Returns false on failure.
    bool Resize(UINT width, UINT height);
    UINT GetWidth() const { return m_width; }
    UINT GetHeight() const { return m_height; }

    ID3D11Device* GetDevice() const { return m_device; }
    ID3D11DeviceContext* GetContext() const { return m_context; }

private:
    bool CreateSizeDependentResources(UINT width, UINT height);
    void ReleaseSizeDependentResources();
    void BindBackBuffer();   // render target + depth + viewport for the current size

    UINT m_width = 0;
    UINT m_height = 0;
    HWND _hwnd = nullptr;
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    IDXGISwapChain* m_swapChain = nullptr;
    ID3D11RenderTargetView* m_renderTargetView = nullptr;
    ID3D11Texture2D* m_depthBuffer = nullptr;
    ID3D11DepthStencilView* m_depthView = nullptr;
    ID3D11DepthStencilState* m_depthState = nullptr;
    ID3D11SamplerState* m_linearSampler = nullptr;   // bound to PS slot s0 for every material
};
