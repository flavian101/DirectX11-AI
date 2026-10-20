#define UNICODE
#include <windows.h>
#include "Window.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    Window* win = new Window(hInstance, nCmdShow);
    win->Run();
    return 0;
}
