# Lesson 64 - The first ocean compute pass (O2, GPU half: steps G1 and G2)

## Understand

**Why the sea moves to the GPU.** The CPU can already make the sea at a given time (`OceanFFT::Evolve`), but a band is up to 256 x 256 modes and 8 FFTs of that size, every frame, times three bands. The GPU does the same sum of waves in parallel, one thread per mode. The CPU copy stays: boats read it, and it is the answer the GPU is checked against.

**What is a compute shader?** A shader with no vertices and no pixels. You tell the GPU "run this many groups", each group is a block of threads (`[numthreads(8, 8, 1)]` means 64), and each thread learns who it is from `SV_DispatchThreadID`. It reads inputs (SRVs, `t` registers) and writes outputs (UAVs, `u` registers).

**Which shader model?** Direct3D 11 only runs the DXBC byte code that `D3DCompile` makes, so the engine compiles `cs_5_0`. Shader model 6 (DXIL) is Direct3D 12 only. DXC at `cs_6_0` is still the CHECK in the roadmap, because a `cs_5_0` shader is also valid `cs_6_0`.

## How it works

**`ComputeShader`.** Load a file, compile it, `Bind`, `Dispatch`. `GroupCount` rounds up (100 items in groups of 8 need 13 groups), so the shader starts with a bounds check and the extra threads do nothing.

**Why `UnbindCompute` matters.** A texture cannot be an input and an output at the same time. If a pass leaves its UAV bound, the next pass that tries to read the texture through an SRV gets nothing, silently. So a pass ends by unbinding what it used.

**What the evolve pass does.** For each mode it computes `h0(k) e^{-iwt} + conj(h0(-k)) e^{+iwt}`, the wave at time t, and then the height, the horizontal "choppy" displacement, the slopes and the three derivatives the Jacobian needs. That is 8 real results.

**The packing trick.** Each of the 8 spectra is conjugate-symmetric, so its inverse FFT is real. For two such spectra, `IFFT(A + iB) = a + ib`. One complex FFT therefore carries two real results, 8 FFTs become 4, and a float4 holds two complex numbers, so the 4 live in 2 textures.

**Why `h0(k)` and `conj(h0(-k))` are stored together.** The shader needs both for every mode. Pairing them on the CPU gives one read per mode instead of two, and the shader never has to find the opposite mode.

**Why the output is RGBA32F.** Typed UAV stores to `R32G32B32A32_FLOAT` are part of Direct3D 11; typed loads from it are not. The FFT passes will read these textures through an SRV and write to another texture, so no pass ever loads from a UAV.

## What the tests show

**It is the same sea.** `EvolvePacked` (the C++ copy of the shader) followed by the FFT gives the height, displacement, slopes and Jacobian of `OceanFFT::Evolve` to about 1e-7 of the wave size, in all three bands, in deep and shallow water, with and without choppiness.

**The test can fail.** Flipping the sign of the displacement, dropping a conjugate, swapping two Jacobian terms or packing the opposite mode without its conjugate each makes it fail. That is how we know the passing run means something.

**No NaN.** Every output is finite, and the zero wave vector (where the shader would divide by |k| = 0) is exactly zero.

**Not done yet.** Nothing runs on a GPU yet. The next steps are the D3D11 buffers and textures for this pass, then the FFT shader (rows, then columns), then a readback that compares the GPU's numbers with `OceanFFT::Evolve`.

## Check yourself

1. `Evolve()` adds a second term, `conj(h0(-k)) e^{+iwt}`, to every mode. What does that term guarantee about the sea after the FFT, and what would you see on screen without it?
2. The shader checks `id.x >= oceanN` and returns. When is that check needed, and when does it do nothing?
3. A pass writes a texture through a UAV, and the next pass samples the same texture in a pixel shader without unbinding anything. What goes wrong, and how would the debug layer tell you?
