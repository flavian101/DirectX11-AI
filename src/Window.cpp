#include "Window.h"
#include <iostream>
#include <comdef.h>

Window::Window(HINSTANCE hInstance, int nCmdShow) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"D3D11WindowClass";
    RegisterClassW(&wc);
    windowHandle = this;
    _hwnd = CreateWindowExW(0, wc.lpszClassName, L"DirectX 11 Demo",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600, NULL, NULL, hInstance, NULL);

    if (_hwnd == NULL)
    {
        OutputDebugString("Failed to create window");
        return;
    }
    ShowWindow(_hwnd, nCmdShow);
    if (!InitD3D(_hwnd)) {
        OutputDebugString("Failed to initialize direct x 11");
        return;
    }
}

LRESULT Window::MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void Window::Run() {
    MSG msg = {};
    while (true) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                return;
        }

        // rendering
        m_renderer->Render();
    }
}

Window::~Window() {
    if (m_renderer) m_renderer->Release();
}

bool Window::InitD3D(HWND hwnd) {
    Direct3D11Renderer* renderer = new Direct3D11Renderer(hwnd);
    if (!renderer->Init()) {
        delete renderer;
        return false;
    }
    m_renderer = renderer;
    return true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_CLOSE:
    {
        PostQuitMessage(0);
        return 0;
    }

    default:
    {
        return windowHandle->MessageHandler(hwnd, msg, wParam, lParam);
    }
    }
}
