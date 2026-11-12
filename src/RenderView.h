#pragma once
#include "Frustum.h"
#include <DirectXMath.h>

// What kind of image a pass is producing. Used for statistics and to let objects behave
// differently per view (e.g. shadows cull against the sun's box, not the camera's).
enum class ViewKind : int { Main = 0, Reflection = 1, Shadow = 2 };
constexpr int kViewKindCount = 3;

struct WaterFramePlan;   // WaterPlan.h

// Everything an object needs to know about the view it is being drawn into.
struct RenderView {
    ViewKind kind = ViewKind::Main;
    Frustum frustum;                       // what this view can see: objects outside it can be skipped
    DirectX::XMFLOAT3 lodOrigin{};         // where level-of-detail distances are measured from: always the
                                           // REAL camera, even for the mirrored and sun views
    float projScale = 1.7320508f;          // cot(fov / 2) of the REAL camera: turns a size and a distance into "how much of the
                                           // screen" (LodSelect.h). The default is a 60 degree lens.
    const WaterFramePlan* waterPlan = nullptr;   // the camera view only: which mirror image each water body reads
    // The mirror view throws away (clip plane) everything below the water. So anything whose box lies wholly below this
    // height cannot show a single pixel there, and the draw code may skip it before it costs a vertex.
    bool clipBelow = false;
    float clipBelowY = 0.0f;
};
