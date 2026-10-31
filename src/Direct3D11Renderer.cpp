#include "Direct3D11Renderer.h"
#include <algorithm>

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
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        requested, 1, D3D11_SDK_VERSION, &desc, &m_swapChain, &m_device, nullptr, &m_context);
    if (FAILED(hr)) {
        LOG_HR(hr, "D3D11CreateDeviceAndSwapChain failed");
        Release();
        return false;
    }

    ID3D11Texture2D* backBuffer = nullptr;
    hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) {
        LOG_HR(hr, "Swap chain back buffer retrieval failed");
        Release();
        return false;
    }
    hr = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTargetView);
    backBuffer->Release();
    if (FAILED(hr)) {
        LOG_HR(hr, "Render target view creation failed");
        Release();
        return false;
    }

    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    hr = m_device->CreateTexture2D(&depthDesc, nullptr, &m_depthBuffer);
    if (SUCCEEDED(hr)) hr = m_device->CreateDepthStencilView(m_depthBuffer, nullptr, &m_depthView);
    if (FAILED(hr)) {
        LOG_HR(hr, "Depth buffer creation failed");
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

    m_context->OMSetRenderTargets(1, &m_renderTargetView, m_depthView);
    m_context->OMSetDepthStencilState(m_depthState, 0);
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);

    LOG_INFO("Direct3D 11 device, swap chain, render target, and depth buffer initialized");
    return true;
}

void Direct3D11Renderer::Render(Scene& scene, float dt)
{
    if (!m_context || !m_renderTargetView || !m_swapChain) return;
    const float clearColor[] = { 0.1f, 0.1f, 0.2f, 1.0f };
    m_context->ClearRenderTargetView(m_renderTargetView, clearColor);
    m_context->ClearDepthStencilView(m_depthView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    scene.Update(dt);
    scene.Render(m_context);
    const HRESULT hr = m_swapChain->Present(0, 0);
    if (FAILED(hr)) LOG_HR(hr, "Swap chain Present failed");
}

void Direct3D11Renderer::Release()
{
    if (m_depthState) { m_depthState->Release(); m_depthState = nullptr; }
    if (m_depthView) { m_depthView->Release(); m_depthView = nullptr; }
    if (m_depthBuffer) { m_depthBuffer->Release(); m_depthBuffer = nullptr; }
    if (m_renderTargetView) { m_renderTargetView->Release(); m_renderTargetView = nullptr; }
    if (m_swapChain) { m_swapChain->Release(); m_swapChain = nullptr; }
    if (m_context) { m_context->Release(); m_context = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }
}
