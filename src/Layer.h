#pragma once
#include <windows.h>
#include <d3d11.h>

class Scene;
class Direct3D11Renderer;

// What a Layer may touch each frame. References, not pointers: they are valid for as long as
// the layer is being called.
struct LayerContext {
    Scene& scene;
    Direct3D11Renderer& renderer;
};

// An optional extension that a host program plugs into the Window. The engine knows ONLY this
// interface - it never includes ImGui. The editor is one implementation (editor/EditorLayer);
// a shipped game plugs in nothing, so none of the editor's code or dependencies are even linked.
class Layer {
public:
    virtual ~Layer() = default;

    // Called once the window and Direct3D exist. Return false to refuse (the Window drops you).
    virtual bool OnAttach(HWND hwnd, Direct3D11Renderer& renderer) = 0;
    // Called before the renderer is destroyed. Must be safe to call twice.
    virtual void OnDetach() = 0;

    // First look at every Windows message. Return true if the layer consumed it.
    virtual bool OnWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        (void)hwnd; (void)msg; (void)wParam; (void)lParam;
        return false;
    }

    // Once per frame, after the simulation update and before drawing: build the UI, and tell the
    // renderer where the 3D view should go.
    virtual void OnFrame(LayerContext& ctx) = 0;

    // After the 3D view is finished, with the window's back buffer bound: draw on top of it.
    virtual void OnRenderOverlay(ID3D11DeviceContext* ctx) = 0;

    // Should the 3D world ignore input right now? (pointer over a panel, typing in a text box)
    virtual bool BlocksSceneMouse() const { return false; }
    virtual bool BlocksSceneKeyboard() const { return false; }
};
