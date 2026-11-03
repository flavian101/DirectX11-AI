#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "Logger.h"
#include "DepthTarget.h"
#include "PostProcess.h"
#include "RenderTarget.h"
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
    bool RenderReflectionPass(const Scene& scene, float planeHeight, float resolutionScale, const float clearColor[4]);
    void BindBackBuffer();   // window's render target + depth + viewport for the current size
    // The HDR scene buffer + depth + viewport: where the 3D world is drawn. With readOnlyDepth the
    // depth buffer can be depth-TESTED but not written, which lets shaders sample it at the same time.
    void BindSceneTarget(bool readOnlyDepth);

    UINT m_width = 0;
    UINT m_height = 0;
    HWND _hwnd = nullptr;
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    IDXGISwapChain* m_swapChain = nullptr;
    ID3D11RenderTargetView* m_renderTargetView = nullptr;
    DepthTarget m_sceneDepth;    // the camera's depth buffer - also readable by shaders (water)
    ID3D11DepthStencilState* m_depthState = nullptr;
    ID3D11SamplerState* m_linearSampler = nullptr;   // bound to PS slot s0 for every material
    RenderTarget m_hdrScene;     // the 3D world is drawn here (16-bit float), then resolved to the window
    RenderTarget m_reflectionColor;   // the scene as seen in the water (created on demand)
    DepthTarget m_reflectionDepth;
    ID3D11SamplerState* m_clampSampler = nullptr;   // bound to s2: linear filtering, edges clamp
    PostProcess m_post;          // full-screen passes: HDR buffer -> back buffer
};
