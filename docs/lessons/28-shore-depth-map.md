# Lesson 28 - A depth map for the shore

## Understand

**The water did not know the ground was there.** Every wave had the same height and speed over a deep trench and over a beach, and the shore foam was measured from the camera's depth buffer. Three separate problems came from that, and none of them was the wind.

**One new idea fixes all three: measure the ground once, on the CPU.** For each water body we bake a small map of `depth = water level - ground height`. The shaders read it for waves and foam. The CPU reads the same numbers in `Water::SampleHeight`, so a boat sees the waves you see.

## Visualize

**Select a water body, then Advanced > Debug > Shore depth.** Red is dry land, green grows with depth, blue is surf (waves about to break). Pull the terrain's height scale and watch the red and green move. A magenta body means no terrain is under it.

**Look along a beach.** Out at sea the waves are as before. Closer in they grow a little taller, bunch up, then shrink to nothing at the waterline, with foam on the crests in that zone. The thin foam line slides up and back with each wave.

## Why it is built this way

**Why measure it on the CPU?** The terrain already keeps its height grid (`Terrain::GetHeightAt`). Reading that for a few thousand points is far cheaper than asking the GPU, and the result is also what gameplay code needs. `WaterShore.h` has no Direct3D in it, so `tests/water_test.cpp` checks the bake and the sampling.

**Why a map in the body's OWN space?** The water mesh vertices already carry local coordinates from -1 to 1. If the map uses the same coordinates, moving, turning or scaling the body never changes what a texel means. The body only has to re-bake when the numbers it depends on change: its place, size, level, or the terrain's `GetRevision()`.

**What do the waves do with it?** `ShoalAmplitude` (in `WaterWaves.h` and `waves.hlsli`, same formula) does two things. In shallow water relative to the wavelength a wave grows up to 50% taller. Then it is limited to 40% of the depth with a smooth `tanh` clamp, so it reaches 0 at the waterline and a trough can never dip below the ground. The sideways pull of the crest is never larger than the open-water value, so a wave that grows cannot fold over.

**Why not turn the waves toward the shore as well?** Real waves refract: crests end up parallel to the beach. In our model each wave is `sin(k * dir . x + omega * t)` with a fixed `dir`. Making `dir` depend on position breaks that formula, because the phase would need to be *integrated* along the wave path. That is a different technique (a baked phase field, or a few texture-driven waves), so it is the next step, not part of this one.

**Why does the foam use vertical depth now?** `thickness` is the difference of two eye-space depths. It depends on where the camera looks. The map gives the true vertical depth, and the shore band no longer changes width when you tilt the camera. We still take the smaller of the two, so a rock or a hull poking through the water gets its contact foam.

**What is `swash`?** The waterline moves with the surface height at the pixel, plus a small lift taken from the first wave's phase, so the foam line and the see-through edge slide with each wave. The mesh itself does not run further up the beach: the geometry cannot move that far, so the visible run-up stops at the point where the water plane meets the ground.

## Other ways, and the trade-offs

**Keep the camera depth buffer only (what it did).** Costs nothing extra, catches every object, but depends on the view and knows nothing about the sea floor.

**Distance-to-shore field.** Cheap gradient (gives you a shore direction for refraction), but ignores how steep the beach is, so surf looks the same on a cliff and on a flat beach.

**Shallow-water simulation.** Physically right, with real wash and backwash, but heavy on the GPU and hard to author. Not worth it at this stage.

**Limits of what we built.** One terrain only (`Scene::GetTerrain`). A big lake gets coarse texels (the map is capped around 360 x 360 for a square body), which is fine for something this smooth. Objects that are not terrain do not change the depth map.

## Predict, then check

1. Stand at a shore and tilt the camera from looking down to nearly level. With the old `thickness` foam, did the band get wider or narrower? Why does the vertical depth not do that?
2. What are the waves' heights where the depth is exactly 0.5 and the nominal amplitude is 0.35? (Use `ShoalAmplitude` and the 0.4 limit.)
3. A river has Shore waves 0.5. Where would it still show foam if you set that to 0?

**Answers.** (1) It depends on the geometry: eye-space depth difference equals the vertical depth only when the camera looks straight down; the more level you look, the larger it is for the same vertical depth, so fewer pixels fall under the foam distance and the band gets NARROWER. (2) At most 0.4 * 0.5 = 0.2, and the `tanh` brings it just under that. (3) Along the waterline (the depth band) and where an object touches the water, but no surf foam, because surf is scaled by the shore strength.
