#include "Window.h"
#include <chrono>
#include "Logger.h"
#include "Cube.h"
#include "Light.h"

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
    if (!m_scene->Initialize(m_renderer->GetDevice())) {
        LOG_ERROR("Failed to initialize scene lights");
        delete m_scene;
        m_scene = nullptr;
        return;
    }
    m_scene->AddObject(new Cube(m_renderer->GetDevice()));

    // Add test point lights to the scene
    LightSystem& lights = m_scene->GetLightSystem();

    // Adjust directional light
    DirectionalLight& dirLight = lights.Directional();
    dirLight.direction = { 0.3f, -0.8f, 0.5f };
    dirLight.color = { 1.0f, 0.95f, 0.85f };
    dirLight.intensity = 0.8f;

    // Set ambient lighting
    lights.SetAmbient({ 0.2f, 0.2f, 0.25f });

    // Add point lights around the cube
    PointLight light1;
    light1.position = { 3.0f, 2.0f, 3.0f };
    light1.color = { 1.0f, 0.6f, 0.3f };
    light1.intensity = 1.5f;
    light1.range = 8.0f;
    lights.AddPointLight(light1);

    PointLight light2;
    light2.position = { -3.0f, 1.0f, -3.0f };
    light2.color = { 0.3f, 0.6f, 1.0f };
    light2.intensity = 1.2f;
    light2.range = 7.0f;
    lights.AddPointLight(light2);

    PointLight light3;
    light3.position = { 0.0f, 3.0f, -4.0f };
    light3.color = { 0.8f, 1.0f, 0.4f };
    light3.intensity = 1.0f;
    light3.range = 6.0f;
    lights.AddPointLight(light3);
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
