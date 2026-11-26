#pragma once
// The inverse FFT of the GPU sea, as the compute shader does it, in C++ (roadmap O2, the GPU half). Pure: no Direct3D.
// Tests: tests/ocean_gpu_fft_test.cpp. Shader: shaders/ocean_fft_cs.hlsl. Same arrangement as OceanGpuPack.h: the shader is a line-by-line copy of
// this, and this is tested against OceanFFT (double precision, the reference). Change one, change the others.
//
// One DISPATCH is one pass over the whole texture: n thread groups, one per LINE (a row, or a column when `vertical`). A group loads its line into
// groupshared memory in BIT-REVERSED order, then does log2(n) stages of n/2 butterflies each, and writes the line back. The 2-D FFT is two
// passes: rows first, then columns. Every element is a float4 = TWO complex numbers (see OceanGpuPack.h), so one butterfly does two FFTs at once.
//
// Nothing is scaled: the spectrum already carries the sea's variance (the same as OceanFFT::Fft2D(data, n, true)).
#include "OceanGpuPack.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace OceanGpuFft {

using OceanGpuPack::Float4;

constexpr int kThreads = 128;   // threads in a group (numthreads in the shader); each handles several elements when the line is longer
constexpr int kMaxN = 512;      // the size of the groupshared array in the shader

// The constant buffer of one FFT pass (b0 in ocean_fft_cs.hlsl).
struct FftConstants {
    std::uint32_t n = 0;          // samples per line (a power of two, at most kMaxN)
    std::uint32_t log2n = 0;
    std::uint32_t vertical = 0;   // 0: the lines are rows (x runs along the line), 1: the lines are columns
    std::uint32_t padding = 0;
};
static_assert(sizeof(FftConstants) == 16, "a constant buffer must be a multiple of 16 bytes");

inline bool IsPowerOfTwo(int n) { return n > 0 && (n & (n - 1)) == 0; }
inline int Log2(int n) { int r = 0; while ((1 << r) < n) ++r; return r; }

// Reverses the lowest `log2n` bits of `i` (the shader: reversebits(i) >> (32 - log2n)).
inline std::uint32_t BitReverse(std::uint32_t i, int log2n)
{
    std::uint32_t r = 0;
    for (int b = 0; b < 32; ++b) r |= ((i >> b) & 1u) << (31 - b);   // reversebits
    return log2n == 0 ? 0u : r >> (32 - log2n);
}

// Butterfly number `b` (0 .. n/2 - 1) of the stage whose half-block length is `halfLen` (1, 2, 4 ... n/2): which two elements it combines, and k,
// the position in the block, which sets the twiddle factor e^{+i 2 pi k / (2 halfLen)}. Every element is in exactly one butterfly per stage, which
// is what lets the threads of a group do them all at once with no conflict (tested).
inline void Butterfly(int b, int halfLen, int& i0, int& i1, int& k)
{
    const int block = b / halfLen;
    k = b % halfLen;
    i0 = block * halfLen * 2 + k;
    i1 = i0 + halfLen;
}

// One thread group: the FFT of one line held in `shared` (n elements already in line order). The threads are acted out one after the other; the
// barriers of the shader fall between the loops.
inline void FftGroup(std::vector<Float4>& line, int n)
{
    const int log2n = Log2(n);
    std::vector<Float4> shared(static_cast<size_t>(n));
    for (int tid = 0; tid < kThreads; ++tid)                                       // load, bit-reversed
        for (int i = tid; i < n; i += kThreads) shared[BitReverse(static_cast<std::uint32_t>(i), log2n)] = line[static_cast<size_t>(i)];
    // (barrier)
    for (int halfLen = 1; halfLen < n; halfLen <<= 1) {
        const int span = halfLen * 2;
        for (int tid = 0; tid < kThreads; ++tid) {
            for (int b = tid; b < n / 2; b += kThreads) {
                int i0, i1, k;
                Butterfly(b, halfLen, i0, i1, k);
                const float angle = 6.2831853f * static_cast<float>(k) / static_cast<float>(span);   // + : the INVERSE transform
                const float c = std::cos(angle), s = std::sin(angle);
                const Float4 u = shared[static_cast<size_t>(i0)], w = shared[static_cast<size_t>(i1)];
                // v = w * (c + i s) for both complex numbers of the float4
                const Float4 v{ w.x * c - w.y * s, w.x * s + w.y * c, w.z * c - w.w * s, w.z * s + w.w * c };
                shared[static_cast<size_t>(i0)] = Float4{ u.x + v.x, u.y + v.y, u.z + v.z, u.w + v.w };
                shared[static_cast<size_t>(i1)] = Float4{ u.x - v.x, u.y - v.y, u.z - v.z, u.w - v.w };
            }
        }
        // (barrier)
    }
    line = shared;
}

// One dispatch: every row of `src` (or every column when `vertical`) transformed, written to `dst`. src and dst are n x n, index j * n + i.
inline void Pass(const std::vector<Float4>& src, std::vector<Float4>& dst, int n, bool vertical)
{
    dst.assign(src.size(), Float4{});
    std::vector<Float4> line(static_cast<size_t>(n));
    for (int lineIndex = 0; lineIndex < n; ++lineIndex) {   // one thread group each
        for (int i = 0; i < n; ++i) line[static_cast<size_t>(i)] = src[vertical ? static_cast<size_t>(i) * n + lineIndex : static_cast<size_t>(lineIndex) * n + i];
        FftGroup(line, n);
        for (int i = 0; i < n; ++i) dst[vertical ? static_cast<size_t>(i) * n + lineIndex : static_cast<size_t>(lineIndex) * n + i] = line[static_cast<size_t>(i)];
    }
}

// The whole 2-D inverse FFT of an n x n texture of float4: rows, then columns (the same two dispatches the GPU makes).
inline std::vector<Float4> Transform2D(const std::vector<Float4>& texture, int n)
{
    std::vector<Float4> rows, result;
    Pass(texture, rows, n, false);
    Pass(rows, result, n, true);
    return result;
}

}   // namespace OceanGpuFft
