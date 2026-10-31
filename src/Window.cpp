#include "Window.h"
#include <chrono>
#include "Logger.h"
#include "Cube.h"

Window::Window() : m_Hinstance(GetModuleHandle(nullptr))
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = m_Hinstance;
    wc.lpszClassName = L"D3D11WindowClass";
    RegisterClassW(&wc);
    windowHandle = this;

    _hwnd = CreateWindowExW(0, wc.lpszClassName, L"DirectX 11 Demo",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600, nullptr, nullptr, m_Hinstance, nullptr);
    if (!_hwnd) {
        LOG_ERROR("Failed to create window");
        return;
    }
    ShowWindow(_hwnd, SW_SHOW);
    if (!InitD3D(_hwnd)) {
        LOG_ERROR("Failed to initialize Direct3D 11");
        return;
    }

    m_scene = new Scene();
    m_scene->AddObject(new Cube(m_renderer->GetDevice()));
}

Window::~Window()
{
    // Destroy scene GPU resources while the D3D device is still alive.
    delete m_scene;
    delete m_renderer;
    if (windowHandle == this) windowHandle = nullptr;
}

LRESULT Window::MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void Window::Run()
{
    if (!m_renderer || !m_scene) return;
    MSG msg{};
    auto lastTime = std::chrono::steady_clock::now();
    while (true) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) return;
        }
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        m_renderer->Render(*m_scene, dt);
    }
}

bool Window::InitD3D(HWND hwnd)
{
    auto* renderer = new Direct3D11Renderer(hwnd);
    if (!renderer->Init()) {
        delete renderer;
        LOG_ERROR("Direct3D11Renderer::Init failed");
        return false;
    }
    m_renderer = renderer;
    return true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return windowHandle ? windowHandle->MessageHandler(hwnd, msg, wParam, lParam)
                            : DefWindowProc(hwnd, msg, wParam, lParam);
    }
}
