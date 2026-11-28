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
5. **Shaders are compile-checked** with DXC at shader model 6.0 (a superset of the 5.0 the engine compiles with); a shader change without a clean compile is not done.
   Everything else that does not need a GPU is a pure header with a test.
6. States, one per item: `[ ]` open, `[~]` code and unit tests landed but **not yet checked in the editor**, `[x]` checked in the editor
   (needs an `Editor:` line with the date and what was seen), `[!]` known bug. Unit tests alone never make an item `[x]`.
7. A commit that changes an item's state edits this file in the same commit and says so in its message (see Tracking).

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

**The white banding along the shore, diagnosed.** In the `ShoreDepth` debug view (red = land, green = depth up to 8 m, blue = surf) the coastline and the depth are
clean and follow the shore, so the depth map is not the cause. The blue bars are the `breaking` term, and they run in straight lines across the shallows: that is the crests
of the (at most 16) straight Gerstner waves, made to break by the shallow water. Few components with a narrow spread make straight, evenly spaced crests, and the foam
follows them. The fix is in the waves and the foam (O1, O2, O6, O8), not in the terrain.

## 2. What to borrow from Unreal (and what not)

This section is only Unreal. **The wider research (Unity, Crest, Sea of Thieves, Atlas, Godot, Frostbite, Bruneton), the physics of waves coming ashore, a foam model and the
shading fixes are in `docs/OCEAN_RESEARCH.md`**; its decisions D7 to D12 are what M2 and M3 are built from.

Checked against Epic's documentation for the Water plugin:

| Unreal does | We do |
|---|---|
| A **Water Zone** defines the region; the surface is meshed as tiles and only what the camera can see is drawn. A cheap **far-distance mesh** fills the gap to the horizon, sits just under the base height so it cannot clip or show seams. | One **camera-centred ring mesh** with a constant vertex count at any world size, plus a far mesh to the horizon (S5). The sea has no `size`. |
| Water bodies are **splines**, and an **Island** body exists only to keep land above the water (applied after the other bodies). | The ground owns the sea (decision D3). We do not copy splines: the island is the ground; connectivity decides what is ocean. |
| A **water info texture** is built from the water bodies and the landscape height; the landscape height is part of the depth the surface sees. | One **world water info raster** (depth, ocean flag, direction to shore), built from the ground function (S1). Replaces the per-body depth map. |
| **Single Layer Water**: a physically based shading model with scattering and absorption coefficients, roughness, specular and a normal; the usual water material blends **two tiled normal maps panning in different directions**. | GGX specular with roughness, absorption and scattering as coefficients, two scrolling normal maps (O3 to O5). |

Waves "bashing the island" are **not simulated**. Engines fake it from the depth: waves grow in shallow water, break, and a swash line slides on the beach.
Your `shoreWaves` / `swash` / `shoreShelter` already follow that idea; they are replaced by a real shore-wave model, swash and a foam simulation (O6 to O8). Particles are for spray, later. They are not
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
6. **Lakes, ponds, rivers and swamps are gone (confirmed).** Each is a different kind of water that needs its own design, so each comes back later, one at a time,
   on the same water info and shading. Git history keeps all of it. The editor offers one thing: **Add > Ocean**.
7. **D7 to D12 (from the research):** waves are a spectrum in three bands simulated by FFT cascades on the GPU (decided), with a CPU mirror so boats get exact heights; the seabed is the
   world's ground (waves, foam, colour and refraction read it); shore waves are modelled (dispersion, shoaling, refraction, breaking, surf zone, swash); foam is a simulated
   persistent field; shading is a microfacet BRDF whose roughness grows with unresolved wave slope; the ocean mesh follows the camera and has no size.

---

## 4. Milestones

### M0. Teardown
Each step is its own commit and leaves the build and `ctest` green. The order matters: the editor is the largest consumer, so it goes first.

Done with this roadmap: `WorldTileFile` (the baked tile file; nothing read it) is deleted with its test, and T1 and T2 have landed (below).

- **T1** `[~]` One terrain only: the world. The single terrain is gone
  - Done: `Terrain` is only a TILE of a world (no heightmap, noise, sculpt, river carving or stroke painting of its own; its public constructor is gone).
    `Scene::GetTerrain` is gone: the scene's ground queries (`GetGroundHeight`, `GetGroundNormal`, `RaycastGround`, `HasGround`) and the water's depth map ask the worlds.
    All migration is deleted: `AdoptFrom`, `MigrationStats`, `WorldSettingsFromTerrain`, `MigrateHeightsIntoEdits`, `MigratePaintIntoEdits`, `WorldGridForTerrain`,
    `HeightFrameOfOldTerrain`, the world's `customMaterial` / `material` / `layerPaths`, and the "Terrain world from this terrain" menu item. The scene file no longer
    reads or writes a standalone terrain or its sculpt and paint images. The editor's single-terrain inspector, its sculpt and paint plumbing, the terrain texture picker,
    `AddRiver` / `RemoveRiver` and the water's "Fit size to terrain" button are deleted; the brush works on the world only. The default scene is a world, an ocean, a cube
    and a sky. The terrain noise settings are gone (`noiseSeed` became `breakupSeed`: it only seeds the layer rules).
  - Kept for H7 (done since): the old picture, shape and river code, which were the world's ground at the time.
  - Consequences you will see: **scatter is off** (it read "the first terrain"; the Add scatter items are gone and an existing scatter object shows nothing) until X1 puts it
    on the world, and **the world has no panel for its look** (layer rules, tints, textures) because that lived in the old terrain inspector: H10.
  - Tests: `tests/world_edits_test.cpp`, `tests/world_base_height_test.cpp`, `tests/world_grid_test.cpp`
  - Acceptance (for `[x]`): in the editor, a new scene has a world; Add shows no single terrain; sculpt and paint work on the world and undo; the water sees the shore.
- **T2** `[~]` Water: one ocean, no patches, no other kinds of water
  - Done: `WaterBodyType`, `WaterTypeUi`, `WaterRiver`, `WaterFlow` and `WaterSelect` are deleted. `WaterSettings` is the ocean: its defaults ARE the ocean (sea level 0,
    2000 x 2000), with no type, shape, river, preset look (`WavePreset`, `WaterLook`), `autoWaveScale`, wave style (only the wind sea is left), or the amplitude /
    frequency / speed / roughness the sea ignored. `Water` lost the river flow map, the ellipse, the per-type switches and `SampleFlow`; `SeaState::ChordLength` (the
    fetch of a hand-sized lake) is gone. The editor: **Add > Ocean** is the only water, the river-drawing tools (about 520 lines) and the Type, Shape and Current
    sections are gone, and the Waves section shows only what the wind sea uses. Saved look presets (`.waterpreset`) stay: a look is a user's file, not a patch.
  - Kept on purpose, because they are REPLACED later rather than deleted: `size` / `sizeZ` / `kSizeMax` and the plane mesh (S5), `meshDetail` / `lodDistance`
    (S5), the per-body depth map (S1). `waveScale` stays too: for the wind sea it is only an exaggeration of the wave HEIGHT.
  - The water shader's river code was left for T4 (below), which has now landed. The terrain's own river code goes with H7.
  - Tests: `tests/water_test.cpp`, `tests/water_settings_test.cpp`, `tests/sea_state_test.cpp`, `tests/buoyancy_test.cpp`, `tests/water_preset_test.cpp`
  - Acceptance (for `[x]`): in the editor, Add shows only Ocean; an ocean can be added, saved, re-opened and edited; its Waves section follows the scene's wind; boats still
    float on it.
- **T3** `[~]` Scene files start again
  - Done: `src/SceneVersion.h` holds the file version (now 3) and the one rule: a scene is read at exactly that version. An older file (or one with no version) and a newer
    one each fail to open with one message that says which way it is wrong; nothing is converted and nothing is read "as far as it goes". The version-1 migration (global lights
    into light objects) is deleted. Bump the version, and say why in that header, whenever a scene file's meaning changes: the bump is the migration.
  - Consequence: every scene you saved before this commit stops opening. Rebuild the island scene (a world, an ocean, a sun, a sky). If you want to keep one, change `"version": 2`
    to `3` in the file by hand and delete its `Terrain` entry: objects of a kind that no longer exists are skipped with a warning and the rest loads.
  - Not done: the "only ever ADD to the end" comments on saved enums in `SceneJson.h` are still there. They are true of a format that keeps old files readable, which this one does not,
    so each enum can be renumbered freely once its owner is rewritten (the water ones already were).
  - Tests: `tests/scene_version_test.cpp`
  - Acceptance (for `[x]`): opening a version-2 scene in the editor shows the message and changes nothing; a scene saved now opens again.

- **T4** `[~]` Water shader: delete the river branches
  - Done: `water_ps.hlsl`, `water_vs.hlsl` and `water_material.hlsli` have no river, flow, bank, rapids, ellipse or falling-surface code. The ripples keep their old look (two layers drifting
    with the wind). The constant block lost `waterFlow`, `waterRiver`, `waterRiver2`; `waterShape` is `waterMirror` (one flag). `Water.cpp`'s mirror struct lost the same fields, `kSrvWaterFlow`
    (slot 17) is gone, and the depth map's fourth channel (the level offset of a falling river) is gone from `WaterShore::Bake` and the packed texels.
  - Checked: all 16 shaders in the shaders folder compile with DXC at shader model 6.0; the depth-map bake is covered by the test below.
  - Tests: `tests/water_test.cpp`
  - Acceptance (for `[x]`): the ocean in the editor looks the same as before this commit, in a sea with a shore and in the open sea; no shader compile error appears in the console.

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
- **H6** `[~]` Edits in metres
  - Done: the world's sculpt edits store each height delta in METRES (`WorldEdits`), and `EditedHeightSource` and `ApplyWorldHeightBrush` take the world's relief to turn them into ground units
    and back. A bump is as many metres tall whatever the relief is; change the relief and the edit keeps its metres. The edits file is version 2; a version-1 file (ground units) is refused
    with a message, not read as if it were metres.
  - Not done: the visible height is still held between 0 and 1 (the tiles' range) after the edit is added, so sculpting cannot go above `baseHeight + heightScale` or below `baseHeight`.
    The range is the one the picture was imported into, so nothing of the PICTURE is clipped; extending the range for sculpting is its own change (a headroom setting).
  - Tests: `tests/world_edits_test.cpp`, `tests/tile_patch_test.cpp`
  - Acceptance (for `[x]`): sculpt a hill, change the relief, and the hill keeps its height in metres; an edit across a tile border is continuous; an old edits file shows the message.
- **H7** `[~]` Wire the new ground into the world
  - Done: the world's ground is `RasterHeightSource` (the picture imported by H1, smoothed by H3, de-terraced by H4, averaged to the lattice) plus `DetailedHeightSource` (H5, in metres)
    and the edits, and nothing else. `src/WorldGroundSource.{h,cpp}` is new; `WorldShape` (levels, curve, invert, smoothing, valley mask, detail) and `WorldPicture` (the old reader, pyramid and auto-levels)
    are deleted, with their tests and `TerrainWorld::AutoLevels`. The world's settings have `detail` and `deterrace` instead of `shape`. `baseHeight` and `heightScale` stay, with the meaning
    H1 gave them (lowest Y and relief: the picture's lowest and highest pixel are fitted to them, so no pixel is clipped); the inspector calls them "Lowest ground" and "Relief" and shows the
    picture's true range, typical slope, share below Y = 0 and the 8-bit stairs option (H2). The world rivers are deleted too: `WorldRivers`, `RiverCarve`, `RiverPath`, `TerrainWorldSettings::rivers`,
    `TerrainTile::rivers`, `river_test`, `world_rivers_test`. The tile-height generation that lived in `WorldRivers.h` is now `src/WorldTileHeights.{h,cpp}`.
  - Not done: `pictureOffsetX/Z` stay (H8 centres the picture). The ground is still stored as a value between 0 and 1 over [`baseHeight`, `baseHeight + heightScale`], not as raw metres: the tile
    meshes clamp to that range (see H6). Scatter and placement ask the same ground function through `Scene`; scatter itself still waits for X1.
  - Tests: `tests/world_ground_source_test.cpp`, `tests/world_edits_test.cpp`, `tests/world_height_test.cpp`, `tests/world_base_height_test.cpp`, `tests/water_visibility_test.cpp`, `tests/tile_patch_test.cpp`
  - Acceptance (for `[x]`): in the editor the dark-alien picture imports with its true range, a sea-level coast has no stairs, the detail slider adds bumps of the stated height, and the Ground panel
    shows the numbers it should.
- **H8** `[~]` A new world is centred (decision D2)
  - Done: a picture is placed by its CENTRE (`pictureCentreX/Z`, 0 and 0 by default), not by its corner (`pictureOffsetX/Z` is gone), so the middle of an island is the middle of the world, whatever
    the picture's size or shape; the world's tiles are the ones covering the centred picture. The grid maths is unchanged (tile (0,0) still starts at the origin). Sea level defaults to Y = 0
    (the ocean and the world's lowest ground), so a picture whose lowest pixels are the sea floor needs a negative lowest ground, e.g. -150 m.
  - Tests: `tests/world_ground_source_test.cpp`
  - Acceptance (for `[x]`): a new world made from the dark-alien picture has the middle of the island at (0, 0); moving the centre moves the island and never the grid; the ocean at Y = 0 meets its coast.
- **H9** `[ ]` Editor check of the whole ground
  - What: import the dark-alien picture, fit it, fly around, sculpt, scatter.
  - Acceptance: flat areas appear only where the file is flat; peaks are rounded and complete; no stairs. Then H1 to H8 can become `[x]`.

- **H10** `[~]` The world's look
  - Done: the world has a **sea level** (`seaLevel`, world Y) and a **look** (`TerrainSettings look`) whose sand and snow heights are metres ABOVE THE SEA (`src/WorldLook.h`), so the beach stays at the
    shore when the relief, the lowest ground or the picture changes, and moves when the sea does. (The first automatic look put the sand at 3% of the relief above the lowest ground, nowhere near the sea.)
    The inspector has a Look section: sea level (with "Match ocean"), sand and snow height and blend, rock slope, breakup, the four layers' tiling and tints, normal, AO and roughness, and a reset. It is
    applied live: no mesh is rebuilt, only a splat map is baked again when a rule moves. `TerrainSettings` moved to its own pure header and source (`src/TerrainSettings.{h,cpp}`) so it can be tested.
  - Not done: the sea level is the world's own number for now and the ocean's level is the ocean's own (the button copies it across); S1 makes the ocean read the world's sea level and removes the button.
    The layer image files cannot be chosen per layer from the editor yet (they are looked up as `assets/Terrain/<Layer>/...`).
  - Tests: `tests/world_material_test.cpp`
  - Acceptance (for `[x]`): the beach sand follows the sea level and not a fixed Y; changing the look changes every loaded tile at once, and tiles that stream in later have it too.

### M2. The world owns the sea

- **S1** `[ ]` World water info
  - What: the ocean reads the WORLD's sea level (H10) instead of having a level of its own, and "Match ocean" goes. One raster over the world bounds, built from the ground function: depth (`seaLevel` - ground), ocean flag, unit direction toward the nearest land and the distance to it
    (refraction and surf need both). Outside the bounds the ground is a constant great depth (decision D4), and the depth falls smoothly toward it so there is no step in the waves or a line
    of foam (Crest). Streamed with the tiles. A tile that is not loaded is never deep ocean inside the world (it is "unknown", and the sea waits).
  - Tests: `tests/world_sea_test.cpp`
  - Acceptance: the depth equals the ground the player sees at every tile LOD, within the wave margin.
- **S2** `[~]` Where the sea is, and where it is not (decision D3)
  - Done: the ocean is the water below sea level that is JOINED to the open sea (`WaterShore::KeepOnlyConnectedSea`, run inside the depth bake): wet texels connected, through wet texels, to a wet texel
    on the edge of the map. A hollow below sea level with a rim all round it is dry (negative depth, so the shore direction, the foam and the see-through edge treat it as land); cut a channel to the
    sea and it floods the next time the depth map is measured.
  - Not done: exclusion boxes. The rule runs on each ocean's own depth map until S1 makes one world raster, so the edge of the ocean plane is what counts as "the open sea": an ocean plane that sits
    entirely inside a bay (its edge all land) would be dry. Until S5 removes the plane, make it reach the sea.
  - Tests: `tests/water_test.cpp`
  - Acceptance (for `[x]`): in the editor a hollow below sea level inside an island has no water; sculpt a channel from the sea to it and it fills when the stroke ends.
- **S3** `[ ]` Shore tiles at full detail
  - What: a tile whose bounds are within the wave margin of the sea level is drawn at LOD 0, so a coarse mesh cannot dip below the sea over dry land.
  - Acceptance: editor check first: force all tiles to LOD 0 and see whether the ocean-through-land artefact disappears. If it does not, the cause is elsewhere
    (skirts or the depth map) and this item changes.
- **S4** `[ ]` Seams: skirts and walls out of the water's depth
  - Acceptance: no foam band along tile seams; no walls at the world edge (decision D4).
- **S5** `[ ]` One ocean for the whole world
  - What: a camera-centred ring mesh with a constant vertex count at any world size, a far-distance mesh to the horizon just under the sea level, and a wave phase kept
    relative to the camera in double so it stays exact far from the origin. The wave bands change scale with distance (a repetition hider used by every engine in the research).
  - Tests: `tests/ocean_ring_test.cpp`
  - Acceptance: a 20 km world shows the same sea near the camera as a 200 m one; no seam where the ring meets the far mesh; no visible edge of the sea at any camera height.
- **S6** `[ ]` Editor check of the water contract
  - What: check that the shore banding (section 1) is gone once O1, O2, O6 and O8 are in, and find what the faint rectangular patch in the lower right of a world-grid shot is (a tile border, an
    unloaded tile, or the depth map's edge); if it is not, the `Foam` and `ShoreDepth` debug views are where to look next.
  - Acceptance: the contract in section 6 holds while tiles stream in, at the world edge, and while sculpting.

### M3. An ocean that looks like water
Built from `docs/OCEAN_RESEARCH.md`. Waves first (O1, O2), then light (O3 to O5), then the shore (O6, O7), then foam (O8). Each item is a pure header with a test, plus its shader
change checked with DXC.

- **W6** `[~]` A sea from the wind
  - What: the weather's wind is a real wind (a speed in m/s and a direction) and is the only wind: a water body has none of its own. The sea is a
    JONSWAP spectrum of that wind over the body's fetch (an ocean: unlimited; a lake: its own length along the wind), cut into bands, one Gerstner wave
    per band, each travelling at the speed its length asks for (`omega^2 = g k`). Nothing is scaled to fit the mesh, so the sea no longer depends on how big the body is.
  - Acceptance: over open water at 10 m/s the significant height is about 2 m and the peak wavelength 80 to 100 m, whatever the size of the body; a lake's waves stay
    short and low in the same wind; every wave of the table has the speed its length asks for; no crest folds over.
  - Tests: `tests/sea_state_test.cpp`
  - Editor check to do: Weather > Wind > Speed from 3 to 25 m/s makes the open sea taller, longer and faster (the Waves panel shows the numbers); a lake in the
    same wind stays small; moving a body or changing its size never changes its waves (an ocean) or only its fetch (a lake).
- **O1** `[~]` A sea spectrum (D7)
  - Done: `src/SeaSpectrum.h`, pure. The 2-D spectrum of the sea from `SeaState`'s own numbers: the scaled JONSWAP density turned into energy per wave vector with the depth-aware dispersion, the
    directional spread `SeaState` defines (normalised), and the TMA shallow-water correction; cut into three bands (swell 2 km patch, wind sea 250 m, ripples 30 m, meeting at 0.15 and 1.2 rad/m) and
    sampled as complex amplitudes with a seeded, order-independent Gaussian. The three bands together give `SeaState`'s significant height to within 0.6% for winds from 5 to 18 m/s.
  - Not wired into anything yet: the water still draws the 16 Gerstner waves. It is read by O2.
  - Tests: `tests/sea_spectrum_test.cpp`
  - Acceptance (for `[x]`): nothing to see on its own; it is accepted with O2. (The tests check: height agrees with `SeaState`; the amplitudes make a real sea; no energy outside a band; the same seed is the
    same sea; a stronger wind is taller; down-wind beats cross-wind; shallow water holds less and slower long waves.)
- **O2** `[ ]` FFT cascades, on the GPU with an exact CPU mirror (D7)
  - Done so far: `src/OceanFFT.h`, pure: a radix-2 2-D inverse FFT; the time step giving height, choppy displacement, slopes and the Jacobian of the displacement for one band; `Surface::HeightAt` (periodic,
    bilinear) and `SeaHeightAt` (the bands added): this is the CPU mirror and the GPU's reference. Checked: a single wave moves at exactly omega / k toward +k, deep or shallow; the sea is real, periodic and has the
    spectrum's variance; the slopes are exact; the Jacobian is 1 with no choppiness, falls as it grows and goes negative where a storm folds.
  - Also done (GPU half, step 1): `src/ComputeShader.h/.cpp` (a cs_5_0 compute shader: Direct3D 11 runs DXBC, so DXC at `cs_6_0` is the check, not the build); `src/OceanGpuPack.h`, pure: the layout the shader reads
    (h0(k) and conj(h0(-k)) side by side, omega) and `EvolvePacked`, the C++ copy of `shaders/ocean_evolve_cs.hlsl`, which writes the 8 real results (height, displacement, slopes, Jacobian derivatives) as 4 complex
    numbers in 2 float4 textures (two real results share one complex FFT). Checked: with the FFT it gives `OceanFFT::Evolve`'s sea to rounding (1e-7), in three bands, deep and shallow; no NaN anywhere; the shader
    compiles at `cs_6_0` with warnings as errors.
  - Also done (GPU half, step 2): `src/OceanGpu.h/.cpp`, the evolve pass's Direct3D 11 side: per band two structured buffers (the modes and omega), a 16-byte constant buffer and two RGBA32F textures with an SRV and a UAV each;
    `Evolve` dispatches every band; `ReadSpectra` copies a band back to the CPU (slow, for checks); `SelfCheck` runs once when the renderer starts and logs, per band, how far the GPU's spectrum is from
    `OceanGpuPack::EvolvePacked`. Not run on a GPU yet: written without the Windows SDK and only syntax-checked against stand-in headers.
  - Also done (GPU half, step 3): `shaders/ocean_fft_cs.hlsl` (one thread group per row or column, bit-reversed load into groupshared memory, log2 n stages of butterflies; compiles at `cs_6_0` with warnings as errors) and its
    C++ copy `src/OceanGpuFft.h`. Checked: the butterflies of a stage never touch an element twice; a line equals `OceanFFT::Fft1D` for 2 to 512 samples (lines longer than the group's 128 threads included); rows then
    columns equal `Fft2D`; and evolve then FFT gives `OceanFFT::Evolve`'s height, displacement, slopes and Jacobian to 1e-4 of the wave size in all three bands, deep and shallow. `OceanGpu::Transform` runs it
    (A -> work -> A, B -> work -> B), `Simulate` is Evolve then Transform, and `SelfCheck` now also compares the finished sea with `OceanFFT::Evolve`. Not run on a GPU yet.
  - Also done (GPU half, step 4): `shaders/ocean_assemble_cs.hlsl` and its C++ copy `src/OceanGpuAssemble.h`: A and B become the **displacement map** (choppiness x displacement x, height, choppiness x displacement z) for the
    vertex shader and the **slope map** (slope x, slope z, Jacobian) for the pixel shader, both RGBA16F (half floats can be filtered on every card, 32-bit float filtering is optional in Direct3D 11) with a full mip chain
    from `GenerateMips`, repeating every patch size. Checked: the chain pack, evolve, FFT, assemble gives the maps of `OceanFFT::Evolve` to 1e-4 of the wave size in all three bands; the layout, the Jacobian without
    choppiness (exactly 1) and the half-float reader (every positive value, the extremes, the sign) are tested; four deliberate bugs are caught. `OceanGpu::Assemble` runs it, `Simulate(ctx, time, choppiness)` is all
    three passes, and `SelfCheck` now also compares the maps and counts their mip levels. Not run on a GPU yet.
  - Also done (CPU mirror): `src/OceanSea.h`, pure: every band's spectrum and the heights at a time, the way a boat reads them. One FFT per band, and the two 128-mode bands share one (the packing trick), so an update is
    two FFTs (about 5 ms in an optimised build, only when something asks, once per time). Same arithmetic as `OceanFFT::Evolve`: heights agree to float rounding at any world position, deep and shallow; asking twice
    for one time costs nothing and going back in time gives the same sea; `HeightBound` = 6 sigma of the spectrum is never reached over 12 times x 160 000 points at 5, 12 and 20 m/s (the highest seen is 4.4 to 5.1
    sigma). Four deliberate bugs are caught. Known limit: it reads the height at the grid position, while the water is drawn pushed sideways by choppiness x displacement, so a boat can be off by up to that
    much along the wave (centimetres in a breeze, up to about a metre in a storm). Not wired into `Water` yet.
  - Editor check to do (compute): the log shows nine lines, "Ocean GPU evolve check", "Ocean GPU sea check" and "Ocean GPU map check", for bands 0 to 2, each ending in "ok" (a "MISMATCH" or an error line means a shader
    or a resource is wrong; the map lines also report the mip levels, 8 or 9).
  - Also done (G6b, the water draws it): `shaders/ocean.hlsli` replaces `waves.hlsli`. The **vertex shader** moves each vertex by the sum of the three displacement maps (t17 to t19), each read at a mip that matches the
    grid spacing there (`SampleLevel`); the **pixel shader** takes the normal from the three slope maps (t20 to t22) and the crest foam from their Jacobian; the maps repeat every patch (2000, 250 and 30 m). `Water`
    owns the two copies of the sea (`OceanSea` for boats, `OceanGpu` for the screen), rebuilds them only when the wind, the seed or the height scale change, runs the compute passes once per scene-clock moment
    (`SetSceneTime`, pushed by the scene next to the wind) and binds the maps for the draw only. `SampleHeight` reads the CPU copy; the margins and the dry-tile test use its 6 sigma bound (the 16-slot table's
    height bound is gone). Near a shore the displacement is limited to 0.4 of the depth with a soft clamp (`OceanSea::ShoreFactor`, copied by `ShoreLimit` in the shader; tested): an interim stand-in for O6. All
    shaders in the folder compile with DXC at shader model 6 with warnings as errors.
  - Also done (G6c, found in the editor): at choppiness 1 with a height scale of 3.34 the surface folded over itself nearly everywhere, covered the water in foam and fell to about 8 FPS. Two fixes: the height scale now
    exaggerates **height only** (the sideways push is as big as the height it comes from, so `Water` passes choppiness / max(height scale, 1) to the maps), and the foam pattern, the costliest code in the pixel shader, is
    computed only where its bubbles are big enough to show (the rest of the foam was already an even cover). To judge: the Performance panel's Water pass time at choppiness 0.55 and 1, and with foam intensity 0.
  - Interim, until the milestone closes: the per-wave shoaling, shelter, wave groups and swash lift are gone with the Gerstner functions, and the **foam trail** (crest foam lingering behind a crest) is off, because
    it read the sea at earlier times (back with O8). The Waves panel's wave count, swell, swell angle, swell length, wave groups, shelter, directional spread, swash and foam-trail sliders do nothing now (height scale,
    seed and choppiness work). They go, with `WaterWaves.h` and its test, in the clean-up step that closes O2.
  - Editor check to do (G6b): the sea moves and has no visible tile pattern over a few kilometres; choppiness 0 gives rounded waves and 1 sharp crests; the horizon is calm (no crawling or shimmer); waves flatten
    toward a beach and the water stays put at the waterline (no gaps or spikes between water and terrain); a boat floats at the height of the surface it sits on (no floating above or sinking in by more than the
    choppy sideways shift in a storm).
  - Still to do: the clean-up that closes O2 (delete `WaterWaves.h`, its test, the dead settings and their editor controls and scene fields), and the foam and shore items that replace the interim ones (O6 to O8).
  - Tests: `tests/ocean_fft_test.cpp`, `tests/ocean_gpu_pack_test.cpp`, `tests/ocean_gpu_fft_test.cpp`, `tests/ocean_gpu_assemble_test.cpp`, `tests/ocean_sea_test.cpp`
  - Acceptance: the CPU and GPU heights agree at test points; boats still float; over 4 km there is no repeating pattern; the straight surf bars and the "structures drifting" are gone.
- **O3** `[ ]` A microfacet BRDF whose roughness comes from unresolved wave slope (D11)
  - What: replace the Blinn-Phong glint. `WaterBrdf.h` (pure maths, mirrored in the shader): GGX distribution, Smith masking, Schlick Fresnel with F0 from the index of refraction (about 0.02), and a
    roughness that rises with the variance of the slopes of the waves too small to resolve at this pixel (Bruneton). `glintSharpness` is deleted.
  - Tests: `tests/water_brdf_test.cpp`
  - Acceptance: the GGX distribution integrates to 1 over the hemisphere; head-on Fresnel is 0.02 and tends to 1 at grazing; roughness never falls with distance; the sun's peak brightness falls as roughness rises.
- **O4** `[ ]` Ripples from two normal maps, filtered
  - What: replace `RippleSlope` (`ValueNoiseD`). Two tileable normal maps (made at start-up) at different scales, scrolling in different directions and speeds, blended with reoriented normal blending; the
    second moment of each (LEAN) is kept so distant ripples add roughness to O3 instead of flickering.
  - Tests: `tests/water_normal_map_test.cpp`
  - Acceptance: the map is tileable; the layers never move the same way; the blended normal is unit length; the roughness they add grows with distance.
- **O5** `[ ]` Colour from absorption and scattering, with a wave-peak mask
  - What: water colour from coefficients per metre (absorption, scattering), mixed with a sub-surface colour at wave crests lit from behind using a peak mask (Sea of Thieves). Replaces colour + alpha + shallow colour +
    fade distance + `scatter`.
  - Tests: `tests/water_optics_test.cpp`
  - Acceptance: transmittance falls as exp(-a d); doubling the depth squares the transmittance; clear shallow water shows the bottom, deep water none.
- **O6** `[ ]` Waves coming ashore (D9)
  - What: `ShoreWaves.h`, pure: (a) **dispersion with depth**, w squared = g k tanh(k d), so the wavelength shortens as the water shallows; (b) **shoaling**, height growing with falling depth; (c) **refraction**, the
    wave direction turning toward the shore with Snell's law from the depth gradient (S1); (d) **breaking**, height limited by the breaker index times the depth, spilling on gentle beaches and plunging on steep ones;
    (e) the **surf zone**, where height stays proportional to depth behind the break. Replaces `shoreWaves`, `shoreShelter` and the old shore height envelope.
  - Tests: `tests/shore_waves_test.cpp`
  - Acceptance: in deep water the speed is sqrt(g / k) and in shallow water sqrt(g d); the period never changes; a crest at an angle to the shore ends up closer to parallel; height never exceeds the breaker limit; in the editor the waves
    shorten, bend to follow the coast and break in a line that follows the depth contour, on a straight beach and around an island.
- **O7** `[ ]` Swash and wet sand
  - What: the waterline slides up and down the beach with the breaking waves, and the sand it wets is darker and glossier, fading after the water has gone (a wetness mask read by `terrain_ps.hlsl`). Replaces `swash`.
  - Tests: `tests/swash_test.cpp`
  - Acceptance: run-up grows with wave height and falls with beach slope; wetness decays at a set rate; in the editor the wet band follows the waterline and dries behind it.
  - Reported while wiring O2: the water travels very little up the land and back to the sea, and a wave dies too soon after reaching the shore (distance travelled and height do not look real). So the acceptance
    includes the run-up reaching a believable distance up the beach and a visible backwash, with the wave height decaying across the surf zone over metres, not at the waterline.
- **O8** `[ ]` Foam as a simulation (D10)
  - What: `FoamField.h`, pure: one foam value per texel of a camera-centred window that **grows from sources** (wave folding from the Jacobian, breaking from O6, objects crossing the surface from the depth buffer), **decays
    exponentially**, and is carried by the current. The window moves with the camera in whole texels so the foam does not swim. Wind speed scales the whitecaps. Replaces `crestFoam`, `crestFoamThreshold`, `foamTrail`,
    `foamIntensity`, the shoreline foam band and the per-wave `breaking` term that draws the straight bars.
  - Tests: `tests/foam_field_test.cpp`
  - Acceptance: foam halves in a set time; growth is capped; advection moves foam without changing its total; re-centring the window by whole texels keeps the values; in the editor foam follows the break line, leaves a trail
    behind a boat's wake later, and the surf bars are gone.
- **O9** `[ ]` Editor check of the look
  - Acceptance: from the island's cliff and from the air the sea does not look like plastic, no pattern slides past, the shore has a surf zone with breaking waves, foam that lingers and dies away, and a wet beach; and the horizon has no edge.

### M4. The first island scene
- **X1** `[ ]` Scatter on the world
  - What: `ScatterObject` reads a ground source instead of "the first terrain": `ScatterTileGround` (built and tested, not wired) over the loaded full-detail tiles, with one change log
    for the whole world so only the cells whose ground changed are placed again. Scatter works in a bounded world first; an endless world needs a streaming grid.
  - Tests: `tests/scatter_world_test.cpp`
  - Acceptance: trees stand on the island's tiles and stay put while tiles stream in and out; sculpting a hill moves only the plants on it.
- **I1** `[ ]` Build the scene
  - What: a heightmap island, the sea, a sun and a sky; one scene file; scatter on the island.
  - Acceptance: every contract rule in section 6 holds in the saved and re-opened scene.

### Not planned yet
Lakes, ponds, rivers and swamps (they return on the same water info), FFT wave cascades, caustics, wakes and boat interaction, the underwater volume, spray particles, biomes, erosion and generators,
camera-relative rendering, cascaded shadows, GPU heights. Each returns to this file only when the scene needs it.

---

## 5. Patches to delete (the list T2 works from)

| Patch | Why it is a patch |
|---|---|
| `autoWaveScale` (`waveScale` stays: for the wind sea it is only a height exaggeration) | stretches the waves until the mesh can show them; a sea does not depend on its mesh (W6) |
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
