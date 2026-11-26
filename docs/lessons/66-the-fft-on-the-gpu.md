# Lesson 66 - The FFT on the GPU (O2, GPU half: step G4)

## Understand

**What the FFT does here.** The evolve pass leaves the sea as a spectrum: how much of each wave there is. The inverse FFT turns that into what you see: the height of the water at every point of the patch. It is the same sum of waves as before, made in n log n steps instead of n squared.

**A 2-D FFT is two 1-D FFTs.** Transform every row, then every column of the result. That is the whole trick, and on the GPU each of the two is one dispatch: one thread group per line.

**Why two textures of float4.** From lesson 64: every texel holds two complex numbers. One butterfly does the same arithmetic on both halves, so the GPU transforms two spectra at once for the price of one.

## How it works

**Bit-reversed load.** A radix-2 FFT wants its input shuffled: the element at index i goes where the bit pattern of i is reversed (index 1 of 8, 001, goes to 100 = 4). Do the shuffle once while loading into groupshared memory and every stage can then combine neighbours.

**Stages and butterflies.** log2(n) stages. Stage s works on blocks of 2 x halfLen elements; each butterfly takes two elements halfLen apart, u and w, multiplies w by a twiddle factor e^{+i 2 pi k / span} (plus because this is the INVERSE transform) and writes u + v and u - v. Every stage touches every element exactly once, which is why all the threads can work at the same time. The test checks that for every size.

**Groupshared memory and the barrier.** Groupshared memory is a small fast array that all the threads of one group share. A stage reads what the previous stage wrote, so every thread must finish stage s before anyone starts s + 1: `GroupMemoryBarrierWithGroupSync()` is that "everybody wait here". It sits outside the butterfly loop, where every thread reaches it.

**128 threads, any line length.** The group always has 128 threads. A 256-sample line therefore has two elements per thread to load and one butterfly each; a 512 line has four and two. Loops `for (i = tid; i < n; i += 128)` spread the work, so one shader handles the bands of 128 and 256 (and up to 512).

**Why work textures.** A pass cannot read and write the same texture. So rows go A -> work A, columns go work A -> A (and the same for B). Nothing is copied; the roles swap.

**Direct twiddles.** The CPU FFT builds the twiddle factors by repeated multiplication; the shader computes `sincos(2 pi k / span)` for each butterfly. That is more accurate in float, and costs nothing next to the memory traffic.

## What the tests show

**The twin is the FFT.** `OceanGpuFft` (the C++ copy of the shader, with the 128-thread loops acted out) equals `OceanFFT::Fft1D` for 2 to 512 samples, and rows then columns equal `Fft2D`. A single bright mode gives exactly one plane wave in the right direction.

**The whole chain is the sea.** Pack, evolve, FFT gives `OceanFFT::Evolve`'s height, displacement, slopes and Jacobian to 1e-4 of the wave size, in all three bands, deep and shallow. A flipped twiddle sign, a missing bit reversal, a wrong block stride, a thread loop that skips elements and columns treated as rows each make the test fail.

**On the GPU.** `SelfCheck` now ends with three more log lines, one per band: the GPU's finished sea against the double-precision CPU sea. That is the first real test of both shaders together.

## Check yourself

1. The barrier sits after the inner loop, not inside it. What would go wrong if one thread reached it and another skipped it? And what would go wrong with no barrier at all?
2. A band was 1024 x 1024. Which constants in the shader and in `OceanGpuFft.h` would have to change, and what is the real limit (look at groupshared size)?
3. The row pass reads A and writes work A; the column pass reads work A and writes A. Why not do both passes in place on A?
