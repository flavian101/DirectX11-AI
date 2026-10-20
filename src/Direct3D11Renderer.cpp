#include "Direct3D11Renderer.h"
#include <iostream>
#include <comdef.h>

Direct3D11Renderer::Direct3D11Renderer(HWND hwnd) : _hwnd(hwnd) {
    // TODO: add error handling for device creation
}

Direct3D11Renderer::~Direct3D11Renderer() {
    Release();
}

bool Direct3D11Renderer::Init() {
    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount = 1;
    desc.BufferDesc.Width = 800;
    desc.BufferDesc.Height = 600;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = _hwnd;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    desc.Flags = 0;

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        &featureLevel,
        1,
        D3D11_SDK_VERSION,
        &desc,
        &m_swapChain,
        &m_device,
        NULL,
        &m_context);

    if (FAILED(hr)) {
        char buf[128];
        sprintf_s(buf, sizeof(buf), "HRESULT: 0x%08X\n", hr);
        OutputDebugStringA(buf);
        OutputDebugStringA("Failed to initialize Device and SwapChain");
        return false;
    }

    ID3D11Texture2D* backBuffer = nullptr;
    hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) return false;
    hr = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTargetView);
    backBuffer->Release();
    if (FAILED(hr)) return false;

    m_context->OMSetRenderTargets(1, &m_renderTargetView, nullptr);

    return true;
}

void Direct3D11Renderer::Render() const {
    float clearColor[] = {1.0f, 0.0f, 0.0f, 1.0f};
    m_context->ClearRenderTargetView(m_renderTargetView, clearColor);
    m_swapChain->Present(0, 0);
}

void Direct3D11Renderer::Release() const {
    if (m_renderTargetView) m_renderTargetView->Release();
    if (m_swapChain) m_swapChain->Release();
    if (m_context) m_context->Release();
    if (m_device) m_device->Release();
}
