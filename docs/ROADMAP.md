# Roadmap: the island scene (ground, sea, and how they fit)

Scope: **one scene that works end to end: an island in an ocean.** Terrain is the base of everything else (scatter, placement and water all ask the
ground), so the ground and the sea are laid down as foundations first, and the old drafts are removed rather than patched. The previous roadmap
is in git history: `git show 1463cb3:docs/ROADMAP.md`.

**Rules**
1. **Existing code is a draft, not a foundation.** Nothing in it counts as working. A draft that is in the way is deleted, not patched around.
2. **No backwards compatibility, anywhere.** No old scenes, saves, presets or enum values are carried. An old scene fails to open with one clear message
   and is re-authored. Code whose only job is to keep an old scene looking the same is deleted.
3. **No parallel systems.** A new module replaces the old one in the same milestone. Two height systems or two wave models at once is a bug in the plan.
4. **A setting is a physical quantity or an artistic intent that survives moving to another scene.** A setting that exists to compensate for another
   setting (an "auto" scale, a size cap, a switch that turns an old look back on) is a patch and is deleted (list: section 5).
5. States, one per item: `[ ]` open, `[~]` code and unit tests landed but **not yet checked in the editor**, `[x]` checked in the editor
   (needs an `Editor:` line with the date and what was seen), `[!]` known bug. Unit tests alone never make an item `[x]`.
6. A commit that changes an item's state edits this file in the same commit and says so in its message (see Tracking).

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

## 1. What is wrong now (from the code and the video)

| Seen | Cause in the code | Consequence |
|---|---|---|
| The sea is a rectangle that stops in the open water | `WaterSettings::size` / `sizeZ` (at most 2000, `kSizeMax`); every body has its own plane, mesh budget and shore depth map | A sea cannot cover an island scene. The 2000 cap is the "patch" that stops working when the world is big. |
| The sea looks like plastic | the sun highlight is one Blinn-Phong lobe, `pow(dot(N,H), glintSharpness)` with a fixed exponent (default 128), in `shaders/water_ps.hlsl`; no roughness, nothing changes with distance | A single sharp lobe on every wave at every distance reads as a hard, glossy plastic. |
| The surface looks like a sheet sliding past | the small ripples are two layers of `ValueNoiseD`, both drifting along the one wind direction (`RippleSlope`) | Every ripple travels the same way at the same speed: one rigid texture, not water. |
| Waves look like a structure drifting | the sea is at most 16 Gerstner waves, one per spectrum band (`WaterWaves::kMaxWaves`, mirrored by `WATER_MAX_WAVES` in the shader's constant buffer), on a plane that also ends | Sixteen components can repeat visibly. A real sea has far more, spread over many directions. |
| The ocean fills the middle of the island | water is sized by hand; shore depth is baked per body | Nothing says "the ground decides where the sea is". |
| The world starts at 0.0 and the old terrain was centred | tile (0,0) starts AT the origin (`WorldGrid.h`); the old `Terrain` was centred on its position; `TerrainWorld::AdoptFrom` snaps one onto the other | The two conventions fight; the migration code exists only to hide it. |
| Two height systems | H1 to H5 (metres, tested) are built but not wired in; the old `Terrain` still carries `heightScale` (29 files, 103 hits) and `baseHeight` (12 files, 40 hits) | Every consumer has to know which one it is talking to. |

Not diagnosed yet: the white striped banding along the shore in the video. It may be the sand layer, the shore foam, tile LOD or the skirts. Item S6
starts with the water's own debug views (Depth, ShoreDepth, Foam) to find out before anything is changed.

## 2. What to borrow from Unreal (and what not)

Checked against Epic's documentation for the Water plugin:

| Unreal does | We do |
|---|---|
| A **Water Zone** defines the region; the surface is meshed as tiles and only what the camera can see is drawn. A cheap **far-distance mesh** fills the gap to the horizon, sits just under the base height so it cannot clip or show seams. | One **camera-centred ring mesh** with a constant vertex count at any world size, plus a far mesh to the horizon (S5). The sea has no `size`. |
| Water bodies are **splines**, and an **Island** body exists only to keep land above the water (applied after the other bodies). | The ground owns the sea (decision D3). We do not copy splines: the island is the ground; connectivity decides what is ocean. |
| A **water info texture** is built from the water bodies and the landscape height; the landscape height is part of the depth the surface sees. | One **world water info raster** (depth, ocean flag, direction to shore), built from the ground function (S1). Replaces the per-body depth map. |
| **Single Layer Water**: a physically based shading model with scattering and absorption coefficients, roughness, specular and a normal; the usual water material blends **two tiled normal maps panning in different directions**. | GGX specular with roughness, absorption and scattering as coefficients, two scrolling normal maps (O2 to O4). |

Waves "bashing the island" are **not simulated**. Engines fake it from the depth: waves grow in shallow water, break, and a swash line slides on the beach.
Your `shoreWaves` / `swash` / `shoreShelter` already follow that idea; they move onto the world water info (O5). Particles are for spray, later. They are not
why the water looks wrong.

## 3. Decisions

1. **One ground, in metres, end to end** (unchanged): `base raster -> detail -> edits`. No `heightScale`, no `baseHeight`. Import never clips.
2. **A new world is centred.** The grid maths stays as it is (tile (0,0) starts at the origin, integer tiles, exact in 64 bits), but New World makes bounds
   that are symmetric around (0, 0): tiles `-N .. N-1`. The island's middle is at the origin. **Sea level is Y = 0** by default and the sea floor is negative.
3. **The ground owns the sea.** A point is ocean when (a) the ground surface there is below `seaLevel`, (b) it is connected to the open sea (a flood fill on a
   coarse raster, seeded from outside the world), and (c) it is not in an exclusion box. A hollow below sea level in the middle of the island is **dry** unless it
   is joined to the sea. This is a design choice of ours, not Unreal's; Unreal answers the same question with spline bodies and Island bodies.
4. **The sea is infinite.** Outside the world bounds the ground is a constant depth (default -200 m), so the sea runs to the horizon and the world edge walls
   have nothing to hold back. Nothing about the ocean is sized by hand.
5. **Water is shaded, not tinted.** GGX specular with a roughness that comes from wave-slope variance (so it grows with distance and with small waves),
   Schlick Fresnel at F0 = 0.02, sky reflection blurred by roughness, colour from absorption and scattering per metre.
6. **Hand-sized water bodies (lakes, ponds, rivers, Custom) leave the editor for now.** They return later on the same water info and shading. To confirm:
   this is the biggest cut in the plan, and git history keeps all of it.

---

## 4. Milestones

### M0. Teardown
Each step is its own commit and leaves the build and `ctest` green. The order matters: the editor is the largest consumer, so it goes first.

Done with this roadmap: `WorldTileFile` (the baked tile file; nothing read it) is deleted with its test.

- **T1** `[ ]` One terrain only: delete the single terrain
  - What: remove `Scene::GetTerrain` (`Scene.h:43-44`, `Scene.cpp:215`) and its 16 uses in `editor/EditorLayer.cpp`; heightmap load/save, `CarveRiver`, `Sculpt`
    and the paint map from `Terrain`; the heightmap fields of `TerrainSettings` (`width`, `heightScale`, noise, picture); `TerrainWorld::AdoptFrom`,
    `MigrationStats`, `WorldSettingsFromTerrain`, `pictureOffsetX/Z`; `WorldShape`, `WorldPicture` and the tests of all of them. The tile (chunks, LOD, skirts, walls,
    splat) stays and is renamed `TerrainTile` if it no longer is a whole terrain.
  - Acceptance: builds; a search for `GetTerrain`, `heightScale`, `baseHeight`, `AdoptFrom`, `MigrationStats`, `WorldPicture`, `WorldShape` finds nothing.
- **T2** `[ ]` Water: delete the patches and the hand-sized bodies
  - What: remove `autoWaveScale`, `waveScale`, the `Spectrum` and `Classic` styles (only the wind sea stays), the settings the Sea model ignores (`waveAmplitude`,
    `waveFrequency`, `waveSpeed`, `waveRoughness`, `waveCount`), `WavePreset`, `WaterLook`, `WaterShape`, `size` / `sizeZ` / `kSizeMax`, per-body `meshDetail` and
    `lodDistance`, `shoreWaves = 0` ("the old look"), the body types other than Ocean and everything river (`WaterRiver`, `WaterFlow`, `RiverPath`, `RiverCarve`,
    `WorldRivers`, `WaterTypeUi`, `WaterSelect`), and the per-body depth map. `Buoyancy` keeps working: it asks the sea for a height.
  - Acceptance: builds; the editor offers one ocean; `buoyancy_test` and `sea_state_test` are green; a search for the removed names finds nothing.
- **T3** `[ ]` Scene files start again
  - What: a scene format version; a scene without it (or with an older one) fails to open with one message. Delete every "only ever ADD to the end" enum
    constraint, every field kept only so old scenes load, and the migration code in `SceneJson.h`.
  - Acceptance: an old scene gives the one message and does not crash; a new scene round-trips.

### M1. One ground
Pure logic first (no Direct3D, built and tested on any compiler), then wired into the editor.

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
- **H6** `[ ]` Edits in metres
  - What: brush deltas in metres on the world lattice, per tile, on the new ground function. (Rivers are gone: T2.)
  - Tests: `tests/height_edits_test.cpp`
  - Acceptance: after re-importing with another `lowestY`, an edit keeps its height in world Y; an edit across a tile border is continuous.
- **H7** `[ ]` Wire the new ground into the world
  - What: `TerrainWorld` takes its heights from H1 to H6 and nothing else; the editor's import panel is "picture, lowest Y, relief, metres per cell".
  - Acceptance: it builds; scatter and placement ask the same ground function the tiles are built from.
- **H8** `[ ]` A new world is centred (decision D2)
  - What: New World makes symmetric bounds; the world's picture is centred on the origin; sea level defaults to 0.
  - Tests: `tests/world_grid_test.cpp` (extended)
  - Acceptance: the middle of the island is at (0, 0); moving the picture never moves the grid.
- **H9** `[ ]` Editor check of the whole ground
  - What: import the dark-alien picture, fit it, fly around, sculpt, scatter.
  - Acceptance: flat areas appear only where the file is flat; peaks are rounded and complete; no stairs. Then H1 to H8 can become `[x]`.

### M2. The world owns the sea

- **S1** `[ ]` World water info
  - What: one raster over the world bounds, built from the ground function: depth (`seaLevel` - ground), ocean flag, unit direction toward the nearest land. Streamed
    with the tiles. A tile that is not loaded is never deep ocean inside the world (it is "unknown", and the sea waits).
  - Tests: `tests/world_sea_test.cpp`
  - Acceptance: the depth equals the ground the player sees at every tile LOD, within the wave margin.
- **S2** `[ ]` Where the sea is, and where it is not (decision D3)
  - What: connectivity from the open sea (flood fill on the coarse raster) and exclusion boxes. An inland hollow below sea level stays dry; cutting a channel to the sea
    floods it when the stroke ends.
  - Tests: `tests/world_sea_test.cpp` (shared with S1)
  - Acceptance: a test island with a hollow below sea level has no ocean in the hollow; opening a channel puts ocean in it; a box excludes a coast basin.
- **S3** `[ ]` Shore tiles at full detail
  - What: a tile whose bounds are within the wave margin of the sea level is drawn at LOD 0, so a coarse mesh cannot dip below the sea over dry land.
  - Acceptance: editor check first: force all tiles to LOD 0 and see whether the ocean-through-land artefact disappears. If it does not, the cause is elsewhere
    (skirts or the depth map) and this item changes.
- **S4** `[ ]` Seams: skirts and walls out of the water's depth
  - Acceptance: no foam band along tile seams; no walls at the world edge (decision D4).
- **S5** `[ ]` One ocean for the whole world
  - What: a camera-centred ring mesh with a constant vertex count at any world size, a far-distance mesh to the horizon just under the sea level, and a wave phase kept
    relative to the camera in double so it stays exact far from the origin.
  - Tests: `tests/ocean_ring_test.cpp`
  - Acceptance: a 20 km world shows the same sea near the camera as a 200 m one; no seam where the ring meets the far mesh; no visible edge of the sea at any camera height.
- **S6** `[ ]` Editor check of the water contract
  - What: first, with the debug views, find the cause of the white banding along the shore (section 1) and write it here.
  - Acceptance: the contract in section 6 holds while tiles stream in, at the world edge, and while sculpting.

### M3. An ocean that looks like water

- **W6** `[~]` A sea from the wind
  - What: the weather's wind is a real wind (a speed in m/s and a direction) and is the only wind: a water body has none of its own. The sea is a
    JONSWAP spectrum of that wind over the body's fetch (an ocean: unlimited; a lake: its own length along the wind), cut into bands, one Gerstner wave
    per band, each travelling at the speed its length asks for (`omega^2 = g k`). Nothing is scaled to fit the mesh, so the sea no longer depends on how big the body is.
  - Acceptance: over open water at 10 m/s the significant height is about 2 m and the peak wavelength 80 to 100 m, whatever the size of the body; a lake's waves stay
    short and low in the same wind; every wave of the table has the speed its length asks for; no crest folds over.
  - Tests: `tests/sea_state_test.cpp`
  - Editor check to do: Weather > Wind > Speed from 3 to 25 m/s makes the open sea taller, longer and faster (the Waves panel shows the numbers); a lake in the
    same wind stays small; moving a body or changing its size never changes its waves (an ocean) or only its fetch (a lake).
- **O1** `[ ]` No visible repetition
  - What: the wind sea is a spectrum of at least 32 components (raise `kMaxWaves` and `WATER_MAX_WAVES` together, or move the waves out of the constant buffer into a
    structured buffer) with a directional spread, random phases per component, and no two components with the same direction and wavelength. Far bands fade by pixel footprint (already so).
  - Tests: `tests/sea_pattern_test.cpp`
  - Acceptance: over 4 km the height field has no autocorrelation peak other than at zero; the significant height and peak wavelength of W6 do not change.
- **O2** `[ ]` GGX specular and roughness from slope variance
  - What: replace the Blinn-Phong glint. `WaterBrdf.h` (pure maths, mirrored in the shader): GGX distribution, Smith visibility, Schlick Fresnel (F0 = 0.02), and a
    roughness that rises with the variance of the wave slope inside one pixel (distance, small waves).
  - Tests: `tests/water_brdf_test.cpp`
  - Acceptance: the GGX distribution integrates to 1 over the hemisphere; head-on Fresnel is 0.02 and 1 at grazing; roughness never falls with distance; sun glint energy
    does not grow when the roughness goes up.
- **O3** `[ ]` Ripples from two normal maps
  - What: replace `RippleSlope` (`ValueNoiseD`). Two tileable normal maps (made on start-up from the noise code, no asset needed) at different scales, scrolling in
    different directions and speeds, blended with reoriented normal blending, faded by distance into the roughness of O2.
  - Tests: `tests/water_normal_map_test.cpp`
  - Acceptance: the map is tileable (the left and right edges agree); the two layers never move the same way; the blended normal is unit length.
- **O4** `[ ]` Colour from absorption and scattering
  - What: water colour from coefficients per metre (absorption, scattering), replacing colour + alpha + shallow colour + fade distance. The look is a few numbers with units.
  - Tests: `tests/water_optics_test.cpp`
  - Acceptance: transmittance falls as exp(-a d); doubling the depth squares the transmittance; clear shallow water shows the bottom, deep water shows none.
- **O5** `[ ]` Shore from the world water info
  - What: shoaling (waves grow in shallow water), breaking foam and the swash line read S1 instead of a per-body map.
  - Acceptance: no foam over deep water; foam follows the shoreline of the island at every LOD.
- **O6** `[ ]` Editor check of the look
  - Acceptance: from the island's cliff and from the air the sea does not look like plastic, no pattern slides past, and the horizon has no edge.

### M4. The first island scene
- **I1** `[ ]` Build the scene
  - What: a heightmap island, the sea, a sun and a sky; one scene file; scatter on the island.
  - Acceptance: every contract rule in section 6 holds in the saved and re-opened scene.

### Not planned yet
Lakes, ponds, rivers and swamps (they return on the same water info), FFT wave cascades, caustics, spray particles, biomes, erosion and generators,
camera-relative rendering, cascaded shadows, GPU heights. Each returns to this file only when the scene needs it.

---

## 5. Patches to delete (the list T2 works from)

| Patch | Why it is a patch |
|---|---|
| `autoWaveScale`, `waveScale` | stretches the waves until the mesh can show them; a sea does not depend on its mesh (W6) |
| `Spectrum` and `Classic` wave styles | older models kept next to the wind sea |
| `waveAmplitude`, `waveFrequency`, `waveSpeed`, `waveRoughness`, `waveCount` | the wind sea ignores them |
| `WavePreset`, `WaterLook` | presets that set many numbers; a look is data, not an enum |
| `size`, `sizeZ`, `kSizeMax = 2000`, `WaterShape` | hand-sized planes; the sea covers the world (S5) |
| `meshDetail`, `lodDistance` per body | per-plane mesh budgets; one ring has one budget |
| per-body shore depth map | replaced by the world water info (S1) |
| `shoreWaves = 0` "the old look" | a switch back to an old look |
| `heightScale`, `baseHeight`, `AdoptFrom`, `MigrationStats` | the two-heights problem and its migration |

## 6. Water-terrain contract

1. Ground below `seaLevel` that is joined to the open sea has water above it.
2. Ground above `seaLevel` plus the wave margin has no water pixel; a hollow below sea level that is not joined to the sea has none either.
3. A tile that is not loaded never means deep ocean inside the world.
4. The depth the water uses equals the ground the player sees at every LOD, within the wave margin.
5. Moving or resizing the world never changes where the shoreline is.
6. Sculpting updates the shoreline when the stroke ends.
7. The sea has no visible edge: it runs to the horizon at any camera height.
