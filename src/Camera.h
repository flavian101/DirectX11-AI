#pragma once
#include <DirectXMath.h>
#include <d3d11.h>

class Camera {
public:
    // 0.1 instead of 0.01: depth precision is spent mostly near the camera, so a tiny near
    // plane makes distant depth values coarse (lesson 8). Nothing in the scene is closer than this.
    static constexpr float kNearPlane = 0.1f;
    static constexpr float kFarPlane = 1000.0f;

    Camera();
    void SetPosition(const DirectX::XMFLOAT3 &pos);
    void SetTarget(const DirectX::XMFLOAT3 &tar);
    void SetUp(const DirectX::XMFLOAT3 &up);
    void Update();
    DirectX::XMFLOAT4X4 GetViewMatrix() const;
    DirectX::XMFLOAT4X4 GetProjectionMatrix() const;
    DirectX::XMFLOAT3 GetPosition() const { return pos_; }
    DirectX::XMFLOAT3 GetTarget() const { return target_; }
    DirectX::XMFLOAT3 GetUp() const { return up_; }
    float GetMoveSpeed() const { return moveSpeed_; }
    float GetRotationSpeed() const { return rotationSpeed_; }
    void SetWindowSize(int width, int height);

    // Camera movement methods
    void Move(bool forward, bool backward, bool left, bool right, float dt);
    void Rotate(float deltaX, float deltaY);
    void SetMoveSpeed(float speed) { moveSpeed_ = speed; }
    void SetRotationSpeed(float speed) { rotationSpeed_ = speed; }

private:
    void UpdateOrientation();

    DirectX::XMFLOAT3 pos_;
    DirectX::XMFLOAT3 target_;
    DirectX::XMFLOAT3 up_;
    DirectX::XMFLOAT4X4 view_;
    DirectX::XMFLOAT4X4 proj_;
    int width_;
    int height_;

    // Camera movement
    float moveSpeed_ = 30.0f;
    float rotationSpeed_ = 0.002f;
    DirectX::XMFLOAT3 forward_ = {0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT3 right_ = {1.0f, 0.0f, 0.0f};
};
