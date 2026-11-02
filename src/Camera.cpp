#include "Camera.h"
#include <DirectXMath.h>

using namespace DirectX;

Camera::Camera() : pos_{ 0,0,-5 }, target_{ 0,0,0 }, up_{ 0,1,0 }, width_(800), height_(600) {
    forward_ = { 0.0f, 0.0f, 1.0f };   
    right_ = { 1.0f, 0.0f, 0.0f };   
    Update();
}

void Camera::SetPosition(const XMFLOAT3 &pos){ pos_=pos; Update(); }
void Camera::SetTarget(const XMFLOAT3 &tar){ target_=tar; Update(); }
void Camera::SetUp(const XMFLOAT3 &up){ up_=up; Update(); }
void Camera::SetWindowSize(int w, int h){ width_=w; height_=h; Update(); }

void Camera::Move(bool forward, bool backward, bool left, bool right, float dt)
{
    // Nothing pressed -> don't touch pos_, target_ or orientation
    if (!forward && !backward && !left && !right)
        return;

    XMVECTOR fwdVec = XMLoadFloat3(&forward_);
    XMVECTOR rgtVec = XMLoadFloat3(&right_);

    XMVECTOR dir = XMVectorZero();
    if (forward)  dir = XMVectorAdd(dir, fwdVec);
    if (backward) dir = XMVectorSubtract(dir, fwdVec);
    if (right)    dir = XMVectorAdd(dir, rgtVec);
    if (left)     dir = XMVectorSubtract(dir, rgtVec);

    dir = XMVector3Normalize(dir);
    XMVECTOR displacement = XMVectorScale(dir, moveSpeed_ * dt);

    XMVECTOR posVec = XMLoadFloat3(&pos_);
    XMVECTOR tgtVec = XMLoadFloat3(&target_);
    XMStoreFloat3(&pos_, XMVectorAdd(posVec, displacement));
    XMStoreFloat3(&target_, XMVectorAdd(tgtVec, displacement));
    Update();
}

void Camera::Rotate(float deltaX, float deltaY)
{
    float yaw = deltaX * rotationSpeed_;
    float pitch = deltaY * rotationSpeed_;

    // Rotate forward vector around up vector (yaw)
    XMVECTOR fwdVec = XMLoadFloat3(&forward_);
    XMVECTOR upVec = XMLoadFloat3(&up_);
    XMMATRIX yawMat = XMMatrixRotationAxis(upVec, yaw);
    fwdVec = XMVector3TransformNormal(fwdVec, yawMat);

    // Rotate forward vector around right vector (pitch)
    XMVECTOR rgtVec = XMLoadFloat3(&right_);
    rgtVec = XMVector3TransformNormal(rgtVec, yawMat);
    XMMATRIX pitchMat = XMMatrixRotationAxis(rgtVec, pitch);
    fwdVec = XMVector3TransformNormal(fwdVec, pitchMat);

    // Recalculate right vector as cross product
    XMVECTOR newRight = XMVector3Cross(upVec, fwdVec);

    XMStoreFloat3(&forward_, fwdVec);
    XMStoreFloat3(&right_, newRight);

    // Update target based on new forward direction
    XMVECTOR posVec = XMLoadFloat3(&pos_);
    XMVECTOR newTarget = posVec + fwdVec;
    XMStoreFloat3(&target_, newTarget);
    Update();
}

void Camera::Update(){
    XMMATRIX viewMat = XMMatrixLookAtLH(XMLoadFloat3(&pos_), XMLoadFloat3(&target_), XMLoadFloat3(&up_));
    XMMATRIX projMat = XMMatrixPerspectiveFovLH(XMConvertToRadians(45.0f), (float)width_/(float)height_, 0.01f, 1000.0f);
    XMStoreFloat4x4(&view_, viewMat);
    XMStoreFloat4x4(&proj_, projMat);
}

XMFLOAT4X4 Camera::GetViewMatrix() const{ return view_; }
XMFLOAT4X4 Camera::GetProjectionMatrix() const{ return proj_; }
