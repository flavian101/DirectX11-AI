#pragma once

// The editor's look: one place for every colour, radius and spacing, so the panels do not each carry their own opinion.
// Call once after ImGui::CreateContext(), instead of (or after) ImGui::StyleColorsDark().
void ApplyEditorTheme();
