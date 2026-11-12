#pragma once
// Measures how long the GPU spends on each part of a frame, with Direct3D 11 timestamp queries.
//
// A CPU stopwatch around a draw call measures nothing useful: the call only QUEUES work, and the GPU runs it later.
// A timestamp query is written INTO the command stream, so it records the moment the GPU itself reaches that point.
// The answer is not ready until the GPU has finished the frame, so reading it straight away would stall the CPU.
// Instead there is a small ring of frames: this frame's queries are issued now and read a few frames later.
#include <d3d11.h>

enum class GpuPass : int { Shadows = 0, Reflections, Opaque, Water, PostProcess, Total, Count };

struct GpuTimings {
    // Smoothed milliseconds per pass (a slow average, so the numbers can be read while they change).
    float ms[static_cast<int>(GpuPass::Count)] = {};
    bool valid = false;   // false until the first result arrives, or when the GPU does not support timestamps
};

class GpuTimer {
public:
    bool Initialize(ID3D11Device* device);
    void Release();

    void BeginFrame(ID3D11DeviceContext* ctx);
    // Marks the START of a pass. The previous pass ends at the same instant, so passes must be marked in drawing order.
    void Mark(ID3D11DeviceContext* ctx, GpuPass pass);
    void EndFrame(ID3D11DeviceContext* ctx);

    const GpuTimings& GetTimings() const { return timings_; }

private:
    static constexpr int kRing = 4;                                  // frames in flight
    static constexpr int kStamps = static_cast<int>(GpuPass::Count);   // one start stamp per pass, plus the final one

    struct Frame {
        ID3D11Query* disjoint = nullptr;
        ID3D11Query* stamp[kStamps + 1] = {};
        bool marked[kStamps + 1] = {};
        bool pending = false;
    };

    void Collect(ID3D11DeviceContext* ctx, Frame& frame);

    ID3D11Device* device_ = nullptr;
    Frame frames_[kRing];
    int writeIndex_ = 0;
    Frame* current_ = nullptr;
    GpuTimings timings_;
};
