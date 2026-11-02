#include "Window.h"
#include <chrono>
#include <windowsx.h>
#include "Logger.h"
#include "ImGuiLayer.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

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

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    if (!ImGui_ImplWin32_Init(_hwnd) ||
        !ImGui_ImplDX11_Init(m_renderer->GetDevice(), m_renderer->GetContext())) {
        LOG_ERROR("Failed to initialize Dear ImGui");
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return;
    }
    imguiInitialized_ = true;
}

void Window::SetScene(std::unique_ptr<Scene> scene)
{
    imguiLayer_.reset();   // it points into the old scene
    m_scene = std::move(scene);
    if (m_scene) imguiLayer_ = std::make_unique<ImGuiLayer>(m_scene.get());
}

Window::~Window()
{
    imguiLayer_.reset();
    if (imguiInitialized_) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        imguiInitialized_ = false;
    }
    // Destroy scene GPU resources while the D3D device is still alive.
    m_scene.reset();
    delete m_renderer;
    if (windowHandle == this) windowHandle = nullptr;
}

LRESULT Window::MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (imguiInitialized_ && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
        return 0;
    }

    switch (msg) {
    case WM_KEYDOWN:
        if (wParam < 256) {
            inputState_.keys[wParam] = true;
     
        }
        break;
    case WM_KEYUP:
        if (wParam < 256) {
            inputState_.keys[wParam] = false;
            if (wParam == VK_CONTROL) {
               
            }
        }
        break;
    case WM_MBUTTONDOWN:
    {
        SetCapture(hwnd);
        ShowCursor(FALSE);

        inputState_.middleMouseButtonDown = true;

        lastMouseX_ = GET_X_LPARAM(lParam);
        lastMouseY_ = GET_Y_LPARAM(lParam);
        hasLastMousePosition_ = true;

        return 0;
    }

    case WM_MBUTTONUP:
    {
        ReleaseCapture();
        ShowCursor(TRUE);

        inputState_.middleMouseButtonDown = false;
        inputState_.mouseDeltaX = 0;
        inputState_.mouseDeltaY = 0;
        hasLastMousePosition_ = false;

        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (inputState_.middleMouseButtonDown)
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            if (hasLastMousePosition_)
            {
                inputState_.mouseDeltaX += x - lastMouseX_;
                inputState_.mouseDeltaY += y - lastMouseY_;
            }

            lastMouseX_ = x;
            lastMouseY_ = y;
        }

        return 0;
    
    }
    }
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
            if (msg.message == WM_QUIT) {
                if (cursorHidden_) {
                    ShowCursor(TRUE);  // Restore cursor before exiting
                    cursorHidden_ = false;
                }
                return;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        // Pass input to scene - Scene owns and controls the camera
        m_scene->ProcessInput(inputState_, dt);

        // Reset mouse delta after processing
        inputState_.mouseDeltaX = 0;
        inputState_.mouseDeltaY = 0;

        if (imguiInitialized_) {
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            if (imguiLayer_) imguiLayer_->Draw();

            ImGui::Render();
        }

        // Renderer will call Update and Render
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
