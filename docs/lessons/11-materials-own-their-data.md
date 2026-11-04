# Lesson 11 - Materials own their data, and shared shader code

## Understand

**The generic buffer was rotting.** Every time a shader needed another setting, we added another anonymous `float4` to the per-object buffer: `materialExtra`, `materialColor2`, `materialExtra2`. Each one meant something different to each shader, and every object in the scene paid for slots it never used. The terrain is about to need a dozen more numbers, so this patch fixes the structure first. **No visible change.**

## Visualize

**A form versus a notebook.** `PerObject` should be a short form every object fills in the same way: world matrix, a colour, one tiling number. Anything specific to one kind of surface goes in that surface's own notebook, a constant block that only it reads.

## Why it is built this way

**A material block (`b4`).** `Renderable::SetMaterialBlock(data, size)` stores the bytes and uploads them. The *caller* defines a C++ struct, the *shader* declares the matching `cbuffer`, and `Renderable` knows nothing about either. The water now has `WaterMaterialCB` in `Water.cpp` and `WaterMaterial` in `water_material.hlsli`, field for field. The terrain will get its own next patch.

**The layout contract.** Constant buffers are read in 16-byte rows (`float4`). A `float3` followed by a `float` shares a row; a `float3` followed by a `float2` does not. That is why every field here is a `float4` and each struct has a `static_assert(size % 16 == 0)`. Mismatch the order and nothing crashes: values are just quietly wrong.

**Uploaded only when it changes.** Settings change when you drag a slider, not every frame. `Renderable` keeps a CPU copy and a `materialDirty_` flag, and only `Map`s the dynamic buffer when the flag is set. The method that checks it is `const`, so those members are `mutable`: they are a cache, not part of the object's meaning.

**`lighting.hlsli`: one copy of the lighting.** `ps.hlsl` used to contain the whole Blinn-Phong loop. The terrain shader needs the same lighting with a different surface colour, so it moved into `ComputeLighting(...)` in a shared header. Change the lighting once, every lit shader follows.

## Other ways, and the trade-offs

**Structured buffers.** Put material records in one big GPU array and pass an index per draw. It scales to thousands of materials and makes bindless-style rendering possible, at the cost of more machinery.

**Automatic layout from reflection.** Ask the compiled shader (`D3DReflect`, as in lesson 5) for the cbuffer's field names and offsets, and set values *by name*. No hand-matched structs; slower to build, much safer.

**One uber-shader with flags.** Fewer files, but every pixel pays for every feature unless the compiler strips branches. We keep separate shaders per material and share code through includes.

## Predict, then check

1. A `float3` colour is followed by a `float` in an HLSL `cbuffer`. Do they share one 16-byte row?
2. Why are `materialData_` and the GPU buffer padded to a multiple of 16?
3. What would go wrong if `SetMaterialBlock` kept the old GPU buffer after the struct size changed?

**Answers.** (1) Yes: 12 + 4 bytes fill one row. (2) The GPU allocates and reads constant buffers in 16-byte units; a smaller buffer is rejected or read out of bounds. (3) The shader would read a block of the wrong size: stale or missing fields, and debug-layer errors.
