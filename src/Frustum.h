#pragma once
#include <DirectXMath.h>

// The six planes that bound what a camera (or the sun's orthographic box) can see.
// "Inside" means on the positive side of every plane: dot(normal, point) + d >= 0.
class Frustum {
public:
    // viewProj = view * projection, in DirectXMath's row-vector convention (point * matrix).
    static Frustum FromViewProj(const DirectX::XMFLOAT4X4& viewProj);

    // False only when the box is entirely outside at least one plane (so it cannot be visible).
    // Conservative: a box near a frustum corner may be reported as "maybe visible" - that is fine,
    // drawing a few extra objects is far cheaper than a precise test.
    bool IntersectsAABB(const DirectX::XMFLOAT3& boxMin, const DirectX::XMFLOAT3& boxMax) const;

private:
    DirectX::XMFLOAT4 planes_[6]{};   // left, right, bottom, top, near, far (a,b,c,d), normalized
};
