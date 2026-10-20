#define UNICODE
#include <windows.h>
#include "Window.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    D3D11Window* win = new D3D11Window(hInstance, nCmdShow);
    win->Run();
    return 0;
}
