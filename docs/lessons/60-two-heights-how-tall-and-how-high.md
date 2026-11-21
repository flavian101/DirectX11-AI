# Lesson 60 - Two heights: how tall and how high (H1, C3 step 1)

## Understand

**"Height" meant two things.** The world inspector had one slider, "Height (m)". It says how *tall* the ground is: a ground value of 1.0 stands that many metres above a value of 0.0. A person looking at a terrain can just as well ask how *high* it starts: at what Y does the lowest ground sit? The slider cannot answer that, and nothing else could either. The lowest ground was always at Y = 0.

**The old terrain had both.** A single `Terrain` has a height scale *and* a position, and the position has a Y. The default terrain stands at -2 m. A world has only the first.

**Migration lost the second one.** `WorldSettingsFromTerrain` copied the old terrain's X and Z and its height scale, and left its Y behind. In the demo scene the terrain is at y = -2, the water at y = 4 and the cube at y = 4. After migrating, the ground came out 2 m higher than it had been, against all three, and the shore and snow rules (which are world Y) moved with it.

## How it works

**One formula.** A ground value g stands at world Y = `baseHeight` + g x `heightScale`. `heightScale` is the extent (the editor's **Hill height**), `baseHeight` is the placement (**Ground level**). Decision 13 in the roadmap says that nobody else adds or subtracts the base.

**Tiles carry it in their position.** A tile is made at `position.y = baseHeight`. Everything a tile already did with world Y read `position_.y`: its bounds, the shore and snow rules baked into the splat map, picking, the ground scatter reads. They all followed without a change. The tile's own vertices stay local (0 to `heightScale`), so the mesh code, the skirts and the walls did not move at all.

**The ground function adds it.** `WorldGroundFunction` and `BuildWorldGroundGrid` return metres, and now return `baseHeight + value x heightScale`. The water's shore depth, "Snap to terrain", picking and the river readout all ask these, so they are right without knowing the base exists. The new parameter of `BuildWorldGroundGrid` comes last and defaults to 0: a caller that never heard of it gets bit for bit the old grid.

**Migration says where the old ground stood.** `HeightFrameOfOldTerrain(position.y, heightScale, scale.y)` is the old terrain's frame: base = its Y, height = its height scale times its vertical scale. A pure function, so a test can hold it against the old terrain's own query.

**The layer rules follow the frame.** The automatic shore and snow lines are fractions of the height, so they are now lifted by the base too. While doing this we found the snow line was held to 200 m by `TerrainSettings::Clamp`; above a hill height of about 277 m it stopped following the slider. `Clamp` now allows -2000 to 2000 m, and the single terrain's sliders keep their old range.

**A river measures from the ground.** The editor turned a river's water Y into a sample value by dividing by `heightScale`. That is only right when the ground starts at 0, so it now subtracts the base first.

## What the test shows

**The base lifts every height by the same amount.** At bases of -250, -2, 0, 37.5 and 800 m, with and without a river, each height equals the old height plus the base, to 0.00013 m (float precision at 800 m).

**Tall and high are separate.** Going from a hill height of 100 m to 250 m at a base of 55 m makes every height *above the base* 2.5 times larger and leaves the base where it was.

**Only height moves.** Normals are unchanged, a ray lifted by the base hits at the same distance, and the ground grid the water bakes from is the function's heights plus the base.

**Migration stands where the terrain stood.** At y -2, 0, 35 (scale 2) and -80 (scale 0.5) the world's ground equals `position.y + Heightfield::HeightAt x scale.y` to 0.00003 m. With the base left out, the test shows the world is off by exactly `|position.y|`: the 2 m bug.

**Not checked: the editor.** Drag **Ground level** with the sea at y = 4, raise **Hill height** to 500 and watch the snow line, and migrate a terrain that is not at Y = 0.

## Check yourself

1. You raise Ground level by 20 m. Which stays where it was: the water, the cube, the slope of the hills? Why does the shoreline move?
2. Why does a tile only need its `position.y` set, while the ground function needs the base added explicitly?
3. A river was carved into a world and then you doubled Hill height. Where is the channel's bottom in metres now, and where is the water body? (This is F11 in the roadmap.)
