# Lesson 24 - Sharper waves, tiled water, and asking the water how high it is

## Understand

**Three changes, one theme: spend the work where you can see it.** The water got better waves (Gerstner), a mesh that is dense near the camera and sparse far away (tiles with level of detail), and it stopped drawing things nobody can see (visibility checks).

## Visualize

**Gerstner waves.** A sine wave only moves a point up and down, so a crest is as round as a trough. Real crests are narrow and troughs are wide because water also moves *toward* the crest. A Gerstner wave adds that sideways move: `x' = x + Q * a * cos(phase) * dir`, `y = a * sin(phase)`. `Q` (the editor's *Choppiness*) says how far points are pulled. Past `sum(Q * k * a) = 1` the surface would fold over itself, so `WaterWaves::Build` scales `Q` to stay under that.

**Tiles.** The water is 8 x 8 meshes. Draw the nearest ones with every vertex, the farthest with one vertex in eight. Neighbours at different levels must still meet exactly, or cracks open. The trick: every tile keeps ALL vertices on its four border edges at every level and coarsens only its interior (`WaterMesh.h`). Two tiles share the same border vertices whatever their levels, so there is nothing to stitch.

## Why it is built this way

**The wave table lives on the CPU** (`WaterWaves.h`) and is uploaded to the shader. The same numbers drive the screen and `Water::SampleHeight(x, z, scene.GetTime())`, so a boat floats on the waves you see. Gerstner points move sideways, so the height *at* a world position is found by undoing that move with a few fixed-point steps.

**The normal is exact.** The pixel shader evaluates the waves at the REST position (what a Gerstner wave is parameterised by) and builds `N = cross(dP/dz, dP/dx)`. Its Y component is the *Jacobian*: 1 on flat water, falling toward 0 where a crest is about to fold. Low Jacobian is where real waves break, so it drives the crest foam.

**Waves fade out by size on screen.** A wave shorter than a few pixels cannot be drawn without shimmering, so the pixel shader fades each wave by `wavelength * pixelsPerUnit`, like a mip level. The vertex shader fades waves the grid is too coarse to hold, using the grid spacing at that distance.

**Refraction needs a copy.** The water is drawn over the picture it wants to bend, and a texture cannot be read and written at once, so the renderer copies the opaque picture first (`CopyResource`, only while water is on screen and *Refraction* > 0). The shader refuses to read a pixel that is in front of the water, which would drag a nearby object into the lake. The blend state is now premultiplied alpha so refraction, body colour and foam combine exactly.

**Skipped work.** The mirror pass (a full second scene draw) used to run even while you looked at the sky. `Scene::GetPlanarReflection` now asks `Water::IsVisible` first.

## Other ways, and the trade-offs

**FFT oceans** (a sum of hundreds of waves from a spectrum) look best for open sea but need compute passes and displacement textures. **Projected grids** put a mesh in screen space and pick vertex density perfectly, at the cost of edge and shore handling. **Tessellation shaders** would subdivide on the GPU instead of the CPU tile levels used here. **Foam trails and wakes** need a texture the water writes into and fades over time.

## Predict, then check

1. Set *Choppiness* to 1 and *Crest foam* to 1: where does the foam appear, and why not in the troughs?
2. Turn on *Wireframe*, fly toward the water, and watch a tile change level. Why is there no crack?
3. Why does `Water::SampleHeight` need the scene time as an argument?

**Answers.** (1) On the crests, where the Jacobian is smallest. In the troughs the surface is stretched (Jacobian above 1), never squeezed. (2) The border vertices are identical at every level. (3) The waves are a function of time, and the GPU animates them with the scene clock: pass that same clock to get the same surface.
