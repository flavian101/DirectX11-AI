# Lesson 5 - An HDR scene buffer and full-screen passes

## Understand

**Until now the screen was the only canvas.** Every pixel was computed and stored straight into the back buffer: 8 bits per channel, values from 0.0 to 1.0, anything brighter clipped to white. That limits two things at once: how bright a pixel can be, and how many shades fit between dark and light.

**The fix is a two-stage pipeline.** Draw the world into a bigger, off-screen buffer first. Then run a final pass that converts it into the image for the screen. Today the second stage is a plain copy, so **the picture should look the same as before.** That is the point: we prove the plumbing before changing any colours.

## Visualize

**Think camera RAW and JPEG.** The HDR buffer is the RAW file: more range than any screen can show. The resolve pass is the JPEG export. Lessons 6 and 7 are the export settings (gamma and tone mapping).

## Why it is built this way

**`R16G16B16A16_FLOAT`.** Each channel is a 16-bit float, so 1.0 is just an ordinary number and 8.0 or 40.0 survive intact. It costs 8 bytes per pixel, about 16 MB at 1080p. The back buffer is only 4.

**The target is created with the other size-dependent resources.** It must match the window, so `CreateSizeDependentResources` builds it and `ReleaseSizeDependentResources` frees it. That is the Resize lesson again: anything the size decides goes through that pair.

**A full-screen triangle, not a quad.** One oversized triangle `(-1,1) (3,1) (-1,-3)` covers the screen, and the GPU clips the overhang. A two-triangle quad has a diagonal seam where pixels are shaded twice. The vertices come from `SV_VertexID`, so there is no vertex buffer at all.

**No vertex data means no input layout.** `Shader::Load` used to build one for every vertex shader. The full-screen shader reads only `SV_VertexID`, so we ask the compiled shader (`D3DReflect`) whether it reads any real vertex attribute and skip the layout if not. Binding a null layout is correct here.

**`Load`, not `Sample`.** The HDR buffer and the screen have the same size, so screen pixel `(x, y)` is texel `(x, y)`: read it exactly, no filtering, no sampler needed. If you later render at a different resolution, switch to `Sample` with a linear sampler.

**State hygiene, again.** The scene pass finished with the water's alpha blending still bound. `PostProcess::Resolve` resets blend, depth, and rasterizer state itself and uses a no-cull state so the triangle's winding cannot matter. Never trust what the last draw left behind.

## Other ways, and the trade-offs

**`R11G11B10_FLOAT`.** Half the memory, no alpha, lower precision: the usual choice in shipped games. Alpha blending of the water still works, since blending only needs the source alpha. **`R32G32B32A32_FLOAT`.** Overkill and four times the bandwidth. **Resolve inside the last scene draw.** Possible, but you lose the ability to run effects (bloom, depth of field) on the whole image first.

**The cost.** One extra full-screen read and write per frame, plus 16 MB. Cheap compared with what it unlocks.

## Predict, then check

1. Why does `ReleaseSizeDependentResources` now release the HDR buffer?
2. `Resolve` unbinds the HDR texture at the end, and `BindSceneTarget` unbinds it again at the start. What goes wrong if neither does?
3. Why is the UI drawn after the resolve?

**Answers.** (1) It is window-sized, so after a resize it must be rebuilt at the new size. (2) Next frame the texture would be bound as a shader input and as a render target at once; D3D silently drops one of them and the scene renders wrongly or not at all. (3) The UI is authored for display colours; drawing it into the HDR buffer would run it through gamma and tone mapping later in this step.
