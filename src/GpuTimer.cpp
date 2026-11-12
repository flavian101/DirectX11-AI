#include "GpuTimer.h"

bool GpuTimer::Initialize(ID3D11Device* device)
{
    Release();
    device_ = device;
    if (!device_) return false;
    D3D11_QUERY_DESC disjointDesc{};
    disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    D3D11_QUERY_DESC stampDesc{};
    stampDesc.Query = D3D11_QUERY_TIMESTAMP;
    for (Frame& frame : frames_) {
        if (FAILED(device_->CreateQuery(&disjointDesc, &frame.disjoint))) { Release(); return false; }
        for (ID3D11Query*& stamp : frame.stamp) {
            if (FAILED(device_->CreateQuery(&stampDesc, &stamp))) { Release(); return false; }
        }
    }
    return true;
}

void GpuTimer::Release()
{
    for (Frame& frame : frames_) {
        if (frame.disjoint) frame.disjoint->Release();
        for (ID3D11Query* stamp : frame.stamp) if (stamp) stamp->Release();
        frame = Frame{};
    }
    current_ = nullptr;
    timings_ = GpuTimings{};
    device_ = nullptr;
}

void GpuTimer::BeginFrame(ID3D11DeviceContext* ctx)
{
    current_ = nullptr;
    if (!device_ || !ctx) return;
    Frame& frame = frames_[writeIndex_];
    if (frame.pending) return;   // the GPU is more than a ring behind: skip this frame's measurement rather than stall
    for (bool& m : frame.marked) m = false;
    ctx->Begin(frame.disjoint);
    current_ = &frame;
}

void GpuTimer::Mark(ID3D11DeviceContext* ctx, GpuPass pass)
{
    if (!current_ || !ctx) return;
    const int index = static_cast<int>(pass);
    ctx->End(current_->stamp[index]);
    current_->marked[index] = true;
}

void GpuTimer::EndFrame(ID3D11DeviceContext* ctx)
{
    if (current_ && ctx) {
        ctx->End(current_->stamp[kStamps]);   // the very end of the frame
        current_->marked[kStamps] = true;
        ctx->End(current_->disjoint);
        current_->pending = true;
        writeIndex_ = (writeIndex_ + 1) % kRing;
    }
    current_ = nullptr;
    // Read whichever finished frames are ready: the OLDEST first, never waiting for one that is not.
    for (int i = 0; i < kRing; ++i) {
        Frame& frame = frames_[(writeIndex_ + i) % kRing];
        if (frame.pending) Collect(ctx, frame);
    }
}

void GpuTimer::Collect(ID3D11DeviceContext* ctx, Frame& frame)
{
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
    if (ctx->GetData(frame.disjoint, &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) return;   // not ready yet
    frame.pending = false;
    if (disjoint.Disjoint || disjoint.Frequency == 0) return;   // the clock changed during the frame (power state): throw it away

    UINT64 stamps[kStamps + 1] = {};
    for (int i = 0; i <= kStamps; ++i) {
        if (!frame.marked[i]) continue;
        if (ctx->GetData(frame.stamp[i], &stamps[i], sizeof(UINT64), 0) != S_OK) return;
    }
    const double toMs = 1000.0 / static_cast<double>(disjoint.Frequency);

    // A pass runs from its own stamp to the NEXT stamp that was marked (passes that did not run have none, so cost 0).
    constexpr int passCount = static_cast<int>(GpuPass::Total);
    constexpr float kSmoothing = 0.05f;   // each new frame moves the average 5% of the way
    for (int p = 0; p < passCount; ++p) {
        float ms = 0.0f;
        if (frame.marked[p]) {
            int next = p + 1;
            while (next < passCount && !frame.marked[next]) ++next;
            const UINT64 end = next < passCount ? stamps[next] : stamps[kStamps];
            ms = static_cast<float>(static_cast<double>(end - stamps[p]) * toMs);
        }
        timings_.ms[p] = timings_.valid ? timings_.ms[p] + (ms - timings_.ms[p]) * kSmoothing : ms;
    }
    // Total = first mark to the end.
    int first = 0;
    while (first < passCount && !frame.marked[first]) ++first;
    if (first < passCount) {
        const float total = static_cast<float>(static_cast<double>(stamps[kStamps] - stamps[first]) * toMs);
        float& slot = timings_.ms[static_cast<int>(GpuPass::Total)];
        slot = timings_.valid ? slot + (total - slot) * kSmoothing : total;
        timings_.valid = true;
    }
}
