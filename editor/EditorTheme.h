#pragma once

// The editor's look: one place for every colour, radius and spacing, so the panels do not each carry their own opinion.
// Call once after ImGui::CreateContext(), instead of (or after) ImGui::StyleColorsDark().
void ApplyEditorTheme();

// The UI font and the pixel scale for high-DPI screens. Call AFTER ApplyEditorTheme() (it scales the spacing) and before the
// first frame. Uses Segoe UI from the Windows fonts folder when it is there, ImGui's built-in font when it is not.
// `dpiScale` is 1.0 on a normal screen, 1.5 on a 150% one (ImGui_ImplWin32_GetDpiScaleForHwnd).
void ApplyEditorFont(float dpiScale);
