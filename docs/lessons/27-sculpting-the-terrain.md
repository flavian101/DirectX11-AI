# Lesson 27 - Sculpting the terrain

## Understand

**The loop.** A stroke is: the mouse goes down on the ground, a brush changes some samples of the heightfield every frame, and the mouse comes up. Each step has four parts: ask the ground where the mouse points (lesson 25), change the height *numbers*, work out which chunks hold those numbers, and rewrite just those chunks' vertices on the GPU.

**The heightfield is now the truth.** Until now the heightmap plus noise made the heights. Once you sculpt, the grid you shaped is what counts. The editor locks Mesh resolution, noise and the heightmap, because changing them would build a different grid. Width and height scale stay free: heights are stored as 0..1, so those only stretch them. *Discard sculpting* goes back, and Undo brings the sculpt back.

## Visualize

**Try it.** Open the editor, press `4` (or the *Sculpt* button) and drag on the terrain. A yellow ring follows the mouse and hugs the hills. Shift flips Raise and Lower, `[` and `]` resize the brush. Raise a hill, Smooth its top, Flatten a plateau, then undo and redo each stroke.

**Save and reload.** Save the scene: a file `yourscene.terrain.png` appears next to it. Open the scene again and the sculpt is back. Delete the PNG and the load reports a warning instead of silently losing your work.

## Why it is built this way

**One brush step, in four lines of maths.** For every sample within the radius, find its distance `d` from the centre and ask `BrushWeight(d)`: 1 in the inner part, then a smoothstep fade to 0 at the edge. Raise adds `weight * strength * 0.25 * seconds` (a fraction of the full height). Smooth moves the sample towards the average of its 3 x 3 neighbours, Flatten towards the target height, both by `weight * strength * 8 * seconds` (capped at all of the way).

**Seconds, not frames.** The step is multiplied by the frame time, so a fast and a slow computer sculpt the same speed. The editor also caps one step at 0.05 s, so a hitch cannot make the ground lurch.

**Two passes, because Smooth reads neighbours.** First work out every new value from the *old* ones, then write them all. If it wrote as it went, the result would depend on which way the loop runs. The test proves it: a symmetric spike must stay symmetric.

**The brush is a circle in local units.** The distance uses real units, not grid steps, so on a stretched grid (cells 2 wide, 1 deep) it is still round. The terrain divides the world radius by its scale before calling in.

**Recording a stroke: remember the *first* value.** The recorder is told each sample's value just before it changes, and keeps only the first one it sees per sample. At the end, the patch holds `before` and `after` for every sample touched. Samples that ended where they started are dropped (with a 1e-6 tolerance, since `0.5 + x - x` is not always exactly `0.5`).

**Undo stores the change, not the world.** A snapshot of the whole grid is 4 MB; a stroke over a radius-12 brush is a few thousand samples. Undo writes the `before` values, redo the `after` values, and refreshes the chunks they touch.

**Which chunks to rewrite.** Chunk `k` covers samples `32k` to `32k + 32`, *inclusive*, so a border sample lives in two chunks and both keep their own copy. A vertex's normal also reads one sample to each side, so the area to redo is the edit grown by one sample. `ChunksToRefresh` returns the chunks holding anything in that area. The test checks it two ways: against a brute-force "which chunk holds this vertex?", and by running 200 random strokes, refreshing *only* those chunks, and comparing with rebuilding *every* chunk. Leaving out the extra sample, or the border rule, leaves a stale chunk and the test fails.

**The GPU side.** Terrain meshes are now created with `D3D11_USAGE_DEFAULT` instead of `IMMUTABLE`, and `Mesh::UpdateVertices` calls `UpdateSubresource` to overwrite the whole vertex buffer of a chunk (about 57 KB). The index buffers never change: raising a hill moves vertices, it does not reconnect them. The chunk's bounding box is recomputed too, or culling would use the old shape. Building a chunk and refreshing it use the same function, `BuildChunkVertices`, so they cannot disagree.

**Why a PNG, and why by hand.** A 16-bit greyscale PNG is a heightmap any tool can open, and it is the format your heightmap loader already reads. A PNG is a signature, then chunks: *IHDR* (size and format), *IDAT* (the pixels inside a zlib stream), *IEND*. Each chunk ends with a CRC-32 over its name and data. The zlib stream here is made of *stored* blocks (no compression, up to 65535 bytes each) and ends with an Adler-32 of the raw bytes. Stored means about 2 MB for a 1025 x 1025 terrain, but it is simple and always valid. The test reads it back with stb_image, and I also checked a sample with `file` and Python's zlib.

**Precision.** The file holds `round(value * 65535)`. The error is at most half a step: with a height scale of 45 that is about 0.0003 units, far below anything visible.

**Saving is all or nothing.** The PNG is written to `.tmp` and renamed into place, and if it cannot be written the whole save fails with a message. A scene that silently forgot your sculpting would be worse than a failed save. Loading happens after the terrain settings, so the grid already has its final size, and a PNG of the wrong size is refused with a warning.

## What this step does not do

**The shoreline catches up when you let go.** Water measures the ground to draw its shore (the depth map), and re-measures whenever `Terrain::GetRevision()` changes. Sculpting moves the revision when a stroke *ends* (and on undo, redo and discard), not on every step: re-measuring every frame while you drag would be far too much work. So while you drag near a lake the foam lags behind, and it snaps to the new ground on release.

**The ring shows through hills.** It is an ImGui overlay of 64 lines, each placed on the ground height. A ring drawn by the terrain shader would be hidden behind hills; that belongs with the visual work later.

**Far chunks use coarse detail.** Sculpting edits the full-detail vertices. A far chunk draws a coarser level of the same vertices, so it follows the sculpt a little loosely, as it always did.

**A stale PNG can remain.** If you discard the sculpt and save again, an old `.terrain.png` stays on disk, unused. Nothing is deleted without being asked.

## Other ways, and the trade-offs

**Non-destructive layers.** Keep the generated heights and store the sculpt as a *difference* on top. Noise sliders keep working after sculpting, but every query adds two grids and the saved file is a signed delta. Worth it once generators (erosion, terracing) arrive as editable layers.

**Brushes on the GPU.** A compute shader applies the brush straight to a height texture: very fast for huge brushes, but the CPU still needs the numbers for picking, placement and undo.

**Dynamic buffers.** `D3D11_USAGE_DYNAMIC` with `Map(WRITE_DISCARD)` suits data rewritten every frame. A chunk is rewritten only while you drag, so `DEFAULT` with `UpdateSubresource` is simpler.

## Predict, then check

1. A 128-cell terrain: a brush edits only the sample at x = 64, a chunk border. Which chunk columns are refreshed?
2. Raise brush, strength 0.5, held for 0.2 s at its centre, height scale 40: how many world units does the centre rise?
3. Why must Smooth work out all new values before writing any?
4. Why can you still change Width and Height scale after sculpting, but not Mesh resolution?
5. Why is the PNG written to `.tmp` and then renamed?

**Answers.** (1) Columns 1 and 2: the area is x 63..65, chunk 1 covers 32..64 and chunk 2 covers 64..96. (2) `0.5 * 0.25 * 0.2 = 0.025` of the full height, times 40 is 1.0 unit. (3) Otherwise the result depends on the loop direction; the symmetry test catches it. (4) Heights are stored as 0..1, so those two only stretch them; a different resolution is a different grid and the sculpted samples would not map onto it. (5) A crash half-way through writing never replaces a good file with a broken one.

## Rebuild cold

**Before reading the code again**, write `BrushWeight` and `ChunkSpan` (in `TerrainChunks.h`) from the descriptions above, in a scratch file. Then build `HeightfieldEditTests` and `TerrainChunksTests`: they grade you. For `ChunkSpan`, remember the two rules: a border sample belongs to two chunks, and the first chunk is the one that *ends* at the first sample.

## The tests

**Four programs, all built by CMake** (`BUILD_TESTS`): `HeightfieldEditTests` (weights, the four brushes, strokes, undo patches), `TerrainChunksTests` (the refresh rule), `Png16Tests` (write and read back, including exact block-size edges) and, from before, `HeightfieldTests`.
