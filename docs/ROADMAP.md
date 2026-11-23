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

- **H1** `[~]` HeightPicture and HeightRaster: import without clipping
  - What: `HeightPicture` reads an 8- or 16-bit PNG as integer levels. `ImportHeightRaster` maps it linearly to metres (`lowestY`, `relief`).
  - Tests: `tests/height_raster_test.cpp`
  - Acceptance: for the dark-alien picture the raster has exactly the file's 6,425 distinct values, the minimum is `lowestY`, the maximum is `lowestY + relief`, the mapping is monotonic and **no pixel is clamped**: the 20,954 pixels above the old white point keep their 1,246 different heights. (Numbers measured independently with Python before the test was written; the test was also run against a deliberately clamping mutant and fails on it.)
- **H2** `[~]` Fit, not clip: range statistics
  - What: true range, share of the area below a given Y (for example the sea), typical slope. Percentile auto-levels are not carried over.
  - Tests: `tests/height_stats_test.cpp`
  - Acceptance: for the dark-alien picture the share below the lowest height is 0 and just above it is 12.436% (521,605 of 4,194,304 samples); the 21,406 samples above 161.5 m are all present; the mean slope between neighbouring pixels is 54.86 degrees at 240 m of relief (a fact H3 has to deal with). Reference numbers computed independently in Python; two deliberate mutants fail the test.
- **H3** `[~]` Smooth sampling and area-average resampling
  - What: bicubic (C1) sampling with no clamp; one area-average down to the lattice spacing when the raster is finer than the cell size. Replaces the mip pyramid and prefilter.
  - Tests: `tests/height_sample_test.cpp`
  - Acceptance: the slope is the same on both sides of every pixel border (jump below 0.001, where the old straight-line blend jumps by more than 1); the surface passes through every pixel; a straight slope is exact; a cliff is not clamped (it overshoots a little, which is real ground). A one-pixel ridge at 8 pixels per cell moves the lattice heights by at most 1/8 of its height when it shifts one pixel; point sampling moves them by all of it. On the dark-alien picture, shifting the lattice by one pixel changes the worst sample by 13.6 m with point sampling and 6.75 m with the area average (another crop: 8.86 m and 2.34 m), and the RMS change by about 1.5 to 1.9 times less. That is far less than the synthetic ridge suggests, because real ground is steep and a shift is real change too. A tent or Gaussian filter was measured in Python and is only 8 to 13% better again while blurring more, so the box stays. Reference numbers computed independently in Python; three deliberate mutants (bilinear, clamped, point sampled) fail the tests.
- **H4** `[~]` 8-bit sources de-terraced without inventing data
  - What: bounded smoothing: blur, then clamp every pixel back into its own quantisation bin, repeated. 16-bit pictures skip it.
  - Tests: `tests/height_deterrace_test.cpp`
  - Acceptance: on smooth ground quantised to 8 bits the RMS error against the true surface drops from 0.289 to 0.068 levels (gentle slope) and from 0.288 to 0.052 (steeper): more than 3 times lower. Every pixel stays inside its own bin and no two pixels the file ranks apart are swapped. A 16-bit picture and a flat one come back unchanged. **Limit, measured:** the dark-alien picture reduced to 8 bits is rough from pixel to pixel, and there de-terracing is neutral (RMS 0.2702 raw, 0.2711 after): the stairs are lost in the roughness. So it helps where terraces are visible (gentle ground) and is harmless elsewhere; 16-bit files remain the better source. The import fits the relief to the range left after the blur, so the lowest point is still exactly `lowestY` and the highest exactly `lowestY + relief`. Reference numbers computed independently in Python; three deliberate mutants (no bin push-back, no blur, 16-bit also processed) fail the tests.
- **H5** `[~]` Detail noise as an additive layer in metres
  - What: strength, wavelength, octaves, seed, ridged. Band-limited, unclamped, deterministic in world coordinates. The valley mask is not carried over.
  - Tests: `tests/height_detail_test.cpp`
  - Acceptance: strength 0 (or a negative or NaN one) adds exactly 0; the detail never goes beyond plus or minus the strength, in either style and at 1 to 8 octaves, and really uses it; over a 3 km square its mean is under 5% of the strength (a ridged layer had a mean of 0.2423 of its strength until that was measured and removed); the slope never exceeds the bound that follows from the octave sizes (a finer octave than promised breaks it); no jump across x = 0 or z = 0; the same position gives the same value anywhere in the world. Four deliberate mutants (ridged mean left in, octaves too fine, truncating instead of flooring, no normalisation) fail the tests.
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
- **W6** `[~]` A sea from the wind
  - What: the weather's wind is a real wind (a speed in m/s and a direction) and is the only wind: a water body has none of its own. The sea is a
    JONSWAP spectrum of that wind over the body's fetch (an ocean: unlimited; a lake: its own length along the wind), cut into bands, one Gerstner wave
    per band, each travelling at the speed its length asks for (`omega^2 = g k`). Nothing is scaled to fit the mesh, so the sea no longer depends on how big the body is.
  - Acceptance: over open water at 10 m/s the significant height is about 2 m and the peak wavelength 80 to 100 m, whatever the size of the body; a lake's waves stay
    short and low in the same wind; every wave of the table has the speed its length asks for; no crest folds over.
  - Tests: `tests/sea_state_test.cpp`
  - Editor check to do: Weather > Wind > Speed from 3 to 25 m/s makes the open sea taller, longer and faster (the Waves panel shows the numbers); a lake in the
    same wind stays small; an old Ocean body needs Wave model set to Sea; moving a body or changing its size never changes its waves (an ocean) or only its fetch (a lake).
- **W7** `[ ]` Where the sea is not
  - What: the world's sea (W1) is water only where the ground SURFACE is below `seaLevel` and the spot is not inside an exclusion zone (a box in world x, z that the
    world owns). Space under dry ground (a cave, a dungeon) never gets a sea, and a basin that is below sea level but should stay dry (a drained dock, a cave mouth) can be excluded.
  - Tests: `tests/sea_level_test.cpp` (shared with W1)
- **W8** `[ ]` One sea for the whole world
  - What: W1's sea as a camera-centred ring mesh with a constant vertex count at any world size, and a wave phase kept relative to the camera in double so it stays exact far from
    the origin. Replaces the 2000-unit limit and the per-body vertex budget of `Water`.
  - Acceptance: a 20 km world shows the same sea near the camera as a 200 m one, and the sea has no seams where tiles meet.

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
