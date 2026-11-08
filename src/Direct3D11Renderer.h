#pragma once
#include "WaterPlan.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "Logger.h"
#include "DepthTarget.h"
#include "PostProcess.h"
#include "RenderTarget.h"
#include "Scene.h"

class Layer;

class Direct3D11Renderer {
public:
    explicit Direct3D11Renderer(HWND hwnd);
    ~Direct3D11Renderer();

    bool Init();
    // Draws one frame. const Scene&: the renderer READS the world, it never advances it.
    // `layer` (optional) gets to draw on top of the finished image - see Layer.h.
    void Render(const Scene& scene, Layer* layer = nullptr);
    void Release();

    // Call when the window's client area changes size. Rebuilds everything that is
    // sized like the back buffer (render target + depth buffer). Returns false on failure.
    bool Resize(UINT width, UINT height);
    // The WINDOW's size (the back buffer).
    UINT GetWidth() const { return m_width; }
    UINT GetHeight() const { return m_height; }

    // The size of the 3D VIEW. In a shipped game this is the window. In the editor the world is
    // drawn into a texture the size of the Viewport panel instead, so the two differ.
    UINT GetViewWidth() const { return m_viewWidth; }
    UINT GetViewHeight() const { return m_viewHeight; }
    bool SetViewTargetSize(UINT width, UINT height);   // draw the view into a texture of this size
    void UseWindowAsViewTarget();                      // back to the default: draw into the window
    bool IsViewOffscreen() const { return m_viewOffscreen; }
    // The finished (tone-mapped) view, or null while drawing straight to the window.
    ID3D11ShaderResourceView* GetViewTexture() const { return m_viewColor.IsValid() ? m_viewColor.GetShaderView() : nullptr; }

    ID3D11Device* GetDevice() const { return m_device; }
    ID3D11DeviceContext* GetContext() const { return m_context; }

private:
    bool CreateBackBufferView(UINT width, UINT height);
    bool CreateViewResources(UINT width, UINT height);   // HDR buffer + depth (+ view texture when offscreen)
    void ReleaseViewResources();
    void ReleaseBackBufferView();
    // Draws the mirror image number `slot` of the frame's water plan. Returns false if its images could not be created.
    bool RenderReflectionPass(const Scene& scene, int slot, float planeHeight, float resolutionScale, const float clearColor[4]);
    bool EnsureSceneColorCopy();   // (re)creates the refraction copy at the view's size
    void BindBackBuffer();   // window's render target + depth + viewport for the current size
    // The HDR scene buffer + depth + viewport: where the 3D world is drawn. With readOnlyDepth the
    // depth buffer can be depth-TESTED but not written, which lets shaders sample it at the same time.
    void BindSceneTarget(bool readOnlyDepth);

    UINT m_width = 0;        // window / back buffer
    UINT m_height = 0;
    UINT m_viewWidth = 0;    // the 3D view (equal to the window unless drawing offscreen)
    UINT m_viewHeight = 0;
    bool m_viewOffscreen = false;
    RenderTarget m_viewColor;    // offscreen mode: the finished image, shown by the editor's Viewport panel
    HWND _hwnd = nullptr;
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    IDXGISwapChain* m_swapChain = nullptr;
    ID3D11RenderTargetView* m_renderTargetView = nullptr;
    DepthTarget m_sceneDepth;    // the camera's depth buffer - also readable by shaders (water)
    ID3D11DepthStencilState* m_depthState = nullptr;
    ID3D11SamplerState* m_linearSampler = nullptr;   // bound to PS slot s0 for every material
    RenderTarget m_hdrScene;     // the 3D world is drawn here (16-bit float), then resolved to the window
    RenderTarget m_reflectionColor[WaterFramePlan::kMaxReflections];   // the scene as seen in the water: one per water level (created on demand)
    RenderTarget m_sceneColorCopy;    // the opaque scene, copied for the water to refract (created on demand)
    DepthTarget m_reflectionDepth[WaterFramePlan::kMaxReflections];
    ID3D11SamplerState* m_clampSampler = nullptr;   // bound to s2: linear filtering, edges clamp
    PostProcess m_post;          // full-screen passes: HDR buffer -> back buffer
};
