#include "Material.h"

Material::~Material()
{
	if (rasterizerState_) rasterizerState_->Release();
	if (depthState_) depthState_->Release();
	if (blendState_) blendState_->Release();
}

bool Material::CreateBlendState(const D3D11_BLEND_DESC& desc)
{
	if (!device_) return false;
	if (blendState_) { blendState_->Release(); blendState_ = nullptr; }
	return SUCCEEDED(device_->CreateBlendState(&desc, &blendState_));
}

bool Material::CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC& desc)
{
	if (!device_) return false;
	if (depthState_) { depthState_->Release(); depthState_ = nullptr; }
	return SUCCEEDED(device_->CreateDepthStencilState(&desc, &depthState_));
}

bool Material::CreateRasterizerState(const D3D11_RASTERIZER_DESC& desc)
{
	if (!device_) return false;
	if (rasterizerState_) { rasterizerState_->Release(); rasterizerState_ = nullptr; }
	return SUCCEEDED(device_->CreateRasterizerState(&desc, &rasterizerState_));
}

void Material::Apply(ID3D11DeviceContext* context) const
{
	if (!context) return;
	const float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	context->OMSetBlendState(blendState_, blendFactor, 0xffffffff);
	context->OMSetDepthStencilState(depthState_, 0);
	context->RSSetState(rasterizerState_);
}
