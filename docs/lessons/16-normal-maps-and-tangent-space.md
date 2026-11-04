# Lesson 16 - Normal maps and tangent space

## Understand

**A normal map fakes bumps by lying about direction.** The triangle stays flat. Each pixel reads a *different normal* from a texture, so the lighting behaves as if the surface were bumpy. Silhouettes and shadows stay flat; only the shading changes.

**The map stores directions relative to the surface, not the world.** A brick face pointing north and one pointing up share the same texture. So the stored normal means "tilted a little to the right, a little up, mostly straight out of the surface". Colour (0.5, 0.5, 1.0) is the flat case: it decodes to (0, 0, 1).

## Visualize

**Run the editor, select the Cube, and open "Debug view".** "Vertex normal" shows smooth colour per face. Switch to "Mapped normal" and the mortar lines and brick edges appear in the colour. The lit view is the same difference, with light on it. Drag "Normal strength" from 0 to 3 and watch the bricks flatten and deepen.

## Why it is built this way

**Tangent space needs three axes.** To turn "a little right, a little up" into a world direction, the pixel needs to know which world direction is "right" and which is "up" *on this surface*. Those are the **tangent** T (the way U runs) and the **bitangent** B (the way V runs), plus the normal N. Together they are the surface's own coordinate system, and the matrix made from them is called TBN.

**We store only the tangent.** B can always be rebuilt as `cross(N, T) * w`. The `w` is +1 or -1 and records whether the UVs are mirrored. So the vertex grew by four floats (T and w), not six. `Vertex` went from 32 to 48 bytes, and `static_assert` guards the layout.

**Computing the tangent.** For each triangle, two edges give two equations: `e1 = T*du1 + B*dv1` and `e2 = T*du2 + B*dv2`. Solving them gives T and B from positions and UVs (`ComputeTangents` in `Mesh.cpp`). Shared vertices average their triangles. Then Gram-Schmidt removes any part of T along N so the two are exactly perpendicular.

**The pixel shader re-squares the frame.** The tangent and normal are interpolated across the triangle, which makes them drift from perpendicular. `ApplyNormalMap` normalises again and subtracts the part of T along N before building the matrix.

**The input layout may offer more than a shader reads.** We added `TANGENT` to the one shared layout. The shadow and water shaders ignore it and keep working. The reverse (a shader that reads what the layout lacks) fails at creation time.

**Neutral defaults instead of branches.** A surface with no normal map would need an `if` in the shader. Instead every slot always holds a texture: a 1x1 flat normal, white occlusion, black emission. `Texture::Solid` makes them. The shader has no special case, and a missing file just looks like "no map".

**Colour space, again.** The normal map and the ORM map are data. Loaded as sRGB, the GPU would bend their numbers (lesson 6). They use `ColorSpace::Linear`; only albedo and emissive are sRGB.

**Green channel convention.** OpenGL-style maps have green pointing up the image; DirectX-style, down. Our UVs run V downward, so an OpenGL map needs its green flipped. The "Flip green" checkbox does it. If the bricks look like they stick out where mortar should be, toggle it.

**ORM packing.** R = ambient occlusion, G = roughness, B = metallic: three greyscale maps in one texture, one sample. Metallic is not used yet. Occlusion only darkens the *ambient* light (a crevice still catches direct sun), which is why `ComputeLightingEx` now returns ambient separately.

**Roughness is a stand-in.** Blinn-Phong has a shininess exponent, not roughness, so `2/r^4 - 2` converts one to the other (r = 0.5 gives about 30, the old fixed 32). The IBL patch replaces this with a proper physically based model; the maps and slots stay.

## Other ways, and the trade-offs

**Compute the frame per pixel from screen-space derivatives.** `ddx`/`ddy` of position and UV give T and B without storing tangents. It saves vertex memory and needs no tangent generator, but it is noisier on small triangles and costs ALU in every pixel.

**Store a quaternion instead.** One 4-float rotation holds the whole frame. Compact and smooth, but harder to author and debug. **Mikktspace.** The standard algorithm that baking tools use. Match it, or your tangents will disagree with the baker's and bumps will shade slightly wrong. Assimp can produce it for loaded models.

**Bump (height) maps.** One channel, smaller, needs derivatives at runtime. Normal maps won because they are cheaper per pixel.

## Predict, then check

1. A cube face has its UV mirrored. What changes in the vertex, and why does the shader need it?
2. Why are normal maps loaded as `Linear` but albedo as `SRGB`?
3. With "Normal strength" at 0, what does the surface look like, and why?

**Answers.** (1) `w` becomes -1, so `cross(N, T) * w` still points along the real V direction. Without it the bumps would be lit from the wrong side on that face. (2) The normal map holds directions, not colours; sRGB decoding would bend 0.5 to about 0.21 and tilt every normal. (3) Exactly like the unmapped surface: the sideways part is scaled to zero, leaving (0, 0, 1).
