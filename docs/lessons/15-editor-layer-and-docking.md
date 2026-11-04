# Lesson 15 - An editor that can be left behind

## Understand

**The editor is a guest, not a part of the engine.** The engine draws and simulates; the editor looks at it and pokes it. If the engine never mentions the editor, shipping a game means not building the editor. There is nothing to "remove".

**The arrow only points one way.** `EditorLayer` includes `Scene.h`. `Scene.h` never includes `EditorLayer.h`. The moment an engine file includes `imgui.h`, the editor is welded in and you are back to `#ifdef` everywhere.

## Visualize

**Build both and compare the two exes.** `D3D11Example.exe` (the game) is about a third of the size of `D3D11Editor.exe` and contains no ImGui at all. Run the editor and drag the Hierarchy tab onto the Viewport's edge: the dock preview shows where it will land.

## Why it is built this way

**A `Layer` is the only door.** `Layer.h` is a tiny interface: attach, detach, see a Windows message, build a frame, draw an overlay, and two questions ("should the world ignore the mouse / keyboard?"). `Window` and `Direct3D11Renderer` talk to that interface and nothing else. The editor is one implementation; the game plugs in none.

**Three CMake targets.** `Engine` is a static library with every engine `.cpp`. `D3D11Example` is `main.cpp` plus `Engine`. `D3D11Editor` is `EditorLayer`, the ImGui sources and `Engine`. Exporting = building `D3D11Example` and copying `shaders/` and `assets/`.

**The 3D view is no longer the window.** In a game the world is drawn at window size and resolved onto the back buffer. In the editor the Viewport panel is smaller than the window, so the renderer resolves into a texture (`SetViewTargetSize`) and ImGui shows that texture with `ImGui::Image`. The HDR buffer, depth buffer and reflection map all follow the *view* size, not the window size. The window size only decides the back buffer.

**Why the size lags a frame.** The panel only knows its size while it is being laid out, which happens after the textures for this frame were already chosen. So the panel records its size, and next frame `OnFrame` applies it first. During a drag you may see one stretched frame. Cheap, and no feedback loop.

**The camera's aspect follows the view.** Resize the panel and the camera must change its aspect ratio or the picture stretches. The editor sets it when the view size changes; the window only does it in game mode.

**Input needs a gate.** Without one, clicking a slider with the middle button would also start the camera look, and typing "w" in a text box would fly the camera. `BlocksSceneMouse` (pointer is not over the Viewport) and `BlocksSceneKeyboard` (a text field wants the keys) answer that. The window asks only when the middle button goes down, so a look that started inside the Viewport keeps working when the pointer leaves it.

**Docking is a layout, and a layout is data.** `ImGuiConfigFlags_DockingEnable` turns it on. The host window owns a `DockSpace`; panels are ordinary windows that dock into it. On a first run `DockBuilder` carves left / middle / right and assigns panels by name. After that ImGui saves the arrangement in `imgui.ini`, which is why we only build when no node exists. `View > Reset layout` deletes and rebuilds it.

**`ConfigWindowsMoveFromTitleBarOnly`.** The Viewport is a window full of one image. Without this flag, dragging on the image drags the window out of its dock.

## Other ways, and the trade-offs

**`#ifdef EDITOR` in one binary.** One target, a compile switch. Simple at first, but every file that touches the editor grows `#ifdef`s, and a stray include still drags ImGui into the game. **Runtime flag.** Ship the editor code but hide it: easy, but it ships your tools and their bugs.

**Dynamic libraries.** Engine as a DLL, editor as another. Clean, but you pay in export macros and in ABI care across the boundary. **Render to the swap chain and use ImGui viewports.** Skips the texture, but you lose control of the view size and post-processing.

## Predict, then check

1. Why does `Direct3D11Renderer::Render` take a `Layer*` instead of calling ImGui directly?
2. In editor mode, the window is resized. Which textures are rebuilt, and which are not?
3. What goes wrong if `SetViewTargetSize` releases the old HDR buffer while it is still bound at pixel shader slot 0?

**Answers.** (1) The renderer must compile without ImGui, or the game would need it too. (2) Only the back buffer view; the view textures belong to the panel's size. (3) Direct3D refuses to bind it as a render target while it is also a shader input, so the frame silently draws nothing. That is why the function unbinds first.
