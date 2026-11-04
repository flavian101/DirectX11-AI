#include "Window.h"
#include <algorithm>
#include <chrono>
#include <windowsx.h>
#include "Logger.h"

Window::Window(int width, int height, const wchar_t* title) : m_Hinstance(GetModuleHandle(nullptr))
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = m_Hinstance;
    wc.lpszClassName = L"D3D11WindowClass";
    RegisterClassW(&wc);
    windowHandle = this;

    _hwnd = CreateWindowExW(0, wc.lpszClassName, title,
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        width, height, nullptr, nullptr, m_Hinstance, nullptr);
    if (!_hwnd) {
        LOG_ERROR("Failed to create window");
        return;
    }

    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01;   // Generic desktop controls
    rid.usUsage = 0x02;       // Mouse
    rid.dwFlags = 0;
    rid.hwndTarget = _hwnd;

    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid)))
    {
        LOG_ERROR("Failed to register raw input");
    }

    ShowWindow(_hwnd, SW_SHOW);
    if (!InitD3D(_hwnd)) {
        LOG_ERROR("Failed to initialize Direct3D 11");
        return;
    }
}

void Window::SetLayer(std::unique_ptr<Layer> layer)
{
    if (layer_) layer_->OnDetach();
    layer_ = std::move(layer);
    if (layer_ && (!m_renderer || !layer_->OnAttach(_hwnd, *m_renderer))) {
        LOG_ERROR("The window layer failed to attach; running without it");
        layer_.reset();
    }
}

void Window::SetScene(std::unique_ptr<Scene> scene)
{
    m_scene = std::move(scene);
    if (!m_scene) return;
    // The camera defaults to 800x600 but the real view is a different size (title bar, borders, panels).
    if (m_renderer) m_scene->GetCamera().SetWindowSize(static_cast<int>(m_renderer->GetViewWidth()),
                                                       static_cast<int>(m_renderer->GetViewHeight()));
}

Window::~Window()
{
    // The layer owns GPU objects of its own (the editor UI): it must go before the device does.
    if (layer_) layer_->OnDetach();
    layer_.reset();
    // Destroy scene GPU resources while the D3D device is still alive.
    m_scene.reset();
    delete m_renderer;
    if (windowHandle == this) windowHandle = nullptr;
}

void Window::OnResize(UINT width, UINT height)
{
    if (!m_renderer || width == 0 || height == 0) return;
    if (!m_renderer->Resize(width, height)) return;
    // Same field of view, new aspect ratio - otherwise the image is stretched. The aspect is the
    // VIEW's: the window in a game, the Viewport panel in the editor (which sets it itself).
    if (m_scene) m_scene->GetCamera().SetWindowSize(static_cast<int>(m_renderer->GetViewWidth()),
                                                    static_cast<int>(m_renderer->GetViewHeight()));
}

LRESULT Window::MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (layer_ && layer_->OnWindowMessage(hwnd, msg, wParam, lParam)) {
        return 0;
    }

    switch (msg) {
    case WM_SIZE:
        minimized_ = (wParam == SIZE_MINIMIZED);
        // While the user drags a border, WM_SIZE fires for every pixel. Rebuilding
        // the swap chain each time is wasteful, so wait for WM_EXITSIZEMOVE.
        // (Maximize/restore arrive here with resizing_ == false and resize at once.)
        if (!minimized_ && !resizing_) OnResize(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_ENTERSIZEMOVE:
        resizing_ = true;
        return 0;
    case WM_EXITSIZEMOVE:
    {
        resizing_ = false;
        RECT client{};
        if (GetClientRect(hwnd, &client)) OnResize(client.right - client.left, client.bottom - client.top);
        return 0;
    }
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

    case WM_INPUT:
    {
        UINT size = 0;

        if (GetRawInputData(
            reinterpret_cast<HRAWINPUT>(lParam),
            RID_INPUT,
            nullptr,
            &size,
            sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1))
        {
            return 0;
        }

        std::vector<BYTE> buffer(size);

        if (GetRawInputData(
            reinterpret_cast<HRAWINPUT>(lParam),
            RID_INPUT,
            buffer.data(),
            &size,
            sizeof(RAWINPUTHEADER)) == size)
        {
            RAWINPUT* raw =
                reinterpret_cast<RAWINPUT*>(buffer.data());

            if (raw->header.dwType == RIM_TYPEMOUSE &&
                inputState_.middleMouseButtonDown)
            {
                inputState_.mouseDeltaX += raw->data.mouse.lLastX;
                inputState_.mouseDeltaY += raw->data.mouse.lLastY;
            }
        }

        return 0;
    }
    case WM_MBUTTONDOWN:
    {
        // Only start looking around when the click landed on the 3D view, not on a panel.
        if (layer_ && layer_->BlocksSceneMouse()) break;
        SetCapture(hwnd);

        if (!cursorHidden_)
        {
            ShowCursor(FALSE);
            cursorHidden_ = true;
        }

        inputState_.middleMouseButtonDown = true;

        return 0;
    }

    case WM_MBUTTONUP:
    {
        ReleaseCapture();

        if (cursorHidden_)
        {
            ShowCursor(TRUE);
            cursorHidden_ = false;
        }

        inputState_.middleMouseButtonDown = false;
        inputState_.mouseDeltaX = 0;
        inputState_.mouseDeltaY = 0;

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
        if (minimized_) {          // nothing to draw into: sleep until the next message
            WaitMessage();
            lastTime = std::chrono::steady_clock::now();
            continue;
        }
        const auto now = std::chrono::steady_clock::now();
        // Clamp: after a stall (dragging the window, a breakpoint) one giant dt would
        // teleport the camera and fast-forward every animation.
        const float dt = (std::min)(std::chrono::duration<float>(now - lastTime).count(), 0.1f);
        lastTime = now;

        // 1. input -> camera, 2. advance the simulation, 3. build UI, 4. draw.
        InputState input = inputState_;
        if (layer_ && layer_->BlocksSceneKeyboard()) input.keys.fill(false);   // typing in a text box
        m_scene->ProcessInput(input, dt);
        m_scene->Update(dt);

        // Reset mouse delta after processing
        inputState_.mouseDeltaX = 0;
        inputState_.mouseDeltaY = 0;

        if (layer_) {
            LayerContext context{ *m_scene, *m_renderer };
            layer_->OnFrame(context);
        }

        m_renderer->Render(*m_scene, layer_.get());
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
