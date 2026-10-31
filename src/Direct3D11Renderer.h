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
    void Render(Scene& scene, float dt);
    void Release();
    ID3D11Device* GetDevice() const { return m_device; }

private:
    HWND _hwnd = nullptr;
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    IDXGISwapChain* m_swapChain = nullptr;
    ID3D11RenderTargetView* m_renderTargetView = nullptr;
    ID3D11Texture2D* m_depthBuffer = nullptr;
    ID3D11DepthStencilView* m_depthView = nullptr;
    ID3D11DepthStencilState* m_depthState = nullptr;
};
