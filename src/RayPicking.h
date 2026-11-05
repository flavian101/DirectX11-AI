#pragma once
// Turning a click in the 3D view into a ray through the world, and testing that ray against boxes.
// Pure maths on DirectXMath types: no Direct3D, no ImGui.
#include <DirectXMath.h>

struct Ray {
    DirectX::XMFLOAT3 origin;
    DirectX::XMFLOAT3 direction;   // unit length
};

// ndcX / ndcY: the click position in normalised device coordinates: x from -1 (left edge of the view) to +1
// (right edge), y from -1 (BOTTOM edge) to +1 (top edge). `view` and `projection` are the camera's matrices.
Ray MakePickRay(const DirectX::XMFLOAT4X4& view, const DirectX::XMFLOAT4X4& projection, float ndcX, float ndcY);

// Does the ray hit the box [boxMin, boxMax] that lives in an object's local space and is placed in the world by
// `world`? On a hit, `distance` is how far along the ray (in world units) the first contact is.
bool RayIntersectsBox(const Ray& ray, const DirectX::XMFLOAT4X4& world, const DirectX::XMFLOAT3& boxMin,
                      const DirectX::XMFLOAT3& boxMax, float& distance);
