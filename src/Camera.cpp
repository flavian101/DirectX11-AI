#include "Camera.h"
#include <DirectXMath.h>

using namespace DirectX;

Camera::Camera() : pos_{0,0,-5}, target_{0,0,0}, up_{0,1,0}, width_(800), height_(600) {
    Update();
}

void Camera::SetPosition(const XMFLOAT3 &pos){ pos_=pos; Update(); }
void Camera::SetTarget(const XMFLOAT3 &tar){ target_=tar; Update(); }
void Camera::SetUp(const XMFLOAT3 &up){ up_=up; Update(); }
void Camera::SetWindowSize(int w, int h){ width_=w; height_=h; Update(); }

void Camera::Update(){
    XMMATRIX viewMat = XMMatrixLookAtLH(XMLoadFloat3(&pos_), XMLoadFloat3(&target_), XMLoadFloat3(&up_));
    XMMATRIX projMat = XMMatrixPerspectiveFovLH(XMConvertToRadians(45.0f), (float)width_/(float)height_, 0.01f, 1000.0f);
    XMStoreFloat4x4(&view_, viewMat);
    XMStoreFloat4x4(&proj_, projMat);
}

XMFLOAT4X4 Camera::GetViewMatrix() const{ return view_; }
XMFLOAT4X4 Camera::GetProjectionMatrix() const{ return proj_; }
