#pragma once
#include "ComputeShader.h"
#include "OceanGpuAssemble.h"
#include "OceanGpuFft.h"
#include "OceanGpuPack.h"
#include "SeaSpectrum.h"
#include <d3d11.h>
#include <vector>

// The FFT sea on the GPU (roadmap O2, the GPU half). Per band it owns what the passes read and write, and runs them:
//   Evolve    (shaders/ocean_evolve_cs.hlsl)    spectrum at time t -> 4 packed complex spectra in two textures A and B
//   Transform (shaders/ocean_fft_cs.hlsl)       inverse FFT of those, rows then columns -> the sea in space, in the same two textures
//   Assemble  (shaders/ocean_assemble_cs.hlsl)  A and B -> the two maps the water reads, with a mip chain each
// The water's shaders come in the next step. Nothing here is drawn yet, so nothing in a scene changes.
//
// Per band, on the GPU:
//     modes   (StructuredBuffer<float4>, t0)  h0(k) and conj(h0(-k)), made once                 \  OceanGpuPack::Pack
//     omega   (StructuredBuffer<float>,  t1)  radians per second of each mode, made once        /
//     constants (b0)                          modes per side, delta k, time: 16 bytes, written every frame
//     A, B    (RGBA32F textures)              after Evolve: the packed spectra. After Transform: the sea in space, per texel
//                 A = (height, displacement x, displacement z, slope x)     B = (slope z, dXdx, dZdz, dXdz)   (dXdx etc. build the Jacobian)
//     work A, B                               the rows-done image between the FFT's two passes (a pass never reads and writes the same texture)
//     displacement map (RGBA16F, mips)        (choppiness * disp x, height, choppiness * disp z, 0)   what the vertex shader reads
//     slope map        (RGBA16F, mips)        (slope x, slope z, Jacobian, 0)                         what the pixel shader reads
class OceanGpu {
public:
    OceanGpu() = default;
    ~OceanGpu();
    OceanGpu(const OceanGpu&) = delete;
    OceanGpu& operator=(const OceanGpu&) = delete;

    // Builds every band's spectrum from `settings` (SeaSpectrum::DefaultBands) and uploads it. False if a shader or any resource failed, or a band is
    // not a power of two the FFT shader supports: whatever was made is released, so a failed Create leaves nothing half-built.
    bool Create(ID3D11Device* device, const SeaSpectrum::Settings& settings);
    bool IsValid() const { return evolveShader_.IsValid() && fftShader_.IsValid() && assembleShader_.IsValid() && fftConstants_ && assembleConstants_ && !bands_.empty(); }
    int BandCount() const { return static_cast<int>(bands_.size()); }
    int Resolution(int band) const { return bands_[static_cast<size_t>(band)].n; }
    float PatchSize(int band) const { return bands_[static_cast<size_t>(band)].patchSize; }   // metres: the maps repeat every PatchSize

    // Evolve, Transform and Assemble for every band at `time` (seconds): the sea at that moment, in the displacement and slope maps.
    // `choppiness` (0 .. 1) is how far the water is pushed sideways toward the crests.
    void Simulate(ID3D11DeviceContext* ctx, float time, float choppiness) { Evolve(ctx, time); Transform(ctx); Assemble(ctx, choppiness); }
    // Only the first pass: A and B hold the packed spectra (what the FFT will read).
    void Evolve(ID3D11DeviceContext* ctx, float time);
    // Only the second: the inverse FFT of A and B, in place (through the work textures). Leaves nothing bound on the compute stage.
    void Transform(ID3D11DeviceContext* ctx);
    // Only the third: A and B (after Transform) -> the two maps, then their mip chains. Leaves nothing bound on the compute stage.
    void Assemble(ID3D11DeviceContext* ctx, float choppiness);

    // A and B of a band, for the next pass to read.
    ID3D11ShaderResourceView* SpectrumA(int band) const { return bands_[static_cast<size_t>(band)].srvA; }
    ID3D11ShaderResourceView* SpectrumB(int band) const { return bands_[static_cast<size_t>(band)].srvB; }
    // The maps the water samples (all mips), repeating every PatchSize(band) metres.
    ID3D11ShaderResourceView* Displacement(int band) const { return bands_[static_cast<size_t>(band)].srvDisplacement; }
    ID3D11ShaderResourceView* Slope(int band) const { return bands_[static_cast<size_t>(band)].srvSlope; }

    // Copies a band's A and B to the CPU. SLOW: it makes the CPU wait for the GPU. For checks, never every frame.
    bool ReadSpectra(ID3D11DeviceContext* ctx, int band, OceanGpuPack::PackedSpectra& out);
    // The same for the displacement and slope maps (mip 0, converted from half floats). SLOW, for checks.
    bool ReadMaps(ID3D11DeviceContext* ctx, int band, std::vector<OceanGpuPack::Float4>& displacement, std::vector<OceanGpuPack::Float4>& slope);

    // Runs the passes once at a fixed time and compares the GPU's numbers with the CPU: after Evolve with OceanGpuPack::EvolvePacked (the C++ copy of
    // the shader), after Transform and after Assemble with OceanFFT::Evolve (the double-precision reference). Logs the error per band.
    // True when every band agrees.
    // Called once when the renderer starts.
    static bool SelfCheck(ID3D11Device* device, ID3D11DeviceContext* ctx);

private:
    struct Band {
        int n = 0;
        float deltaK = 0.0f;
        float patchSize = 0.0f;
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
        ID3D11Texture2D* textureDisplacement = nullptr;
        ID3D11Texture2D* textureSlope = nullptr;
        ID3D11ShaderResourceView* srvDisplacement = nullptr;
        ID3D11ShaderResourceView* srvSlope = nullptr;
        ID3D11UnorderedAccessView* uavDisplacement = nullptr;
        ID3D11UnorderedAccessView* uavSlope = nullptr;
        void Release();
    };
    bool CreateBand(const OceanGpuPack::PackedBand& packed, Band& band);
    // One dispatch of the FFT shader: every row (or column) of `source` transformed into `target`.
    void RunFftPass(ID3D11DeviceContext* ctx, const Band& band, ID3D11ShaderResourceView* source, ID3D11UnorderedAccessView* target, bool vertical);
    // Mip 0 of a texture as raw bytes, n rows of n texels of `bytesPerTexel` each (a staging copy: waits for the GPU).
    bool ReadTextureBytes(ID3D11DeviceContext* ctx, ID3D11Texture2D* texture, int n, size_t bytesPerTexel, std::vector<unsigned char>& out);
    bool ReadTexture(ID3D11DeviceContext* ctx, ID3D11Texture2D* texture, int n, std::vector<OceanGpuPack::Float4>& out);
    void Release();

    ID3D11Device* device_ = nullptr;   // borrowed: the renderer owns the device and outlives this
    ComputeShader evolveShader_;
    ComputeShader fftShader_;
    ComputeShader assembleShader_;
    ID3D11Buffer* fftConstants_ = nullptr;   // 16 bytes, rewritten before every FFT pass (n, log2 n, rows or columns)
    ID3D11Buffer* assembleConstants_ = nullptr;   // 16 bytes (n, choppiness), rewritten before every Assemble dispatch
    std::vector<Band> bands_;
};
