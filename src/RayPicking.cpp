#include "RayPicking.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

Ray MakePickRay(const XMFLOAT4X4& view, const XMFLOAT4X4& projection, float ndcX, float ndcY)
{
    // A pixel on screen is a LINE of points in the world: every point that projects to that pixel. Two of
    // them are enough: the one on the near plane (depth 0 in Direct3D) and the one on the far plane (depth 1).
    // Un-projecting means running the camera transform backwards: multiply by the inverse of view * projection,
    // then divide by w (the perspective divide, undone).
    const XMMATRIX viewProjection = XMLoadFloat4x4(&view) * XMLoadFloat4x4(&projection);
    const XMMATRIX inverse = XMMatrixInverse(nullptr, viewProjection);
    const XMVECTOR nearPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), inverse);   // TransformCoord divides by w
    const XMVECTOR farPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), inverse);
    Ray ray;
    XMStoreFloat3(&ray.origin, nearPoint);
    XMStoreFloat3(&ray.direction, XMVector3Normalize(XMVectorSubtract(farPoint, nearPoint)));
    return ray;
}

bool RayIntersectsBox(const Ray& ray, const XMFLOAT4X4& world, const XMFLOAT3& boxMin, const XMFLOAT3& boxMax,
                      float& distance)
{
    // Easiest to test a box that is axis-aligned. So instead of rotating the box into the world, carry the RAY
    // into the box's own space (where the box is a plain axis-aligned one) and test it there.
    const XMMATRIX worldMatrix = XMLoadFloat4x4(&world);
    if (std::fabs(XMVectorGetX(XMMatrixDeterminant(worldMatrix))) < 1e-12f) return false;   // squashed flat: nothing to hit
    const XMMATRIX inverse = XMMatrixInverse(nullptr, worldMatrix);
    const XMVECTOR localOrigin = XMVector3TransformCoord(XMLoadFloat3(&ray.origin), inverse);
    const XMVECTOR localDirection = XMVector3TransformNormal(XMLoadFloat3(&ray.direction), inverse);   // not normalised: see below

    XMFLOAT3 o, d;
    XMStoreFloat3(&o, localOrigin);
    XMStoreFloat3(&d, localDirection);
    const float origin[3] = { o.x, o.y, o.z };
    const float direction[3] = { d.x, d.y, d.z };
    const float low[3] = { boxMin.x, boxMin.y, boxMin.z };
    const float high[3] = { boxMax.x, boxMax.y, boxMax.z };

    // The "slab" test: the box is three pairs of parallel planes. For each pair, find when the ray enters and
    // leaves the slab between them. The ray is inside the box only while it is inside ALL three slabs at once.
    float tEnter = 0.0f;          // only count hits in front of the ray's start
    float tExit = 3.402823466e+38f;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(direction[axis]) < 1e-12f) {
            if (origin[axis] < low[axis] || origin[axis] > high[axis]) return false;   // parallel to the slab and outside it
        } else {
            float t0 = (low[axis] - origin[axis]) / direction[axis];
            float t1 = (high[axis] - origin[axis]) / direction[axis];
            if (t0 > t1) std::swap(t0, t1);
            tEnter = (std::max)(tEnter, t0);
            tExit = (std::min)(tExit, t1);
            if (tEnter > tExit) return false;
        }
    }
    // Because the local direction was transformed WITHOUT renormalising, t is measured in the same units as the
    // original ray, so it is already the distance in world units: scaling the box scales the direction vector to match.
    distance = tEnter;
    return true;
}
