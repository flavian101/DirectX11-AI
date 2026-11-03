#include "Direct3D11Renderer.h"
#include "Color.h"
#include "ShaderSlots.h"
#include <algorithm>
#include "imgui_impl_dx11.h"

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

    if (!CreateSizeDependentResources(width, height)) {
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

    m_context->OMSetDepthStencilState(m_depthState, 0);
    BindBackBuffer();

    LOG_INFO("Direct3D 11 device, swap chain, render target, and depth buffer initialized");
    return true;
}

bool Direct3D11Renderer::CreateSizeDependentResources(UINT width, UINT height)
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

    // 32-bit float depth, readable from shaders. (The old D24S8 buffer could not be read, and
    // nothing here uses stencil.)
    if (!m_sceneDepth.Create(m_device, width, height)) return false;

    // 16 bits per channel, FLOAT: values above 1.0 survive (a bright sun, a sky glint) and
    // nothing is rounded to 256 levels until the very last pass.
    if (!m_hdrScene.Create(m_device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT)) return false;

    m_width = width;
    m_height = height;
    return true;
}

void Direct3D11Renderer::ReleaseSizeDependentResources()
{
    m_hdrScene.Release();
    m_sceneDepth.Release();
    m_reflectionColor.Release();   // window-sized too; rebuilt on demand at the new size
    m_reflectionDepth.Release();
    if (m_renderTargetView) { m_renderTargetView->Release(); m_renderTargetView = nullptr; }
}

bool Direct3D11Renderer::Resize(UINT width, UINT height)
{
    if (!m_swapChain || !m_context || width == 0 || height == 0) return false;   // minimized
    if (width == m_width && height == m_height) return true;

    // ResizeBuffers fails while anything still references the old back buffer, so:
    // unbind it from the pipeline, release OUR views of it, flush the queued work, then resize.
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    ReleaseSizeDependentResources();
    m_context->Flush();

    const HRESULT hr = m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        LOG_HR(hr, "Swap chain ResizeBuffers failed");
        return false;   // Render() skips frames while there is no render target
    }
    return CreateSizeDependentResources(width, height);
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
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
}

bool Direct3D11Renderer::RenderReflectionPass(const Scene& scene, float planeHeight, float resolutionScale,
                                              const float clearColor[4])
{
    const UINT width = (std::max)(1u, static_cast<UINT>(static_cast<float>(m_width) * resolutionScale));
    const UINT height = (std::max)(1u, static_cast<UINT>(static_cast<float>(m_height) * resolutionScale));

    // Create (or re-create after a resize / quality change) the reflection image + its own depth.
    if (!m_reflectionColor.IsValid() || m_reflectionColor.GetWidth() != width || m_reflectionColor.GetHeight() != height) {
        if (!m_reflectionColor.Create(m_device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT) ||
            !m_reflectionDepth.Create(m_device, width, height)) {
            m_reflectionColor.Release();
            m_reflectionDepth.Release();
            return false;
        }
    }

    // It was bound as a shader input for the water last frame: let go before using it as output.
    ID3D11ShaderResourceView* none = nullptr;
    m_context->PSSetShaderResources(Slots::kSrvReflection, 1, &none);

    ID3D11RenderTargetView* target = m_reflectionColor.GetRenderView();
    m_context->OMSetRenderTargets(1, &target, m_reflectionDepth.GetDepthView());
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
    m_reflectionColor.Clear(m_context, clearColor);
    m_reflectionDepth.Clear(m_context);

    scene.RenderReflection(m_context, planeHeight, width, height);
    return true;
}

void Direct3D11Renderer::Render(const Scene& scene)
{
    if (!m_context || !m_renderTargetView || !m_sceneDepth.IsValid() || !m_swapChain || !m_hdrScene.IsValid()) return;
    // Lights first: both passes below read them.
    scene.GetLightSystem().Apply(m_context, scene.GetCamera().GetPosition());

    // PASS 1 - depth from the sun's point of view, into the shadow map.
    scene.RenderShadows(m_context);

    // The background colour is picked as sRGB, but the HDR buffers hold LINEAR light.
    const float clearColor[] = { SrgbToLinear(0.1f), SrgbToLinear(0.1f), SrgbToLinear(0.2f), 1.0f };

    // Resources shared by every view below (shadow map, environment map).
    scene.BeginFrame(m_context);

    // PASS 2 - the scene seen in the water: drawn from a camera mirrored below the surface.
    float planeHeight = 0.0f, resolutionScale = 1.0f;
    bool reflectionReady = false;
    if (scene.GetPlanarReflection(planeHeight, resolutionScale)) {
        reflectionReady = RenderReflectionPass(scene, planeHeight, resolutionScale, clearColor);
    } else {
        m_reflectionColor.Release();   // not needed: give the memory back
        m_reflectionDepth.Release();
    }

    // PASS 3 - the camera's view into the HDR scene buffer (NOT the window any more).
    // OPAQUE phase first: sky, terrain, cube - everything that writes depth.
    BindSceneTarget(false);
    m_hdrScene.Clear(m_context, clearColor);
    m_sceneDepth.Clear(m_context);
    scene.SetMainView(m_context, m_width, m_height, reflectionReady);
    scene.RenderPhase(m_context, ScenePhase::Opaque);

    // TRANSPARENT phase (the water). Depth is now complete, so make it read-only and hand it to
    // the shaders as a texture, together with the reflection image.
    BindSceneTarget(true);
    ID3D11ShaderResourceView* sceneDepth = m_sceneDepth.GetShaderView();
    ID3D11ShaderResourceView* reflection = reflectionReady ? m_reflectionColor.GetShaderView() : nullptr;
    m_context->PSSetShaderResources(Slots::kSrvSceneDepth, 1, &sceneDepth);
    m_context->PSSetShaderResources(Slots::kSrvReflection, 1, &reflection);
    scene.RenderPhase(m_context, ScenePhase::Transparent);
    ID3D11ShaderResourceView* none = nullptr;
    m_context->PSSetShaderResources(Slots::kSrvSceneDepth, 1, &none);
    m_context->PSSetShaderResources(Slots::kSrvReflection, 1, &none);

    // PASS 4 - turn the HDR buffer into the window's image. The UI is drawn after this,
    // straight onto the back buffer, because it is already designed for display colours.
    m_post.Resolve(m_context, m_hdrScene.GetShaderView(), m_renderTargetView, m_width, m_height,
                   scene.GetPostSettings());
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    const HRESULT hr = m_swapChain->Present(0, 0);
    if (FAILED(hr)) LOG_HR(hr, "Swap chain Present failed");
}

void Direct3D11Renderer::Release()
{
    m_post.Release();
    if (m_clampSampler) { m_clampSampler->Release(); m_clampSampler = nullptr; }
    if (m_linearSampler) { m_linearSampler->Release(); m_linearSampler = nullptr; }
    if (m_depthState) { m_depthState->Release(); m_depthState = nullptr; }
    ReleaseSizeDependentResources();
    if (m_swapChain) { m_swapChain->Release(); m_swapChain = nullptr; }
    if (m_context) { m_context->Release(); m_context = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }
}
