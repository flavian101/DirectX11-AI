#include "Direct3D11Renderer.h"
#include "Color.h"
#include "ShaderSlots.h"
#include <algorithm>
#include "Layer.h"

Direct3D11Renderer::Direct3D11Renderer(HWND hwnd) : _hwnd(hwnd) {}
Direct3D11Renderer::~Direct3D11Renderer() { Release(); }

bool Direct3D11Renderer::Init()
{
    RECT client{};
    if (!_hwnd || !GetClientRect(_hwnd, &client)) {
        LOG_ERROR("Cannot determine render window size");
        return false;
    }
    const UINT width = static_cast<UINT>(std::max<LONG>(1, client.right - client.left));
    const UINT height = static_cast<UINT>(std::max<LONG>(1, client.bottom - client.top));

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 1;
    desc.BufferDesc.Width = width;
    desc.BufferDesc.Height = height;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = _hwnd;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL requested[] = { D3D_FEATURE_LEVEL_11_0 };
    UINT deviceFlags = 0;
#if defined(_DEBUG)
    // The debug layer prints "constant buffer not bound" style mistakes to the
    // Output window. It needs the "Graphics Tools" Windows feature installed.
    deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, deviceFlags,
        requested, 1, D3D11_SDK_VERSION, &desc, &m_swapChain, &m_device, nullptr, &m_context);
    if (FAILED(hr) && (deviceFlags & D3D11_CREATE_DEVICE_DEBUG)) {
        LOG_WARNING("D3D11 debug layer unavailable (install 'Graphics Tools'); retrying without it");
        deviceFlags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, deviceFlags,
            requested, 1, D3D11_SDK_VERSION, &desc, &m_swapChain, &m_device, nullptr, &m_context);
    }
    if (FAILED(hr)) {
        LOG_HR(hr, "D3D11CreateDeviceAndSwapChain failed");
        Release();
        return false;
    }

    if (!CreateBackBufferView(width, height) || !CreateViewResources(width, height)) {
        Release();
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC depthStateDesc{};
    depthStateDesc.DepthEnable = TRUE;
    depthStateDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthStateDesc.DepthFunc = D3D11_COMPARISON_LESS;
    hr = m_device->CreateDepthStencilState(&depthStateDesc, &m_depthState);
    if (FAILED(hr)) {
        LOG_HR(hr, "Depth stencil state creation failed");
        Release();
        return false;
    }

    // Nothing in the project created a sampler before, so every Sample() call relied
    // on whatever the driver does for an unbound slot. Make it explicit.
    D3D11_SAMPLER_DESC samplerDesc{};
    // Anisotropic = trilinear (blends between mip levels) + extra samples along the
    // stretched direction, so terrain seen at a grazing angle stays sharp instead of smearing.
    // 8x is a good cost/quality point; 16 is the hardware maximum.
    samplerDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    samplerDesc.MaxAnisotropy = 8;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = m_device->CreateSamplerState(&samplerDesc, &m_linearSampler);
    if (FAILED(hr)) {
        LOG_HR(hr, "Sampler state creation failed");
        Release();
        return false;
    }
    m_context->PSSetSamplers(Slots::kSamplerLinear, 1, &m_linearSampler);

    // Screen-space lookups (the water reading the reflection image) must NOT wrap around at the
    // edges, so they get their own sampler.
    D3D11_SAMPLER_DESC clampDesc{};
    clampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    clampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    clampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    clampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    clampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    clampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = m_device->CreateSamplerState(&clampDesc, &m_clampSampler);
    if (FAILED(hr)) {
        LOG_HR(hr, "Clamp sampler creation failed");
        Release();
        return false;
    }
    m_context->PSSetSamplers(Slots::kSamplerClamp, 1, &m_clampSampler);

    if (!m_post.Initialize(m_device)) {
        Release();
        return false;
    }
    if (!m_gpuTimer.Initialize(m_device)) LOG_WARNING("GPU timestamp queries unavailable: no per-pass GPU times");

    m_context->OMSetDepthStencilState(m_depthState, 0);
    BindBackBuffer();

    LOG_INFO("Direct3D 11 device, swap chain, render target, and depth buffer initialized");
    return true;
}

bool Direct3D11Renderer::CreateBackBufferView(UINT width, UINT height)
{
    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) {
        LOG_HR(hr, "Swap chain back buffer retrieval failed");
        return false;
    }
    hr = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTargetView);
    backBuffer->Release();
    if (FAILED(hr)) {
        LOG_HR(hr, "Render target view creation failed");
        return false;
    }
    m_width = width;
    m_height = height;
    return true;
}

bool Direct3D11Renderer::CreateViewResources(UINT width, UINT height)
{
    // 32-bit float depth, readable from shaders. (The old D24S8 buffer could not be read, and
    // nothing here uses stencil.)
    if (!m_sceneDepth.Create(m_device, width, height)) return false;

    // 16 bits per channel, FLOAT: values above 1.0 survive (a bright sun, a sky glint) and
    // nothing is rounded to 256 levels until the very last pass.
    if (!m_hdrScene.Create(m_device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT)) return false;

    // Editor only: the resolve pass writes the finished picture here, and ImGui shows it as an image.
    if (m_viewOffscreen && !m_viewColor.Create(m_device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM)) return false;

    m_viewWidth = width;
    m_viewHeight = height;
    return true;
}

void Direct3D11Renderer::ReleaseViewResources()
{
    m_viewColor.Release();
    m_hdrScene.Release();
    m_sceneDepth.Release();
    m_sceneColorCopy.Release();    // view-sized too
    for (int i = 0; i < WaterFramePlan::kMaxReflections; ++i) {   // view-sized too; rebuilt on demand at the new size
        m_reflectionColor[i].Release();
        m_reflectionDepth[i].Release();
    }
}

void Direct3D11Renderer::ReleaseBackBufferView()
{
    if (m_renderTargetView) { m_renderTargetView->Release(); m_renderTargetView = nullptr; }
}

bool Direct3D11Renderer::Resize(UINT width, UINT height)
{
    if (!m_swapChain || !m_context || width == 0 || height == 0) return false;   // minimized
    if (width == m_width && height == m_height) return true;

    // ResizeBuffers fails while anything still references the old back buffer, so:
    // unbind it from the pipeline, release OUR views of it, flush the queued work, then resize.
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    ReleaseBackBufferView();
    // Drawing straight to the window: the 3D view is window-sized, so it follows. Drawing offscreen:
    // the view has its own size (the Viewport panel's) and a window resize does not touch it.
    if (!m_viewOffscreen) ReleaseViewResources();
    m_context->Flush();

    const HRESULT hr = m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        LOG_HR(hr, "Swap chain ResizeBuffers failed");
        return false;   // Render() skips frames while there is no render target
    }
    if (!CreateBackBufferView(width, height)) return false;
    return m_viewOffscreen ? true : CreateViewResources(width, height);
}

bool Direct3D11Renderer::SetViewTargetSize(UINT width, UINT height)
{
    if (!m_device || !m_context || width == 0 || height == 0) return false;
    if (m_viewOffscreen && width == m_viewWidth && height == m_viewHeight) return true;

    // Let go of every binding that could still point at the textures we are about to destroy.
    ID3D11ShaderResourceView* none = nullptr;
    m_context->PSSetShaderResources(0, 1, &none);
    m_context->PSSetShaderResources(Slots::kSrvSceneDepth, 1, &none);
    m_context->PSSetShaderResources(Slots::kSrvReflection, 1, &none);
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    ReleaseViewResources();
    m_viewOffscreen = true;
    return CreateViewResources(width, height);
}

void Direct3D11Renderer::UseWindowAsViewTarget()
{
    if (!m_viewOffscreen || !m_context) return;
    ID3D11ShaderResourceView* none = nullptr;
    m_context->PSSetShaderResources(0, 1, &none);
    m_context->PSSetShaderResources(Slots::kSrvSceneDepth, 1, &none);
    m_context->PSSetShaderResources(Slots::kSrvReflection, 1, &none);
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    ReleaseViewResources();
    m_viewOffscreen = false;
    CreateViewResources(m_width, m_height);
}

void Direct3D11Renderer::BindBackBuffer()
{
    // Done every frame (not once in Init): the size can change, and later passes that
    // render into other textures (shadows, planar reflection) must restore this state.
    m_context->OMSetRenderTargets(1, &m_renderTargetView, nullptr);
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
}

void Direct3D11Renderer::BindSceneTarget(bool readOnlyDepth)
{
    // The resolve pass reads the HDR texture as an input. Make sure it is not still bound as
    // one before binding it as an output (see lesson 1: never both at once).
    ID3D11ShaderResourceView* none = nullptr;
    m_context->PSSetShaderResources(0, 1, &none);
    // Same rule for the depth buffer: if it is about to be WRITTEN, it must not be a shader input.
    if (!readOnlyDepth) m_context->PSSetShaderResources(Slots::kSrvSceneDepth, 1, &none);

    ID3D11RenderTargetView* hdr = m_hdrScene.GetRenderView();
    m_context->OMSetRenderTargets(1, &hdr, readOnlyDepth ? m_sceneDepth.GetReadOnlyDepthView()
                                                         : m_sceneDepth.GetDepthView());
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(m_viewWidth);
    viewport.Height = static_cast<float>(m_viewHeight);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
}

bool Direct3D11Renderer::EnsureSceneColorCopy()
{
    if (m_sceneColorCopy.IsValid() && m_sceneColorCopy.GetWidth() == m_viewWidth && m_sceneColorCopy.GetHeight() == m_viewHeight) return true;
    if (m_sceneColorCopy.Create(m_device, m_viewWidth, m_viewHeight, m_hdrScene.GetFormat())) return true;
    m_sceneColorCopy.Release();
    return false;
}

bool Direct3D11Renderer::RenderReflectionPass(const Scene& scene, int slot, float planeHeight, float resolutionScale,
                                              const float clearColor[4])
{
    RenderTarget& color = m_reflectionColor[slot];
    DepthTarget& depth = m_reflectionDepth[slot];
    const UINT width = (std::max)(1u, static_cast<UINT>(static_cast<float>(m_viewWidth) * resolutionScale));
    const UINT height = (std::max)(1u, static_cast<UINT>(static_cast<float>(m_viewHeight) * resolutionScale));

    // Create (or re-create after a resize / quality change) the reflection image + its own depth.
    if (!color.IsValid() || color.GetWidth() != width || color.GetHeight() != height) {
        if (!color.Create(m_device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT) || !depth.Create(m_device, width, height)) {
            color.Release();
            depth.Release();
            return false;
        }
    }

    // It was bound as a shader input for the water last frame: let go before using it as output.
    ID3D11ShaderResourceView* none = nullptr;
    m_context->PSSetShaderResources(Slots::kSrvReflection, 1, &none);

    ID3D11RenderTargetView* target = color.GetRenderView();
    m_context->OMSetRenderTargets(1, &target, depth.GetDepthView());
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
    color.Clear(m_context, clearColor);
    depth.Clear(m_context);

    scene.RenderReflection(m_context, planeHeight, width, height);
    return true;
}

void Direct3D11Renderer::Render(const Scene& scene, Layer* layer)
{
    if (!m_context || !m_renderTargetView || !m_sceneDepth.IsValid() || !m_swapChain || !m_hdrScene.IsValid()) return;
    // Lights first: both passes below read them.
    scene.GetLightSystem().Apply(m_context, scene.GetCamera().GetPosition());

    m_gpuTimer.BeginFrame(m_context);
    m_gpuTimer.Mark(m_context, GpuPass::Shadows);

    // PASS 1 - depth from the sun's point of view, into the shadow map.
    scene.RenderShadows(m_context);

    // The background colour is picked as sRGB, but the HDR buffers hold LINEAR light.
    const float clearColor[] = { SrgbToLinear(0.1f), SrgbToLinear(0.1f), SrgbToLinear(0.2f), 1.0f };

    // Bake the sky into image-based lighting if that has not happened yet (a one-off, a few milliseconds).
    scene.PrepareEnvironment(m_context);

    // Resources shared by every view below (shadow map, environment map, baked sky lighting).
    scene.BeginFrame(m_context);

    // What every water body on screen needs this frame (mirror images, a refraction copy). Decided once, here.
    WaterFramePlan waterPlan;
    scene.PlanWater(waterPlan);

    m_gpuTimer.Mark(m_context, GpuPass::Reflections);
    // PASS 2 - the scene seen in the water: drawn from a camera mirrored below each water LEVEL that wants one.
    // Bodies at the same level share an image; a budget caps how many are drawn (see Scene::PlanWater).
    bool reflectionReady = false;
    for (int i = 0; i < WaterFramePlan::kMaxReflections; ++i) {
        if (i < waterPlan.reflectionCount &&
            RenderReflectionPass(scene, i, waterPlan.reflections[i].planeHeight, waterPlan.reflections[i].resolutionScale, clearColor)) {
            waterPlan.reflections[i].image = m_reflectionColor[i].GetShaderView();
            reflectionReady = true;
        } else {
            m_reflectionColor[i].Release();   // not needed: give the memory back
            m_reflectionDepth[i].Release();
        }
    }

    // Refraction needs a copy of the opaque picture to read while the water is drawn over that same picture.
    // Only made when some water on screen asks for it (refraction > 0): otherwise it costs nothing.
    bool refractionReady = false;
    if (waterPlan.refraction) {
        refractionReady = EnsureSceneColorCopy();
    } else {
        m_sceneColorCopy.Release();   // not needed: give the memory back
    }

    // PASS 3 - the camera's view into the HDR scene buffer (NOT the window any more).
    // OPAQUE phase first: sky, terrain, cube - everything that writes depth.
    m_gpuTimer.Mark(m_context, GpuPass::Opaque);
    BindSceneTarget(false);
    m_hdrScene.Clear(m_context, clearColor);
    m_sceneDepth.Clear(m_context);
    scene.SetMainView(m_context, m_viewWidth, m_viewHeight, reflectionReady, refractionReady);
    RenderView mainView = scene.GetMainView();   // the camera's frustum, for culling
    mainView.waterPlan = &waterPlan;             // each water body picks its own mirror image from this
    scene.RenderPhase(m_context, ScenePhase::Opaque, mainView);
    scene.RenderHullMasks(m_context, mainView);   // depth only: the water is not drawn inside a floating boat's hull
    if (refractionReady) m_context->CopyResource(m_sceneColorCopy.GetTexture(), m_hdrScene.GetTexture());

    // TRANSPARENT phase (the water). Depth is now complete, so make it read-only and hand it to
    // the shaders as a texture, together with the reflection image.
    m_gpuTimer.Mark(m_context, GpuPass::Water);
    BindSceneTarget(true);
    ID3D11ShaderResourceView* sceneDepth = m_sceneDepth.GetShaderView();
    m_context->PSSetShaderResources(Slots::kSrvSceneDepth, 1, &sceneDepth);
    // (The mirror image, t3, is bound by each water body just before it draws: they do not all use the same one.)
    ID3D11ShaderResourceView* sceneColor = refractionReady ? m_sceneColorCopy.GetShaderView() : nullptr;
    m_context->PSSetShaderResources(Slots::kSrvSceneColor, 1, &sceneColor);
    scene.RenderPhase(m_context, ScenePhase::Transparent, mainView);
    ID3D11ShaderResourceView* none = nullptr;
    m_context->PSSetShaderResources(Slots::kSrvSceneDepth, 1, &none);
    m_context->PSSetShaderResources(Slots::kSrvReflection, 1, &none);
    m_context->PSSetShaderResources(Slots::kSrvSceneColor, 1, &none);

    // PASS 4 - turn the HDR buffer into the finished image. Normally that goes straight to the
    // window. In the editor it goes into a texture (shown by the Viewport panel) and the window
    // itself only receives the UI.
    m_gpuTimer.Mark(m_context, GpuPass::PostProcess);
    if (m_viewOffscreen && m_viewColor.IsValid()) {
        m_post.Resolve(m_context, m_hdrScene.GetShaderView(), m_viewColor.GetRenderView(),
                       m_viewWidth, m_viewHeight, scene.GetPostSettings());
        BindBackBuffer();
        const float shellColor[4] = { 0.08f, 0.08f, 0.09f, 1.0f };
        m_context->ClearRenderTargetView(m_renderTargetView, shellColor);
    } else {
        m_post.Resolve(m_context, m_hdrScene.GetShaderView(), m_renderTargetView, m_width, m_height,
                       scene.GetPostSettings());
    }
    m_gpuTimer.EndFrame(m_context);   // the UI below is not counted: this measures the 3D view
    // The UI (if there is one) draws after this, straight onto the back buffer, because it is
    // already designed for display colours. The renderer does not know what the layer draws.
    if (layer) layer->OnRenderOverlay(m_context);
    const HRESULT hr = m_swapChain->Present(0, 0);
    if (FAILED(hr)) LOG_HR(hr, "Swap chain Present failed");
}

void Direct3D11Renderer::Release()
{
    m_gpuTimer.Release();
    m_post.Release();
    if (m_clampSampler) { m_clampSampler->Release(); m_clampSampler = nullptr; }
    if (m_linearSampler) { m_linearSampler->Release(); m_linearSampler = nullptr; }
    if (m_depthState) { m_depthState->Release(); m_depthState = nullptr; }
    ReleaseViewResources();
    ReleaseBackBufferView();
    if (m_swapChain) { m_swapChain->Release(); m_swapChain = nullptr; }
    if (m_context) { m_context->Release(); m_context = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }
}
