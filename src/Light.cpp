#include "Light.h"
#include "Logger.h"
#include "Color.h"
#include "ShaderSlots.h"
#include <algorithm>

using namespace DirectX;

bool LightSystem::Initialize(ID3D11Device* device)
{
    if (!device) {
        LOG_ERROR("LightSystem requires a valid device");
        return false;
    }
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(CBLights);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    const HRESULT hr = device->CreateBuffer(&desc, nullptr, &cb_);
    if (FAILED(hr)) {
        LOG_HR(hr, "Create light constant buffer failed");
        return false;
    }
    return true;
}

void LightSystem::Release()
{
    if (cb_) { cb_->Release(); cb_ = nullptr; }
}

bool LightSystem::AddPointLight(const PointLight& light)
{
    if (pointLights_.size() >= kMaxPointLights) {
        LOG_ERROR("LightSystem: point light limit reached");
        return false;
    }
    pointLights_.push_back(light);
    return true;
}

void LightSystem::Apply(ID3D11DeviceContext* ctx, const XMFLOAT3& cameraPos) const
{
    if (!ctx || !cb_) return;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT hr = ctx->Map(cb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        LOG_HR(hr, "Map light constant buffer failed");
        return;
    }

    auto* data = static_cast<CBLights*>(mapped.pData);
    *data = {};

    // Colours are stored as sRGB (what the picker shows) and uploaded as LINEAR light.
    const XMFLOAT3 ambient = SrgbToLinear(ambient_);
    data->ambientColor = XMFLOAT4(ambient.x, ambient.y, ambient.z, 0.0f);

    const XMVECTOR dir = XMVector3Normalize(XMLoadFloat3(&directional_.direction));
    XMStoreFloat4(&data->dirDirection, dir);
    data->dirDirection.w = directional_.intensity;
    const XMFLOAT3 sunColor = SrgbToLinear(directional_.color);
    data->dirColor = XMFLOAT4(sunColor.x, sunColor.y, sunColor.z, 0.0f);

    const size_t count = (std::min)(pointLights_.size(), kMaxPointLights);
    for (size_t i = 0; i < count; ++i) {
        const PointLight& p = pointLights_[i];
        data->pointPosRange[i] = XMFLOAT4(p.position.x, p.position.y, p.position.z, p.range);
        const XMFLOAT3 color = SrgbToLinear(p.color);
        data->pointColorIntensity[i] = XMFLOAT4(color.x, color.y, color.z, p.intensity);
    }

    data->cameraPosNumLights = XMFLOAT4(cameraPos.x, cameraPos.y, cameraPos.z,
                                        static_cast<float>(count));

    ctx->Unmap(cb_, 0);
    ctx->PSSetConstantBuffers(Slots::kCbLights, 1, &cb_);
}
