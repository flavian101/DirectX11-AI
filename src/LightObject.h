#pragma once
#include "LightAngles.h"
#include "TimeOfDay.h"
#include "SceneObject.h"
#include <DirectXMath.h>
#include <memory>

// Which kind of light. Saved by name (see SceneJson.h): only ever ADD to the end.
enum class LightKind : int { Directional = 0, Point = 1 };

// A light in the scene, as an object you can pick, drag, duplicate and delete like any other.
//   Directional - light from very far away, all rays parallel: the SUN and the MOON are both just directional lights
//                 (a preset colour and strength each). It has a position, but only so the editor has a handle to grab:
//                 where it stands changes nothing. What matters is the way it is aimed (azimuth and elevation).
//   Point       - light from one spot that fades out over its range: a lamp, a torch, a campfire.
// The scene gathers its light objects every frame (Scene::ApplyLights): the strongest directional light is the one that casts
// shadows, the next strongest lights the scene without shadows (a moon beside a sun), and the point lights nearest the
// camera fill the GPU's point light slots.
class LightObject final : public SceneObject {
public:
    explicit LightObject(LightKind kind = LightKind::Point);

    // Ready-made settings for a new light. They set the colour, strength and aim; the caller still places it and names it.
    static void ApplySunPreset(LightObject& light);    // warm, strong, high in the sky
    static void ApplyMoonPreset(LightObject& light);   // pale blue, weak, opposite the sun

    const char* GetEditorName() const override { return GetName().empty() ? "Light" : GetName().c_str(); }
    const char* GetTypeName() const override { return "Light"; }
    DirectX::XMFLOAT3 GetSortPosition() const override { return position_; }

    LightKind GetKind() const { return kind_; }
    bool IsDirectional() const { return kind_ == LightKind::Directional; }
    void SetKind(LightKind kind);

    // Colour is what the picker shows (sRGB), like every colour in the editor; the GPU gets it as linear light.
    const DirectX::XMFLOAT3& GetColor() const { return color_; }
    void SetColor(const DirectX::XMFLOAT3& color);
    float GetIntensity() const { return intensity_; }
    void SetIntensity(float intensity);
    // Which time-of-day light this is (Weather > Time of day): the Sun or the Moon follow the clock (direction, colour and strength are
    // computed from it, the light's own are the noon values); None is an ordinary light that stays as it is.
    LightRole GetRole() const { return role_; }
    void SetRole(LightRole role) { role_ = role; }
    float GetRange() const { return range_; }   // point lights: distance at which the light has faded to nothing
    void SetRange(float range);
    const DirectX::XMFLOAT3& GetPosition() const { return position_; }

    // Directional lights: the direction the light TRAVELS (unit length), and the same thing as sun-in-the-sky angles.
    DirectX::XMFLOAT3 GetDirection() const;
    float GetAzimuth() const;     // degrees, 0..360
    float GetElevation() const;   // degrees above the horizon, -90..90
    void SetAngles(float azimuthDegrees, float elevationDegrees);

    bool GetEditorTransform(DirectX::XMFLOAT3& position, DirectX::XMFLOAT3& rotation, DirectX::XMFLOAT3& scale) const override;
    bool SetEditorTransform(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation, const DirectX::XMFLOAT3& scale) override;
    bool SupportsRotation() const override { return IsDirectional(); }
    bool SupportsScale() const override { return false; }
    bool GetOrientedBounds(DirectX::XMFLOAT4X4& world, DirectX::XMFLOAT3& boundsMin, DirectX::XMFLOAT3& boundsMax) const override;
    bool IsUserContent() const override { return true; }   // deletable and duplicable; "snap to terrain" drops a lamp onto the ground
    bool CanClone() const noexcept override { return true; }
    std::unique_ptr<SceneObject> Clone(ID3D11Device* device) const override;

    // A light draws nothing itself: the editor shows a marker for it, and the scene reads its numbers.
    void Update(float) override {}
    void Render(ID3D11DeviceContext*, const RenderView&) const override {}

    static constexpr float kIntensityMax = 20.0f;
    static constexpr float kRangeMin = 0.5f, kRangeMax = 500.0f;

private:
    LightKind kind_;
    DirectX::XMFLOAT3 color_ = { 1.0f, 0.85f, 0.6f };
    float intensity_ = 2.0f;
    float range_ = 12.0f;
    DirectX::XMFLOAT3 position_ = { 0.0f, 4.0f, 0.0f };
    DirectX::XMFLOAT3 rotation_ = { 0.0f, 0.0f, 0.0f };   // radians; only a directional light uses it
    LightRole role_ = LightRole::None;
};
