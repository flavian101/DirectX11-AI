# Lesson 32 - What the water costs, and how to measure it

## Understand

**You cannot tune what you have not measured.** A guess says "the water is slow". A number says "mirrors take 3.1 ms, the water itself 0.8 ms". The first thing this patch adds is a GPU timer, so the next change can be judged by a number.

**The expensive part is probably not the water mesh.** The planar mirror (`RenderReflectionPass`) draws the opaque scene a second time: terrain, scatter and models, lit and shadowed, at half the screen size. Anything the water causes the scene to draw is billed to the water. The timer's "mirrors" figure shows that bill.

**Two kinds of waste were visible in the code.** (1) A water body that is on screen only where the terrain stands above it still asked for a mirror, and still sent its tiles to the GPU. (2) The mirror view drew chunks, plants and models that lie wholly under the water plane, only for the clip plane to throw every pixel away.

## Visualize

**Select a water body, then Advanced > Performance.** Two new lines appear: how many tiles were skipped because they lie under dry ground, and the GPU milliseconds per pass. Move the water level below the terrain around it and watch "skipped" rise and the mirror time fall.

**Turn the reflection mode to Sky and back.** The difference in the "mirrors" figure is the price of the planar mirror. If it is most of your water cost, no amount of mesh tuning will matter.

## Why it is built this way

**Why a timestamp query?** A draw call only queues work. A CPU stopwatch around it measures the queueing, not the work. `D3D11_QUERY_TIMESTAMP` is written into the command stream, so it records the moment the GPU itself reaches that point. The results arrive a few frames later, which is why `GpuTimer` keeps a ring of four frames and never waits for one (waiting would stall the CPU and change what we measure).

**Why a "dry tile" flag?** The depth map already says, for every spot of the body, `water level - ground height`. A tile is dry when that value is below `-(tallest wave + swash + margin)` everywhere in the tile and a little around it (waves push the surface sideways too). Then no wave can lift the surface above the ground, so the terrain hides every pixel. The flag is computed when the map is re-baked or the waves change, never per frame. `DepthField::MaxDepthIn` reads one extra texel of border, because a bilinear read near a tile edge mixes in its neighbour. The test in `water_test.cpp` checks that no sampled value inside a rectangle ever exceeds what `MaxDepthIn` reports.

**Why ask `IsWetVisible` before planning a mirror?** `PlanWater` used to ask "is any of the body in view?". A big ocean whose visible part is a bay under a cliff said yes. Now it asks "is any tile that is not under dry ground in view?". It is still a box test per tile, not a true occlusion test, so a wet tile hidden behind a hill still counts. That is the limit of this approach.

**Why `clipBelow` on the view?** The mirror camera sits below the water and the clip plane keeps only what is above it. Anything whose box is wholly under the plane can never contribute, so skipping it on the CPU saves its vertex work, its triangles and its pixel-shader launches. The same test serves terrain chunks, scatter cells and model parts through one field on `RenderView`.

**Why `continue` for faded waves?** In the vertex shader a wave too short for the grid fades to 0, and in the pixel shader a wave under 2 pixels long fades to 0. Before, both still ran `sincos` and then multiplied by 0. Far water, which is most of the pixels of a big sea, now skips the high-frequency waves entirely.

## Other ways, and the trade-offs

**GPU occlusion query for the water.** Draw the water's bounding box with an occlusion query one frame ahead and skip the mirror if no pixel passed. It catches hills and cliffs, which the tile flag cannot, but it adds a frame of latency and pop-in. Worth doing if the timer shows mirrors are still the biggest cost.

**Cheaper mirror.** Render the mirror at quarter size (`PlanarQuarter`), cull to a shorter distance than the main view, or skip shadows in it. All trade detail for time; the timer tells you which one pays.

**Screen-space reflections.** No second scene draw, but it cannot show anything that is off screen, and it needs the HDR colour and depth you already have. A reasonable next step if the planar mirror stays too expensive.

**Limits of what we built.** The dry test only knows the terrain (not a rock or a building), the timer measures the 3D view only (not the editor UI), and numbers from a debug build are not numbers for a release build.

## Predict, then check

1. Put the water level 3 units above a flat lake bed, then raise the terrain around the lake. At what ground height does a tile at the lake's edge become "dry" if the tallest wave is 0.9 and the swash 0.6 times a 0.35 wave? (Work out `rise` first.)
2. In the mirror view, why is a terrain chunk whose box top is exactly at the water height NOT skipped?
3. The timer says water = 0.4 ms and mirrors = 3 ms. Which setting would you try first, and what would you expect to see?

**Answers.** (1) `rise` = 0.9 + 0.6 * 0.35 + 0.5 = 1.61, so a tile is dry once the ground is higher than 3 + 1.61 = 4.61 (depth below -1.61) everywhere in the padded tile. (2) The test is `worldMax.y < clipBelowY`, strictly less: a chunk reaching the plane still has pixels above it. (3) The mirror resolution or the mirror's culling distance; the mirror figure should fall in step, the water figure should barely move.

## Rebuild cold

Without looking: write the `MaxDepthIn` loop, then the one-line dry test. Then write down, in order, what `GpuTimer::EndFrame` does with a frame whose query is not ready yet.
