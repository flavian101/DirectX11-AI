#pragma once
#include <DirectXMath.h>
#include <d3d11.h>

class Camera {
public:
    Camera();
    void SetPosition(const DirectX::XMFLOAT3 &pos);
    void SetTarget(const DirectX::XMFLOAT3 &tar);
    void SetUp(const DirectX::XMFLOAT3 &up);
    void Update();
    DirectX::XMFLOAT4X4 GetViewMatrix() const;
    DirectX::XMFLOAT4X4 GetProjectionMatrix() const;
    DirectX::XMFLOAT3 GetPosition() const { return pos_; }
    void SetWindowSize(int width, int height);

private:
    DirectX::XMFLOAT3 pos_;
    DirectX::XMFLOAT3 target_;
    DirectX::XMFLOAT3 up_;
    DirectX::XMFLOAT4X4 view_;
    DirectX::XMFLOAT4X4 proj_;
    int width_;
    int height_;
};
