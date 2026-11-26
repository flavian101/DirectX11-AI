#pragma once
#include "ComputeShader.h"
#include "OceanGpuPack.h"
#include "SeaSpectrum.h"
#include <d3d11.h>
#include <vector>

// The FFT sea on the GPU (roadmap O2, the GPU half). This step: one band per entry, each with the buffers the evolve pass reads and the textures it
// writes, and the pass itself (shaders/ocean_evolve_cs.hlsl). The FFT, the assemble pass and the water shaders come in the next steps.
//
// Per band, on the GPU:
//     modes   (StructuredBuffer<float4>, t0)  h0(k) and conj(h0(-k)), made once                 \  OceanGpuPack::Pack
//     omega   (StructuredBuffer<float>,  t1)  radians per second of each mode, made once        /
//     constants (b0)                          modes per side, delta k, time: 16 bytes, written every frame
//     spectrum A, B (RGBA32F textures, u0/u1) the 4 packed complex spectra of time t: what the FFT will read
// Nothing here is drawn yet, so nothing in a scene changes.
class OceanGpu {
public:
    OceanGpu() = default;
    ~OceanGpu();
    OceanGpu(const OceanGpu&) = delete;
    OceanGpu& operator=(const OceanGpu&) = delete;

    // Builds every band's spectrum from `settings` (SeaSpectrum::DefaultBands) and uploads it. False if the shader or any resource failed:
    // whatever was made is released, so a failed Create leaves nothing half-built.
    bool Create(ID3D11Device* device, const SeaSpectrum::Settings& settings);
    bool IsValid() const { return evolveShader_.IsValid() && !bands_.empty(); }
    int BandCount() const { return static_cast<int>(bands_.size()); }
    int Resolution(int band) const { return bands_[static_cast<size_t>(band)].n; }

    // Runs the evolve pass for every band at `time` (seconds). Leaves nothing bound on the compute stage.
    void Evolve(ID3D11DeviceContext* ctx, float time);

    // What the next pass (the FFT) reads: the two packed spectrum textures of a band.
    ID3D11ShaderResourceView* SpectrumA(int band) const { return bands_[static_cast<size_t>(band)].srvA; }
    ID3D11ShaderResourceView* SpectrumB(int band) const { return bands_[static_cast<size_t>(band)].srvB; }

    // Copies a band's two spectrum textures to the CPU. SLOW: it makes the CPU wait for the GPU. For checks, never every frame.
    bool ReadSpectra(ID3D11DeviceContext* ctx, int band, OceanGpuPack::PackedSpectra& out);

    // Runs the evolve pass once at a fixed time and compares the GPU's numbers with OceanGpuPack::EvolvePacked (the C++ copy of the shader),
    // logging the error per band. True when every band agrees. Called once when the renderer starts.
    static bool SelfCheck(ID3D11Device* device, ID3D11DeviceContext* ctx);

private:
    struct Band {
        int n = 0;
        float deltaK = 0.0f;
        ID3D11Buffer* modes = nullptr;
        ID3D11Buffer* omega = nullptr;
        ID3D11Buffer* constants = nullptr;
        ID3D11ShaderResourceView* modesView = nullptr;
        ID3D11ShaderResourceView* omegaView = nullptr;
        ID3D11Texture2D* textureA = nullptr;
        ID3D11Texture2D* textureB = nullptr;
        ID3D11ShaderResourceView* srvA = nullptr;
        ID3D11ShaderResourceView* srvB = nullptr;
        ID3D11UnorderedAccessView* uavA = nullptr;
        ID3D11UnorderedAccessView* uavB = nullptr;
        void Release();
    };
    bool CreateBand(const OceanGpuPack::PackedBand& packed, Band& band);
    bool ReadTexture(ID3D11DeviceContext* ctx, ID3D11Texture2D* texture, int n, std::vector<OceanGpuPack::Float4>& out);
    void Release();

    ID3D11Device* device_ = nullptr;   // borrowed: the renderer owns the device and outlives this
    ComputeShader evolveShader_;
    std::vector<Band> bands_;
};
