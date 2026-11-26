#pragma once
#include "ComputeShader.h"
#include "OceanGpuFft.h"
#include "OceanGpuPack.h"
#include "SeaSpectrum.h"
#include <d3d11.h>
#include <vector>

// The FFT sea on the GPU (roadmap O2, the GPU half). Per band it owns what the passes read and write, and runs them:
//   Evolve    (shaders/ocean_evolve_cs.hlsl)  spectrum at time t -> 4 packed complex spectra in two textures A and B
//   Transform (shaders/ocean_fft_cs.hlsl)     inverse FFT of those, rows then columns -> the sea in space, in the same two textures
// The assemble pass and the water shaders come in the next steps. Nothing here is drawn yet, so nothing in a scene changes.
//
// Per band, on the GPU:
//     modes   (StructuredBuffer<float4>, t0)  h0(k) and conj(h0(-k)), made once                 \  OceanGpuPack::Pack
//     omega   (StructuredBuffer<float>,  t1)  radians per second of each mode, made once        /
//     constants (b0)                          modes per side, delta k, time: 16 bytes, written every frame
//     A, B    (RGBA32F textures)              after Evolve: the packed spectra. After Transform: the sea in space, per texel
//                 A = (height, displacement x, displacement z, slope x)     B = (slope z, dXdx, dZdz, dXdz)   (dXdx etc. build the Jacobian)
//     work A, B                               the rows-done image between the FFT's two passes (a pass never reads and writes the same texture)
class OceanGpu {
public:
    OceanGpu() = default;
    ~OceanGpu();
    OceanGpu(const OceanGpu&) = delete;
    OceanGpu& operator=(const OceanGpu&) = delete;

    // Builds every band's spectrum from `settings` (SeaSpectrum::DefaultBands) and uploads it. False if a shader or any resource failed, or a band is
    // not a power of two the FFT shader supports: whatever was made is released, so a failed Create leaves nothing half-built.
    bool Create(ID3D11Device* device, const SeaSpectrum::Settings& settings);
    bool IsValid() const { return evolveShader_.IsValid() && fftShader_.IsValid() && fftConstants_ && !bands_.empty(); }
    int BandCount() const { return static_cast<int>(bands_.size()); }
    int Resolution(int band) const { return bands_[static_cast<size_t>(band)].n; }

    // Evolve then Transform for every band at `time` (seconds): the sea at that moment, in A and B.
    void Simulate(ID3D11DeviceContext* ctx, float time) { Evolve(ctx, time); Transform(ctx); }
    // Only the first pass: A and B hold the packed spectra (what the FFT will read).
    void Evolve(ID3D11DeviceContext* ctx, float time);
    // Only the second: the inverse FFT of A and B, in place (through the work textures). Leaves nothing bound on the compute stage.
    void Transform(ID3D11DeviceContext* ctx);

    // A and B of a band, for the next pass to read.
    ID3D11ShaderResourceView* SpectrumA(int band) const { return bands_[static_cast<size_t>(band)].srvA; }
    ID3D11ShaderResourceView* SpectrumB(int band) const { return bands_[static_cast<size_t>(band)].srvB; }

    // Copies a band's A and B to the CPU. SLOW: it makes the CPU wait for the GPU. For checks, never every frame.
    bool ReadSpectra(ID3D11DeviceContext* ctx, int band, OceanGpuPack::PackedSpectra& out);

    // Runs the passes once at a fixed time and compares the GPU's numbers with the CPU: after Evolve with OceanGpuPack::EvolvePacked (the C++ copy of
    // the shader), after Transform with OceanFFT::Evolve (the double-precision reference). Logs the error per band. True when every band agrees.
    // Called once when the renderer starts.
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
        ID3D11Texture2D* textureWorkA = nullptr;
        ID3D11Texture2D* textureWorkB = nullptr;
        ID3D11ShaderResourceView* srvA = nullptr;
        ID3D11ShaderResourceView* srvB = nullptr;
        ID3D11ShaderResourceView* srvWorkA = nullptr;
        ID3D11ShaderResourceView* srvWorkB = nullptr;
        ID3D11UnorderedAccessView* uavA = nullptr;
        ID3D11UnorderedAccessView* uavB = nullptr;
        ID3D11UnorderedAccessView* uavWorkA = nullptr;
        ID3D11UnorderedAccessView* uavWorkB = nullptr;
        void Release();
    };
    bool CreateBand(const OceanGpuPack::PackedBand& packed, Band& band);
    // One dispatch of the FFT shader: every row (or column) of `source` transformed into `target`.
    void RunFftPass(ID3D11DeviceContext* ctx, const Band& band, ID3D11ShaderResourceView* source, ID3D11UnorderedAccessView* target, bool vertical);
    bool ReadTexture(ID3D11DeviceContext* ctx, ID3D11Texture2D* texture, int n, std::vector<OceanGpuPack::Float4>& out);
    void Release();

    ID3D11Device* device_ = nullptr;   // borrowed: the renderer owns the device and outlives this
    ComputeShader evolveShader_;
    ComputeShader fftShader_;
    ID3D11Buffer* fftConstants_ = nullptr;   // 16 bytes, rewritten before every FFT pass (n, log2 n, rows or columns)
    std::vector<Band> bands_;
};
