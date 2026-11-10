# Terrain world design: big maps, ecosystems, and what we build when

This is a design note, not a lesson. It answers three questions: how big worlds are organised, whether a swamp or a town is a terrain chunk, and what a normal engine does about terrain rotation and size. The "what normal engines do" parts are general industry practice as far as I know it, not claims about any one studio's internals.

## The words, first

**Chunk.** A 32 x 32 cell square of terrain mesh. It is the unit of *drawing*: one draw call, one bounding box for culling, one level-of-detail choice. It exists today and knows nothing about what is on it.

**Tile.** A bigger square, for example 8 x 8 chunks, which is 256 x 256 cells. It is the unit of *data*: one heightfield, one paint map, one file on disk, one thing to load or unload. Today the whole terrain (up to 1024 cells, so 32 x 32 chunks) is a single tile.

**World.** A grid of tiles. Only the tiles near the player are in memory.

**Biome.** A label painted onto the ground (swamp, forest, desert). It is data about *what is here*, not geometry.

## Is a swamp, a forest or a town a terrain chunk?

**No: chunks are a performance tool, ecosystems are content.** A chunk is a square cut at fixed positions for culling and level of detail. A swamp has no such shape: its edge wanders, and one chunk can hold half a swamp and half a forest. Mixing the two ideas would mean ecosystems have square borders.

**Think in three layers on top of each other.** *Shape* is the heightfield: hills, valleys, riverbeds. *Surface* is what the ground is made of and which biome it belongs to: a paint map with one biome id per texel. *Things* are what stands on it: trees, reeds, rocks, buildings. Chunks and tiles cut across all three.

**A biome is a recipe.** It names the ground layers to paint (mud and moss for a swamp), the scatter rules (reeds and dead trees, how dense, how steep a slope they tolerate), and the mood (fog, ambient light, water level). Painting "swamp" onto a region then means: use this ground, grow these things, set this mood. The same biome can appear anywhere on the map.

**A forest is mostly scatter rules.** Ground layers plus a tree density, spacing and size range. Nothing about its geometry is special.

**A town is mostly hand-placed objects.** Buildings and props you put there yourself, plus a flattened foundation in the heightfield and a painted dirt road. It is closer to a *place* (like a dungeon entrance or a camp) than to a biome. Engines treat these as authored content that sits on the terrain, not as terrain.

## How big worlds are built

**Cut the world into tiles and stream them.** A 10 km x 10 km world at one sample per metre is 100 million samples: 200 MB as 16-bit values, far too much to hold, and far too much to draw in one go. Load the tiles within a radius of the player (on a background thread, with a budget), drop the ones beyond it, and cull by tile before culling by chunk.

**Far land is drawn cheaply.** A tile a kilometre away does not need 65,000 vertices. Distant tiles use a low-resolution copy of the heightfield, or fewer levels of the same mesh, and fade into fog. Our chunk level of detail and skirts already do a small version of this inside one tile.

**Neighbouring tiles share an edge.** A tile of 256 cells has 257 samples per side, and the last row equals the first row of the next tile. Then there is no seam, and normals at the edge can read the neighbour's height.

**Floats lose precision far from the origin.** A 32-bit float has about seven digits. If one unit is one metre, then near 10 km from the origin its smallest step is about 1 mm; near 100 km it is about 8 mm, and objects visibly jitter. The standard fix is *camera-relative rendering*: store positions as "which tile, plus an offset inside it", and subtract the camera's position before sending anything to the GPU, so the numbers the GPU sees are always small.

**Open fields and hand-built areas are different problems.** I do not know FromSoftware's internal technology, so take this as general practice. A large open world is a tile grid with streaming, as above. A game made of interconnected hand-built areas (the Dark Souls style) is mostly authored level geometry loaded as *cells*, with terrain used for part of them, not one continuous landscape. For us that suggests a world made of cells, where a cell can hold a terrain tile, hand-built objects, or both.

## Rotation and scale: what normal engines do

**Terrain is an axis-aligned grid.** Unity's Terrain ignores the rotation and scale of its transform, as far as I know. Most engines treat it as a grid on the ground plane, partly because physics height-field colliders (PhysX, Bullet, Jolt) are axis-aligned too. A rotated terrain would turn every height query into an inverse-matrix problem and every chunk bounding box into an oriented one.

**Where we are.** Rotation is now locked (lesson 25). Scale still works for now. When tiles arrive, size comes from the data (cells x metres per cell) and the scale handles go away, and the terrain's position snaps to the tile grid.

## Units, size, and what limits the terrain today

**Units.** One world unit is one metre. The editor shows the terrain's width in kilometres and its height scale in metres, and a line under "Mesh resolution" shows the real world size and metres per cell.

**Width and height are independent, and both absolute.** The heightmap picture is stretched over `width` metres, and its white is `heightScale` metres tall. Make the terrain 4x wider with the same height scale and every slope becomes 4x gentler: it looks flat, and the rock layer (which starts at a slope) vanishes. That is how engines behave (Unity's terrain has separate width, length and height; so do the exporters of World Machine and Gaea, which think in a real extent and a real height range). "Keep proportions" in the editor raises the height scale with the width for you.

**What stops it from being a big world today.**
- *The camera's far plane is 1000 m* (`Camera::kFarPlane`). Anything further is not drawn, so a terrain wider than about a kilometre is clipped. Raising it with a 0.1 m near plane and a normal depth buffer spends depth precision badly; the usual fix is a reversed-Z depth buffer.
- *One tile of at most 1024 x 1024 cells.* At 4 km wide that is 4 m per cell: coarse hills, and (lesson 29) the layer weights have one texel per cell, so layer borders get blocky too. Bigger worlds need several tiles, each at a fixed metres-per-cell, which is the "world" step of the roadmap.
- *Everything is resident.* Every chunk's vertex buffer lives on the GPU; there is no streaming.
- *32-bit floats far from the origin* (see above): needs camera-relative rendering.
- *The sun's shadow map* covers a fixed area around the camera, so shadows also need cascades at that scale.

## What we build now and what we leave for later

**Now: one tile, built like a tile.** The heightfield has its own local space and layout, new code uses no global "the terrain" singleton, and the terrain's position is the tile's world position. Nothing built in the next steps has to be thrown away when tiles arrive.

**Later: the world.** A `TerrainWorld` that owns a grid of tiles, tile files on disk, streaming, distant level of detail, camera-relative rendering and the biome map.

**Things that assume ONE terrain today.** `Scene::GetTerrain()` returns the first terrain it finds. `DemoScene` sizes the water to that terrain. The scene serializer saves a single terrain entry. All three change when tiles arrive; they are the checklist.

## The roadmap

1. **Heightfield** (done, lesson 25): the kept height grid, `GetHeightAt`, `GetNormalAt`, `Raycast`, snap to terrain, ground readout.
2. **Sculpt brushes** (done, lesson 27): raise, lower, smooth, flatten; brush ring (an ImGui overlay for now, a shader decal later); partial vertex updates; undo by stroke patches; heights saved as a 16-bit PNG.
3. **Paint layers**: a paint map blended with the existing slope and height rules. (Lesson 29 bakes those rules into a splat texture, so a painted layer is just an override applied before the upload.) Its texel size is also where the biome ids will live.
4. **Generators**: ridged noise, terracing, erosion.
5. **Scatter**: instanced placement with density, spacing, slope limits and per-chunk culling. Biome recipes drive it.
6. **Performance pass**: GPU timers first, then skipping unused layers in the shader, shared index buffers, cheaper shadow detail.
7. **The world**: tiles, streaming, distant level of detail, camera-relative rendering.
