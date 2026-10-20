#include "Window.h"
#include "Window.h"
#include <iostream>

D3D11Window::D3D11Window(HINSTANCE hInstance, int nCmdShow) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"D3D11WindowClass";
    RegisterClassW(&wc);
    windowHandle = this;
    _hwnd = CreateWindowExW(0, wc.lpszClassName, L"DirectX 11 Demo",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600, NULL, NULL, hInstance, NULL);
    ShowWindow(_hwnd, nCmdShow);
}

LRESULT D3D11Window::MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {


    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

}

void D3D11Window::Run() {
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
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
