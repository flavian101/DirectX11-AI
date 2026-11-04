#include "Frustum.h"

using namespace DirectX;

Frustum Frustum::FromViewProj(const XMFLOAT4X4& viewProj)
{
    // A point is inside the view volume when, after multiplying by viewProj,
    //     -w <= x <= w     -w <= y <= w     0 <= z <= w      (Direct3D clip space)
    // Each inequality is "a weighted sum of the point's coordinates >= 0", i.e. a plane. With
    // row vectors, the weights for clip.x/y/z/w are the matrix COLUMNS, so every plane is a
    // sum or difference of two columns (the Gribb-Hartmann method).
    auto column = [&](int c) {
        return XMVectorSet(viewProj.m[0][c], viewProj.m[1][c], viewProj.m[2][c], viewProj.m[3][c]);
    };
    const XMVECTOR c0 = column(0), c1 = column(1), c2 = column(2), c3 = column(3);

    const XMVECTOR raw[6] = {
        c3 + c0,   // left:   w + x >= 0
        c3 - c0,   // right:  w - x >= 0
        c3 + c1,   // bottom: w + y >= 0
        c3 - c1,   // top:    w - y >= 0
        c2,        // near:       z >= 0
        c3 - c2,   // far:    w - z >= 0
    };

    Frustum frustum;
    for (int i = 0; i < 6; ++i) {
        // Normalizing makes "dot(n, p) + d" a real distance in world units.
        XMStoreFloat4(&frustum.planes_[i], XMPlaneNormalize(raw[i]));
    }
    return frustum;
}

bool Frustum::IntersectsAABB(const XMFLOAT3& boxMin, const XMFLOAT3& boxMax) const
{
    for (const XMFLOAT4& plane : planes_) {
        // The box corner that sticks out FURTHEST in the plane's direction (the "positive vertex").
        // If even that corner is behind the plane, the whole box is.
        const float x = plane.x >= 0.0f ? boxMax.x : boxMin.x;
        const float y = plane.y >= 0.0f ? boxMax.y : boxMin.y;
        const float z = plane.z >= 0.0f ? boxMax.z : boxMin.z;
        if (plane.x * x + plane.y * y + plane.z * z + plane.w < 0.0f) return false;
    }
    return true;
}
