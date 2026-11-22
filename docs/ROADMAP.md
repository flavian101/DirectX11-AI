# Roadmap: terrain and water

Scope: what the first scene needs, a **heightmap terrain that looks like terrain** and **a sea that fits it cleanly**. This file replaces the
old roadmap (still readable in git history: `git show fb29681:docs/ROADMAP.md`).

**Rules**
1. **Existing code is a draft, not a foundation.** Nothing in it counts as working. A draft that is in the way is deleted, not patched around.
2. **No backwards compatibility.** Old scenes, old saves and the single `Terrain` are not supported. Re-author instead of migrating.
3. States, one per item: `[ ]` open, `[~]` code and unit tests landed but **not yet checked in the editor**, `[x]` checked in the editor
   (needs an `Editor:` line with the date and what was seen), `[!]` known bug. Unit tests alone never make an item `[x]`.
4. A commit that changes an item's state edits this file in the same commit and says so in its message (see Tracking).

## Tracking

The roadmap is checked by `tools/roadmap.py` (Python 3, no packages). Run it from the repository root:

| Command | What it tells you |
|---|---|
| `python tools/roadmap.py status` | every item and its state, per milestone, and the next open item |
| `python tools/roadmap.py check` | validates this file: a `[~]` or `[x]` item names test files that exist and are registered in `CMakeLists.txt`; `[x]` has an `Editor:` date. `ctest` runs it (test `Roadmap`) |
| `python tools/roadmap.py check --range origin/master..HEAD` | also validates commits: every state change is declared in the commit message and every declaration is true |
| `python tools/roadmap.py history` | every state change, commit by commit |

A commit that changes a state carries one trailer line per item, for example `Roadmap: H1 [ ] -> [~]`. Run `check --range` before pushing.
When an item reaches `[x]`, the `Editor:` line is the record of what was seen.

---

## 1. Why the terrain looks wrong (measured)

Measured on `assets/dark alien/Height Map.png` (2048 x 2048, 16-bit):

| Fact | Consequence |
|---|---|
| Levels 0 to 6475 of 65535 are used (0 .. 0.0988) | At the old `heightScale` of 240 m the terrain would be 23.7 m tall. This is why "levels" were added. |
| Auto-levels (0.5% / 99.5%) put white at level 5178 | Everything above is clamped flat: 20,954 pixels with 1,246 distinct heights become one value, and the top 20.0% of the range is gone. The clamp is permanent. |
| 12.436% of pixels sit exactly at the minimum (521,605 pixels) | A real flat floor in the file. The levels clamp adds to it. |
| Heights are stored 0..1 and clamped at every stage (levels, source, detail) | Any stage can silently flatten the ground, and `heightScale` / `baseHeight` have to be applied by every consumer. |

## 2. Decisions

1. **Heights are metres (world Y), end to end.** No 0..1 values past the importer. No `heightScale`, no `baseHeight`.
2. **Import never clips.** The picture's true minimum becomes `lowestY` and its true maximum becomes `lowestY + relief`. The original file is kept
   untouched; changing relief or lowest Y re-imports from the file.
3. **Ground is a function of world (x, z) in metres:** `base raster -> detail -> edits -> rivers`. Tiles, water, collision and scatter ask that
   function, never a loaded mesh.
4. **Edits are deltas in metres** on the world lattice, per tile.
5. **One terrain system:** the world. There is no single `Terrain`.
6. **The sea is a property of the world** (`seaLevel`, covering the world bounds). No hand-sized sea bodies.

---

## 3. Milestones

### M1. Rewrite the height system
Pure logic first (no Direct3D, built and tested on any compiler), then wired into the editor in H7.

- **H1** `[ ]` HeightPicture and HeightRaster: import without clipping
  - What: `HeightPicture` reads an 8- or 16-bit PNG as integer levels. `ImportHeightRaster` maps it linearly to metres (`lowestY`, `relief`).
  - Tests: `tests/height_raster_test.cpp`
  - Acceptance: for the dark-alien picture the raster has exactly the file's 6,425 distinct values, the minimum is `lowestY`, the maximum is `lowestY + relief`, the mapping is monotonic and **no pixel is clamped**.
- **H2** `[ ]` Fit, not clip: range statistics
  - What: true range, share of the area below a given Y (for example the sea), typical slope. Percentile auto-levels are not carried over.
  - Tests: `tests/height_stats_test.cpp`
  - Acceptance: the share below the lowest height is 0 and just above it is 12.436% for the dark-alien picture; the peak's top 20% is present in the raster.
- **H3** `[ ]` Smooth sampling and area-average resampling
  - What: bicubic (C1) sampling with no clamp; one area-average down to the lattice spacing when the raster is finer than the cell size. Replaces the mip pyramid and prefilter.
  - Tests: `tests/height_sample_test.cpp`
  - Acceptance: the surface is continuous in value and slope across pixel borders; it passes through pixel centres; a linear ramp is reproduced exactly; a one-pixel ridge at 8 pixels per cell moves the lattice heights by at most 1/8 of the ridge height when the ridge shifts one pixel (point sampling moves them by the whole height).
- **H4** `[ ]` 8-bit sources de-terraced without inventing data
  - What: bounded smoothing: blur, then clamp every pixel back into its own quantisation bin, repeated. 16-bit pictures skip it.
  - Tests: `tests/height_deterrace_test.cpp`
  - Acceptance: on a smooth surface quantised to 8 bits the RMS error against the true surface is at least 3 times lower than the raw file's, and every pixel stays inside its original bin.
- **H5** `[ ]` Detail noise as an additive layer in metres
  - What: strength, wavelength, octaves, seed, ridged. Band-limited, unclamped, deterministic in world coordinates. The valley mask is not carried over.
  - Tests: `tests/height_detail_test.cpp`
  - Acceptance: strength 0 adds exactly 0; the detail never exceeds the strength; its mean over a large area is small compared with the strength; the same position gives the same value anywhere in the world.
- **H6** `[ ]` Edits and rivers in metres
  - What: brush deltas and river `level`, `depth`, `bankHeight` in metres, on the new ground function.
  - Tests: `tests/height_edits_test.cpp`
  - Acceptance: after re-importing with another `lowestY`, a river's water level in world Y is unchanged.
- **H7** `[ ]` Wire in the new ground and delete the single terrain
  - What: `Terrain` is split. The tile (chunks, LOD, skirts, walls, splat) stays. Removed: heightmap load/save, `CarveRiver`, `Sculpt`, the paint map, the heightmap fields of `TerrainSettings`, `Scene::GetTerrain()` and its callers, migration, and the old shape code (`WorldShape`, `WorldPicture`).
  - Acceptance: it builds; a search for `GetTerrain`, `heightScale`, `baseHeight` and `AdoptFrom` finds nothing; tests of deleted code are deleted with it.
- **H8** `[ ]` Editor check of the whole height system
  - What: import the dark-alien picture, fit it, fly around.
  - Acceptance: flat areas appear only where the file is flat; peaks are rounded and complete; no stairs. Then H1 to H6 can become `[x]`.

### M2. Water that fits the world

- **W1** `[ ]` Sea level on the world
  - What: `seaLevel` in metres plus an on/off flag. The sea covers the world bounds exactly and nothing beyond them. The default level is a chosen percentile of the ground, shown in the UI.
  - Tests: `tests/sea_level_test.cpp`
- **W2** `[ ]` Shore depth from the ground function
  - What: never from loaded meshes, and never "100 m deep" for a tile that is not loaded.
  - Tests: `tests/water_visibility_test.cpp`
- **W3** `[ ]` Shore tiles at full detail
  - What: a tile whose bounds are within the wave margin of the sea level is drawn at LOD 0, so a coarse mesh cannot dip below the sea over dry land.
  - Acceptance: editor check first: force all tiles to LOD 0 and see whether the ocean-through-land artefact disappears. If it does not, the cause is elsewhere (skirts or the depth map) and this item changes.
- **W4** `[ ]` Seams: skirts and walls out of the water's depth
  - Acceptance: no foam band along tile seams.
- **W5** `[ ]` Editor check of the water contract
  - Acceptance: the contract below holds while tiles stream in, at the world edge, and while sculpting.

### Not planned yet
Scatter on the world, biomes, rivers across tiles, lakes and swamps, erosion and generators, camera-relative rendering, cascaded shadows, GPU
heights, water shading and effects. Each returns to this file only when the scene needs it.

---

## 4. Water-terrain contract

1. Ground below `seaLevel` has water above it.
2. Ground above `seaLevel` plus the wave margin has no water pixel.
3. A tile that is not loaded never means deep ocean inside the world.
4. The depth the water uses equals the ground the player sees at every LOD, within the wave margin.
5. Moving or resizing the world never changes where the shoreline is.
6. Sculpting updates the shoreline when the stroke ends.

## 5. Removed from the old roadmap

| Removed | Why |
|---|---|
| Levels, auto-levels, curve, invert, valley mask, `smoothing`, `bicubic` toggle, `prefilter` | They clamp data or patch around the clamp. Replaced by H1 to H5. |
| `heightScale`, `baseHeight`, old H1, F11, F12 | The two-heights problem exists only because heights are 0..1. Gone with decision 1. |
| Mip pyramid (`HeightPyramid`) | Replaced by H3. |
| Migration (`AdoptFrom`) and its tests | No backwards compatibility. |
| Tile file format | Nothing reads it, and it stores 0..1 heights. Revisit with a real streaming need, in metres. |
| Old `[x]` and `[~]` statuses | Reset; nothing was verified as a working whole. |
| Biomes, density brushes, GPU culling, erosion, old W2 to W5, camera-relative rendering, F2 to F10, F13 | Not needed for the first scene. |
