# Lesson 1 - Render targets, depth you can read, and passes

## Understand

**A frame is a list of passes.** A pass is "pick where to draw, pick a viewport, draw some things". So far you had exactly one pass: everything into the back buffer. Shadows add a second one that draws *only depth*, from the sun's point of view, into a texture of our own.

**The back buffer's depth is not reusable.** The depth buffer you create in `CreateSizeDependentResources` exists only so depth testing works. It has no shader view, so no shader can read it later. A shadow map is the opposite: we draw into it first and *read* it afterwards.

## Visualize

**One block of memory, two lenses.** Picture the texture as 2048x2048 slots, each holding 32 raw bits. The depth view (DSV) is a lens that says "treat these bits as a depth float and let me write to them". The shader view (SRV) is a lens that says "treat them as an R32 float and let me sample them". The memory never moves; only the lens changes.

## Why it is built this way

**Typeless texture.** A texture created directly as `D32_FLOAT` cannot get a shader view. So we create it as `R32_TYPELESS` ("32 bits, undecided") and let each view pick its interpretation. This is the standard D3D11 recipe for readable depth.

**Never both lenses at once.** If the same texture is bound as a depth target and a shader input in the same draw, D3D silently nulls the shader input and the debug layer prints a warning. It is the same family of bug as the `ResizeBuffers` lesson: the pipeline holds references, and you have to let go explicitly.

**Vertex-shader-only programs.** `Shader::Load` now accepts a null pixel shader path. With no pixel shader bound the GPU skips all colour work and just writes the interpolated depth. Note `Apply` still calls `PSSetShader(nullptr)`: that is what *unbinds* the previous object's pixel shader.

**`FrameData` instead of `Camera`.** The per-frame buffer needs view, projection and an eye position. The sun is not a `Camera`, so tying the buffer to that class would force us to fake one. Now any pass builds a `FrameData` by hand and the shaders cannot tell the difference.

## Other ways, and the trade-offs

**Depth format.** `D24_UNORM_S8_UINT` (via `R24G8_TYPELESS`) uses half the memory of D32 and is what most older samples use; D16 is fine for tiny maps. D32 float is the simplest to reason about with an orthographic sun, so we use it.

**A fake `Camera` for the sun.** Works, but `Camera` carries first-person movement and a perspective-only projection. Cheap now, awkward later.

**A trivial pixel shader instead of none.** Required when a caster needs alpha cutouts (leaves, fences): the pixel shader must `discard` transparent texels. We do not need that yet.

## Predict, then check

1. Patch 2 binds this texture's *depth view* at the start of the shadow pass. Which shader-input slot must you clear first, and why?
2. Why can the same `DepthTarget` class later serve water depth fade?

**Answers.** (1) The slot where last frame's main pass left the shadow map's *shader view* (`t2`): clear it before binding the depth view. (2) Water depth fade needs to read the scene's depth in a pixel shader, which is exactly "draw depth into a texture, then sample it".
