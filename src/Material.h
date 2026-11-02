#pragma once

#include <d3d11.h>

// Owns the fixed-function D3D11 state used by a drawable material.
class Material {
public:
	explicit Material(ID3D11Device* device) : device_(device) {}
	~Material();

	Material(const Material&) = delete;
	Material& operator=(const Material&) = delete;

	bool CreateBlendState(const D3D11_BLEND_DESC& desc);
	bool CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC& desc);
	bool CreateRasterizerState(const D3D11_RASTERIZER_DESC& desc);

	void Apply(ID3D11DeviceContext* context) const;
	bool IsValid() const { return device_ != nullptr; }
	bool HasBlendState() const { return blendState_ != nullptr; }
	bool HasDepthStencilState() const { return depthState_ != nullptr; }
	bool HasRasterizerState() const { return rasterizerState_ != nullptr; }

private:
	ID3D11Device* device_ = nullptr; // non-owning
	ID3D11BlendState* blendState_ = nullptr;
	ID3D11DepthStencilState* depthState_ = nullptr;
	ID3D11RasterizerState* rasterizerState_ = nullptr;
};
