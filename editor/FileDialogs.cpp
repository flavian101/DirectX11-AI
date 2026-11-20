#include "FileDialogs.h"
#include <commdlg.h>   // GetOpenFileNameW

// Shows the Open dialog and returns the chosen file as UTF-8 (empty = cancelled).
std::string PickModelFile(HWND owner)
{
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"3D models (*.gltf;*.glb;*.fbx;*.obj)\0*.gltf;*.glb;*.fbx;*.obj\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    // NOCHANGEDIR matters: without it the dialog moves the program's working directory to the folder you
    // browsed, and every relative path afterwards ("shaders/vs.hlsl", "assets/...") stops resolving.
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, file, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return {};
    std::string path(static_cast<size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, file, -1, path.data(), bytes, nullptr, nullptr);
    return path;
}

// Open dialog for an image (a terrain texture or heightmap). Returns UTF-8, empty if cancelled.
std::string PickImageFile(HWND owner)
{
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.tga;*.bmp)\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrInitialDir = L"assets";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;   // NOCHANGEDIR: see PickModelFile
    if (!GetOpenFileNameW(&dialog)) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, file, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return {};
    std::string path(static_cast<size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, file, -1, path.data(), bytes, nullptr, nullptr);
    return path;
}
