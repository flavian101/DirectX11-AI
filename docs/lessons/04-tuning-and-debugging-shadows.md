# Lesson 4 - Tuning, cost, and debugging shadows

## Understand

**There is no correct bias.** The right values depend on your scene's scale, the sun's angle and the map resolution. So the panel exposes them, and the real skill is a *method* for finding the smallest values that work.

## The tuning method

**Find the edge, then back off.** (1) Set depth bias, slope bias and normal offset to 0 and look at the terrain: you should see stripes (acne). (2) Raise *normal offset* first until the stripes on slopes fade. (3) Raise *slope bias*, then *depth bias*, only if some stripes remain. (4) Look at the cube's base: if its shadow detaches (peter-panning), you overshot, so lower the last value you raised. You want the minimum that makes stripes disappear, not the maximum that hides them.

## Why it is built this way

**Settings follow the same pattern as the terrain and water.** `ShadowSettings` holds the values and their legal ranges, `Clamp()` enforces them, and the sliders read the same constants. One source of truth.

**`SetSettings` rebuilds only what changed.** A rasterizer state is immutable, so a new bias means a new state object; a new resolution means a new 16 MB texture. Everything else (strength, normal offset) is just a number sent to the shader next frame. That is why `SetSettings` compares old and new values before touching anything.

## Cost: what you are paying for

**Memory is `resolution squared x 4 bytes`:** 1 MB at 512, 4 MB at 1024, 16 MB at 2048, 64 MB at 4096. The panel shows it.

**The shadow pass redraws every caster.** The terrain at mesh resolution 512 is about half a million triangles, and it is drawn a second time every frame. Raising terrain resolution to 1024 roughly quadruples that. Options when it hurts: use a coarser mesh for the shadow pass only, draw the shadow pass only when the sun or a caster moves (the terrain is static), or move to cascades.

## Debugging checklist (use "Show sun depth map")

**Map all one colour.** Nothing was drawn: check the depth-stencil reset in `Begin`, check `CastsShadow()`, check the pass runs before the main pass.

**Terrain visible, cube missing.** The cube's bounding sphere or `RenderShadow` override is wrong.

**Everything in the map looks tiny in one corner.** The bounding sphere is far too large; look at `ComputeShadowBounds`.

**Stripes on slopes, map looks fine.** Bias problem, not a pass problem: go back to the tuning method.

**Whole scene dark, or no shadows at all.** Check strength is above 0 and that `lightViewProj` reached the shader. The debug layer will also warn if the map is still bound as a shader input while the shadow pass uses it as a depth target.

## Other ways, and where this leads

**Cache the map.** If nothing moves, skip the pass. **Cascades** give sharp shadows near the camera. **Shadows on water:** call `ShadowFactor` in `water_ps.hlsl`. **Point-light shadows** need cube-map depth targets: six passes, a good use of the `DepthTarget` class.

**Step 2 of your roadmap builds on this.** Once the main pass renders into an HDR target instead of the back buffer, you will have a third pass and a good reason to introduce a small pass list.

## Predict, then check

1. You halve the resolution to 1024. What happens to texel size and memory?
2. Why does `SetSettings` compare old and new bias before creating a state?
3. Shadows look right but the cube's shadow floats a little off its base. What do you lower first?

**Answers.** (1) Texel size doubles (blurrier edges, more acne risk) and memory drops from 16 MB to 4 MB. (2) State objects are immutable; recreating one every frame while dragging an unrelated slider is wasted work. (3) The last bias you raised: normal offset, slope bias or depth bias.
