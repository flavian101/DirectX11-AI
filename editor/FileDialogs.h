#pragma once
// The Windows "Open" dialogs of the editor. Each returns the chosen file as UTF-8, or an empty string if the person cancelled.
#include <windows.h>
#include <string>

std::string PickModelFile(HWND owner);   // glTF, FBX, OBJ
std::string PickImageFile(HWND owner);   // PNG, JPG, TGA, BMP (starts in "assets")
