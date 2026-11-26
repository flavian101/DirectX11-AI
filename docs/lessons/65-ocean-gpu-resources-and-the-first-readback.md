# Lesson 65 - Ocean GPU resources and the first readback (O2, GPU half: step G3)

## Understand

**A compute pass needs a home for its data.** The shader from lesson 64 reads a spectrum and writes two textures. Before it can run, someone has to create those things on the GPU, bind them, run the pass and clean up. `OceanGpu` is that someone, one entry per band.

**Three kinds of GPU data, three ways to make them.**
- Data made once and never changed (the modes, omega): an IMMUTABLE structured buffer.
- Data changed every frame (the time): a small DEFAULT constant buffer updated with `UpdateSubresource`.
- Data the GPU makes and then reads (the packed spectra): a texture with both an SRV and a UAV.

**Why structured buffers for the modes.** The shader indexes them like an array (`oceanModes[index]`). A texture would add 2-D addressing and filtering that nothing here wants. A "structured" buffer is just records of a fixed size, so the CPU array goes in as it is.

**Why RGBA32F for the outputs.** One float4 holds two complex numbers, and typed UAV *stores* to `R32G32B32A32_FLOAT` are part of Direct3D 11. Typed *loads* are not, which is why the FFT will read a texture through an SRV and write another one: no pass ever loads from a UAV.

## How it works

**`Create`** builds each band's spectrum on the CPU (`SeaSpectrum::Make`), packs it (`OceanGpuPack::Pack`) and uploads it. If anything fails, everything made so far is released, so a failed `Create` never leaves half a sea behind.

**`Evolve`** per band: write the constants, bind inputs (t0, t1), outputs (u0, u1) and the constant buffer (b0), dispatch, then unbind. The number of groups is `GroupCount(n, 8)` in both directions, rounded up. Rounding up is why the shader begins with a bounds check.

**Why unbind after the dispatch.** The next pass will read these textures as SRVs. A texture that is still bound as a UAV cannot be bound as an SRV, so Direct3D would bind nothing there and the FFT would read zeros without any error.

**Reading back.** The CPU cannot map a GPU texture directly. `ReadTexture` copies it into a STAGING texture and maps that. `Map` waits for the GPU to finish, which makes it slow: a check, never something for every frame. Rows in a mapped texture are `RowPitch` bytes apart, which can be more than the row's data, so the copy goes row by row.

**`SelfCheck`** runs once when the renderer starts. It runs the pass at a fixed time and compares every number with `OceanGpuPack::EvolvePacked`. Three outcomes: `ok`, `MISMATCH` (the shader or a resource is wrong), or an error line from the logger (something could not be created). A NaN from the GPU counts as the largest possible error instead of disappearing from the maximum.

## What is checked, and what is not

**Checked on this machine:** the code compiles against stand-in headers (typos, scopes, types). The maths it compares with is covered by lesson 64's tests.

**Not checked:** nothing has run on a GPU yet. The log lines at start-up are the first real test, and they decide whether the next step (the FFT) can build on this one.

## Check yourself

1. `SelfCheck` allows a difference of 1e-3 of the biggest value. Why can the GPU's answer differ from the CPU's at all, and what would a difference of 0.5 tell you?
2. If the check printed a `MISMATCH` only for band 2 (the ripples), which differences between the bands would you suspect first?
3. Why is `ReadTexture` allowed to wait for the GPU here, but would be a bad idea in the water's draw call?
