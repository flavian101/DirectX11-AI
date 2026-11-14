#include "LightObject.h"
#include <algorithm>

using namespace DirectX;

LightObject::LightObject(LightKind kind) : kind_(kind)
{
    if (kind == LightKind::Directional) ApplySunPreset(*this);
}

void LightObject::SetKind(LightKind kind)
{
    if (kind == kind_) return;
    kind_ = kind;
    if (kind == LightKind::Directional && intensity_ < 0.01f) intensity_ = 1.0f;
}

void LightObject::SetColor(const XMFLOAT3& color)
{
    color_ = { std::clamp(color.x, 0.0f, 1.0f), std::clamp(color.y, 0.0f, 1.0f), std::clamp(color.z, 0.0f, 1.0f) };
}

void LightObject::SetIntensity(float intensity) { intensity_ = std::clamp(intensity, 0.0f, kIntensityMax); }
void LightObject::SetRange(float range) { range_ = std::clamp(range, kRangeMin, kRangeMax); }

void LightObject::ApplySunPreset(LightObject& light)
{
    light.kind_ = LightKind::Directional;
    light.role_ = LightRole::Sun;
    light.color_ = { 1.0f, 0.95f, 0.85f };
    light.intensity_ = 2.5f;   // HDR: the sun is far brighter than "1 = white"; the tone mapper brings it back
    light.position_ = { 0.0f, 40.0f, 0.0f };
    light.SetAngles(200.0f, 55.0f);
}

void LightObject::ApplyMoonPreset(LightObject& light)
{
    light.kind_ = LightKind::Directional;
    light.role_ = LightRole::Moon;
    light.color_ = { 0.62f, 0.72f, 1.0f };
    light.intensity_ = 0.25f;
    light.position_ = { 0.0f, 40.0f, 0.0f };
    light.SetAngles(20.0f, 40.0f);   // roughly opposite the sun preset
}

XMFLOAT3 LightObject::GetDirection() const
{
    const LightAngles::Vec3 d = LightAngles::TravelFromEuler(rotation_.x, rotation_.y, rotation_.z);
    return { d.x, d.y, d.z };
}

float LightObject::GetAzimuth() const
{
    const XMFLOAT3 d = GetDirection();
    float azimuth = 0.0f, elevation = 0.0f;
    LightAngles::FromTravel({ d.x, d.y, d.z }, azimuth, elevation);
    return azimuth;
}

float LightObject::GetElevation() const
{
    const XMFLOAT3 d = GetDirection();
    float azimuth = 0.0f, elevation = 0.0f;
    LightAngles::FromTravel({ d.x, d.y, d.z }, azimuth, elevation);
    return elevation;
}

void LightObject::SetAngles(float azimuthDegrees, float elevationDegrees)
{
    elevationDegrees = std::clamp(elevationDegrees, -90.0f, 90.0f);
    float pitch = rotation_.x, yaw = rotation_.y;
    LightAngles::EulerFromTravel(LightAngles::Travel(azimuthDegrees, elevationDegrees), pitch, yaw);
    rotation_ = { pitch, yaw, 0.0f };
}

bool LightObject::GetEditorTransform(XMFLOAT3& position, XMFLOAT3& rotation, XMFLOAT3& scale) const
{
    position = position_;
    rotation = rotation_;
    scale = { 1.0f, 1.0f, 1.0f };
    return true;
}

bool LightObject::SetEditorTransform(const XMFLOAT3& position, const XMFLOAT3& rotation, const XMFLOAT3&)
{
    position_ = position;
    if (IsDirectional()) rotation_ = rotation;
    return true;
}

bool LightObject::GetOrientedBounds(XMFLOAT4X4& world, XMFLOAT3& boundsMin, XMFLOAT3& boundsMax) const
{
    // A small box around the handle: big enough to click, small enough not to get in the way.
    XMStoreFloat4x4(&world, XMMatrixTranslation(position_.x, position_.y, position_.z));
    boundsMin = { -0.5f, -0.5f, -0.5f };
    boundsMax = { 0.5f, 0.5f, 0.5f };
    return true;
}

std::unique_ptr<SceneObject> LightObject::Clone(ID3D11Device*) const
{
    auto copy = std::make_unique<LightObject>(kind_);
    copy->color_ = color_;
    copy->intensity_ = intensity_;
    copy->range_ = range_;
    copy->position_ = position_;
    copy->rotation_ = rotation_;
    copy->role_ = role_;
    return copy;
}
