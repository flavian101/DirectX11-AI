# Terrain, World, River and Waterfall Roadmap

Status: draft. Companion to `docs/terrain-world-design.md` (the design stays
as written there; this file is the ordered plan, the parity checklist and the
improvement backlog).

## Goal

Retire the single `Terrain` object. Replace it with a **bounded world made of
tiles**, where everything the old terrain could do also works on the world.
Rivers and waterfalls are carved into the world, and edits (sculpt, paint) are
stored per tile.

## Non-negotiable rule: parity before removal

The old terrain is deleted only when every row below works on a world.
Verify each row against the current `Terrain` code before ticking it.

| Old terrain feature                          | World equivalent                              | Done |
|----------------------------------------------|-----------------------------------------------|------|
| Heightmap from a picture                      | Picture as a world *source*                   | [~]  |
| Noise on the heightmap (strength, scale, octaves, seed) | Detail noise in the ground shape, in metres | [~] |
| Material and layer images of the terrain      | Carried by a migrated world (`customMaterial`) | [~] |
| Migration of an old scene's terrain           | *Add > Terrain world from this terrain (migrate)* | [~] |
| Height scale in real units (km / metres)      | World size in metres, metres-per-texel, max height in metres | [~] |
| Chunking / LOD                                | Per-tile chunks, LOD across tile borders      | [x] (`world_grid_test`) |
| Automatic texturing (height / slope rules)    | Same rules, evaluated per tile                | [x] (`world_grid_test`: world noise, no seams) |
| Splat-map painting (RGBA, 4 layers)           | Per-tile paint on the world lattice, seamless | [~] |
| Sculpt brushes (raise, lower, smooth, flatten)| Brushes on the world lattice, across tiles    | [~] |
| Undo / redo                                   | Before/after of the touched lattice points    | [~] |
| Collision / height queries                    | `World::HeightAt(x, z)` across tiles          | [ ] |
| Water level interaction                       | Water reads world height, not terrain         | [ ] |
| Scatter (vegetation, rocks)                   | Scatter over every tile                       | [ ] (a one-tile world works; several tiles: not yet) |
| Save / load in scenes                         | Settings in the scene, edits in a `.worldedits` file | [~] |

## Phases

### Phase 1 - Sources and bounded worlds
- A *source* produces heights: picture (16-bit PNG / R16 raw), later noise or a
  composition of both.
- A bounded world has a fixed size in metres, a tile size, and a tile grid.
- Rebuild the current terrain scene as a world (this is the first parity test).
- Heightmap scale is explicit: `metresPerTexel`, `minHeight`, `maxHeight`.
  Never an implicit "0..1 times some number".
- Acceptance: the existing demo terrain loads as a world and looks the same.

**Phase 1 status** (`[~]` in the table = written and unit-tested, not yet run in the editor):

- `WorldBounds` (`WorldGrid.h`): a rectangle of tiles. `PlanTileStreaming` never wants a tile outside it and unloads one that
  falls outside it. `WorldBounds::Covering` gives the tiles that cover a rectangle of metres (an exact fit does not pull in a
  second tile).
- `PictureHeightSource` (`WorldPicture.h`): a picture placed in the world by its corner, with the old terrain's sampling
  convention, so the same picture gives the same heights. `LoadHeightImage` reads 8-bit and 16-bit images.
- `TerrainWorldSettings`: `source` (fractal or picture), `picturePath`, `pictureWidth`, `pictureOffsetX/Z`, `bounds`.
  Saved with the scene; a scene saved before this loads as the endless fractal world it was.
- Editor: Ground combo, picture browse/width/corner, bounded fractal worlds, and
  *Add > Terrain world from this terrain* (Phase 1 made the empty shell; the migrate version below carries the edits too).
- Tests: `tests/world_picture_test.cpp` (sampling, placement, shared tile edges at every level of detail, bounds, bounded
  streaming, "one tile equals the old terrain sample for sample", 16-bit PNG round trip).
- Not done in this phase: the heightmap loader still exists twice (`LoadHeightmapFile` in `Terrain.cpp` and `LoadHeightImage`);
  the copy in `Terrain.cpp` goes with the old terrain. A picture whose shape is not a whole number of tiles shows the repeated
  edge pixel in the unused part of its last tile.

**Heightmap customisation** (why a picture can look flat, and the controls for it; `src/WorldShape.*`, tests `tests/world_shape_test.cpp`):

- Levels (black / white point) with *Auto levels*, which reads the picture's real span. A picture that uses 10% of its grey range
  spans 10% of the height scale: the inspector says so and shows the typical slope in degrees.
- Curve (exponent), invert, smoothing (blur radius in pixels, removes 8-bit steps), bicubic interpolation (less faceted when a small
  picture is stretched), and detail noise in METRES (round or ridged, kept smooth in low ground). Four presets.
- All of it is a function of the world position, so tiles still meet. With the defaults the picture's own heights come out bit for bit.

**Phase 3 and 4 status** (edits, migration; `src/WorldEdits.*`, tests `tests/world_edits_test.cpp`; `[~]` = unit-tested, not yet run in the editor):

- Edits are a height delta per lattice sample and four paint weights per lattice cell, stored per tile; a sample belongs to one tile, so
  shared borders cannot disagree. The visible ground is source -> shape -> edits -> rivers, so a river is dug through sculpted ground.
- The brushes are the old terrain's maths on the lattice (tested against the old brushes sample for sample) and a stroke over a tile
  border equals the same stroke on one big tile. Strokes are one undo step; "Discard sculpting" and "Clear painting" are undoable.
- Touched tiles are rebuilt by `TerrainWorld::Stream`, nearest first (one tile's worth of work per frame). That is simple and correct but
  not as light as the old terrain's partial chunk refresh: a first optimisation target.
- Edits are saved in a `.worldedits` file beside the scene. While a world carries edits, *Metres per cell* is locked (the edits sit on it).
- MIGRATION: `WorldSettingsFromTerrain` + `TerrainWorld::AdoptFrom`. One tile as wide as the terrain (several if the terrain is wider than
  the finest cell allows), same heightmap, height scale, cell size, material, layer images and position. Sculpting, noise and painting
  become edits; rivers carved into the old heights come along as heights. The old terrain stays in the scene until the user deletes it.
  The picture's corner is snapped to the cell grid (at most half a cell, a few decimetres, from where the terrain stood).
- Not done yet: scatter over several tiles (`ScatterObject` still feeds on one terrain), a world-level `HeightAt(x, z)` for collision,
  the old terrain's material sliders on the world itself (a tile's inspector changes that tile only), the removal of the old `Terrain`
  class (after the parity table is all ticked), and the partial rebuild of tiles during a stroke.

### Phase 2 - Waterfalls on the world (roadmap 20d)
- The river carve is already part of the world, so this phase builds on it.
- Waterfall placement, surface, plunge pool and effects: see the river and
  waterfall section below.
- Acceptance: one authored river with one waterfall runs from source to sea
  without seams at tile borders.

### Phase 3 - Edits
- Sculpt and paint are stored per tile, as a *delta over the source* (not a
  rewritten copy of the source), so re-importing or changing the source does
  not destroy edits.
- Brushes touching a tile border update both tiles; shared border rows must
  stay bit-identical.
- Undo stores before/after of only the touched tiles.
- Note: sculpting terrain that has a river or waterfall must re-run the carve
  for the touched tiles only.

### Phase 4 - Scatter and migration
- Scatter runs over every tile, deterministic per tile (seed from tile id), so
  streaming a tile in and out gives the same result.
- Migration: loading an old scene converts its `Terrain` into a bounded world
  that carries its edits.
- After migration works on all demo scenes, delete the single-terrain code
  path (see the parity table, every row must be ticked).

Until Phase 4 both paths work and a scene can use either.

### Why this order (trade-off)

| Order                                | Pros                                   | Cons                                                |
|--------------------------------------|----------------------------------------|-----------------------------------------------------|
| Waterfalls before edits (chosen)     | The feature we were aiming for lands sooner | Sculpting a world's terrain waits for Phase 3  |
| Edits and migration first            | Old terrain retired sooner             | Waterfalls wait for a long stretch of editor work   |

Suggested tweak: if Phase 3 slips, still do the minimum of Phase 4 migration
(import only, no edits) right after Phase 1, so new scenes never use the old
terrain even before it is deleted.

## Suggested improvements - terrain / world

1. **Tile format with a version number from day one.** One file per tile:
   header (version, tile id, size), heights (R16), splat (RGBA8), optional
   hole mask. Migrations then become a switch on the version.
2. **Heights as R16_UNORM textures on the GPU** and compute normals in the
   shader (or a small compute pass). It saves vertex memory and makes
   sculpt cheap: update a texture region, not a vertex buffer.
3. **Seams.** Use a one-texel overlap (tile stores N+1 samples) and morph
   or stitch vertices between LOD levels (CDLOD-style geomorph) so cracks do
   not appear between neighbouring chunks of different LOD.
4. **Large-world precision.** Render camera-relative (subtract the camera
   position on the CPU in double or in tile-local coordinates) so far-away
   vertices do not jitter at several km.
5. **Streaming.** Load and unload tiles in a ring around the camera on a
   worker thread; upload to GPU on the main thread with a per-frame budget.
6. **Holes mask.** A per-tile 1-bit/8-bit mask lets you cut holes (caves,
   tunnel entrances, a dock cut-out) without changing heights.
7. **Splat scaling.** Four layers is the limit of one RGBA map. Plan for a
   second map (8 layers) or a texture-array with per-tile layer ids; keep the
   storage format flexible now.
8. **Slope-aware texturing.** Triplanar (or biplanar, cheaper) only above a
   slope threshold, so cliffs do not stretch and flat ground stays cheap.
9. **Erosion as an optional source filter.** A thermal/hydraulic erosion pass
   (compute shader) applied when a procedural source is baked gives much more
   natural slopes, and its flow output can seed river paths.
10. **Physics / queries.** Keep one authoritative CPU height-sampling function
    (bilinear on the tile data). Use it for collision, scatter placement,
    river carving and AI so they all agree with what is rendered.
11. **Editor safety.** Autosave dirty tiles only; show the tile grid and
    dirty/streamed state in a debug overlay.
12. **Tests.** A small headless test: build world from a picture, sample
    heights at tile borders from both sides, assert equality. Same for after
    a sculpt stroke that crosses a border.

## Suggested improvements - rivers

1. **Spline-based rivers** with per-point width and depth, so one authored
   path defines both the carve and the water surface.
2. **Carve profile.** Use a smooth cross-section (not a hard box) with a
   bank falloff blended with `smoothstep`, and always carve *toward* the
   water level so the river never ends up above the surrounding ground.
3. **Monotonic flow.** Validate that the river bed only goes downhill along
   the spline (clamp or warn), except at waterfalls, which are explicit
   drops.
4. **Flow map.** Bake flow direction and speed along the spline into a
   small texture (or vertex data) used for scrolling normals and foam.
5. **Surface mesh along the spline** (a ribbon with UV along the flow), not a
   flat world-sized plane, so rivers can sit at different heights.
6. **Confluences and the sea.** Blend widths and levels where two rivers
   join; fade the river surface into the global water at the shoreline.
7. **Bake per tile.** Carve and bake only the tiles the river crosses, and
   rebake only the dirty ones after an edit.
8. **Shore and bank foam** from the distance between water level and ground
   height (depth fade), which also helps the "shore waves" goal.

## Suggested improvements - waterfalls

1. **Detect or author.** A waterfall is an explicit spline segment with a
   drop height; optionally auto-suggest places where the river bed slope
   exceeds a threshold.
2. **Carve the drop.** Cut a near-vertical lip and a plunge pool below it;
   keep the cliff face stable under later sculpting (re-apply the carve for
   the touched tiles).
3. **Water sheet.** A ribbon mesh down the drop with a vertically scrolling
   normal and foam texture; add slight noise displacement so it is not a flat
   plane. Thicker and slower at the top, thinner and faster at the bottom.
4. **Plunge pool.** Radial foam ring around the impact point, animated; the
   pool surface blends into the downstream river.
5. **Mist and splash.** Camera-facing particles (or a few soft billboards) at
   the base, fading with distance; this is cheap and sells the effect.
6. **Depth fade and refraction** so the sheet does not show hard edges where
   it meets rock.
7. **Audio hook.** Emit a looping positional sound with volume tied to drop
   height and flow.
8. **LOD.** Far away, replace the sheet and particles with a simple textured
   strip; disable mist beyond a distance.

## Water issues to fix alongside (from the water roadmap)

- **Water inside boats.** Add a water-exclusion mask: render hull interiors
  into stencil/depth before the water pass (or use an authored exclusion
  volume per object) and skip water pixels inside it.
- **Distant shimmering.** Fade high-frequency wave normals with distance, use
  mipmapped normal maps, and raise roughness from normal variance
  (Toksvig/LEAN-style) so far water goes calm instead of aliasing.
- **Physics.** A full physics engine is not needed for these two issues;
  buoyancy can sample the same wave function on the CPU if boats need to float.

## Definition of done for the whole roadmap

- Every row of the parity table is ticked.
- All demo scenes load through the world path.
- Old `Terrain` class and its shader/editor code are removed.
- `docs/terrain-world-design.md` updated to describe the final state only.
