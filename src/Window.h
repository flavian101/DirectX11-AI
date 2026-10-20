#pragma once
#include <Windows.h>

class D3D11Window {
public:
    D3D11Window(HINSTANCE hInstance, int nCmdShow);
    LRESULT MessageHandler(HWND, UINT, WPARAM, LPARAM);
    void Run();
private:
    HWND _hwnd;
};

static D3D11Window* windowHandle = 0;
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
