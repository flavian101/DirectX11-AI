# Lesson 57 - The water measures the ground function, not the loaded tiles (W1, step 1)

## Understand

**The water needs to know where it is deep.** Each body bakes a small map: depth equals the water level minus the ground height at every texel. Shading, foam, shoaling and the decision which water tiles to draw all read that map, so a wrong depth shows up as ocean where there should be land.

**It measured the wrong thing.** The map was baked from the tile meshes that happened to be loaded, and a texel with no loaded tile got "100 m deep". A tile still streaming in, a tile being replaced and the gap between them all read as open ocean. Worse, the bake started again every time a tile loaded or unloaded.

**The world already has the right answer.** Decision 3 says the ground is a function of the world position: source, shape, edits, rivers. That function answers for every point, loaded or not. Tiles are only a cache of it for drawing.

## How it works

**A grid made from the function.** `BuildWorldGroundGrid` fills a grid of heights in metres over the body's footprint, on the world's own lattice, with rivers dug in. At full detail a sample is exactly the height of a tile vertex, so the water sees the ground the player sees. A very large body gets a step that is a whole multiple of the lattice, never more than 1024 samples across.

**Beyond the edge there is no ground.** A bounded world marks the samples outside its tiles as having none, and the grid's bilinear read refuses a point if any of its four samples is missing. That is the only place the map still says "deep ocean".

**The stamp follows the world.** `TerrainWorld::GroundRevision` moves when the settings change, a river is added or removed, a stroke finishes, an edit is undone or loaded, and when the source is rebuilt. It does not move on every sculpt step, which fits lesson 55, and it does not move when a tile streams in. `Scene::Update` leaves a world's tiles out of the ground list and stamps by the revision.

**Single terrains are unchanged.** A terrain that is not part of a world still answers from its own heightfield.

## What the test shows

**The depth map agrees with the ground function.** Across 15,376 texels inside the test world the map is within 0.21 m of the function, with water where the ground is below the level and none where it is above, about half of each. Beyond the world's edge every texel is deep, and inside it none is.

**The grid equals the tile vertices.** 676 vertices of four tiles, with and without a river: a difference of 0.000000 m. Two bodies, one moved, turned and resized, agree on the shoreline to 0.05 m.

**One suspect is measured, not fixed.** A coarse tile is drawn from fewer samples. On gentle hills with 4 m cells the mesh stands up to 0.7 m off the true ground at level 1, 1.8 m at level 2 and 3.1 m at level 3, above and below. Rugged ground and a tall height scale give more, and that is a candidate for ocean showing through a ridge.

**Not checked: the editor.** Fly around a world with a sea body, also while tiles stream in. If the ocean still shows under land, force every tile to level 0 and see whether it vanishes.

## Check yourself

1. Why does `Scene::Update` leave a world's tiles out of the water's ground list instead of adding them as well?
2. A river is added across a body that is already baked. Which number changes, and what makes the water measure again?
3. The grid and the mesh agree at lattice points. Between them they differ slightly. Why can that never be exactly zero, and which level of detail makes it largest?
