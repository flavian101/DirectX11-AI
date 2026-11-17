# Lesson 45: a river you can draw, and a river that draws itself

## Why the old tool was hard

**Three separate problems made it feel hard to use.** None of them was about drawing skill. The click did not land under the cursor, the river needed four hand-made steps in a hidden order, and starting from nothing meant deciding every bend yourself.

**The click aimed at the wrong surface.** The mouse gives a ray: a line from the camera through the cursor. The old tool cut that ray with the flat plane at the water's level. Wherever the land is higher or lower than the water, the ray meets the plane at a different spot than the ground you are looking at. You click on a hill and the point lands somewhere beside it. This is parallax, and it is the same reason a finger held up looks shifted against the background if you swap eyes.

**The fix is to cut the ray with what you see.** `Terrain::Raycast` already answers "where does this ray first hit the ground?" (the sculpt brushes use it). The river tool now asks that first, and falls back to the water plane only when the ray hits no terrain. The overlay (centre line, banks, handles) is also drawn on the ground, so what you click and what you see agree.

## Water runs downhill, so let it

**A river is a path of steepest descent.** From a spring, look around, step toward the steepest way down, repeat. That is `RiverPath::FlowDownhill`. It needs no drawing skill, only a spring.

**Four small rules make it behave.** It looks 24 directions around. It scores each by the drop over one step and a second look at twice the distance, so a small bump is not mistaken for the end of the slope. It adds a little momentum toward its current heading, so it flows in long curves instead of zigzagging. It refuses a step onto its own earlier water, so it can never spiral on itself.

**Knowing when to stop is the hard part.** It stops at the sea height, at the edge of the terrain, or in a basin. A basin is detected two ways: a wall that keeps climbing (the total rise over a run of non-descending steps exceeds `maxClimb`), or twelve steps without finding lower ground than ever before. When that happens the river is trimmed back to the lowest point it reached. The first version did not trim, and in a bowl it overshot the bottom and spiralled outward. The unit test for the bowl caught it.

**Many tiny steps become a few handles.** The walk produces hundreds of points. They are smoothed (a three-point average, ends fixed), then thinned with Douglas-Peucker: keep the point farthest from the straight line between two kept points while it is farther than a tolerance, and split there. You end up with at most 40 control points, which you can still drag, insert and delete like hand-drawn ones.

## One order, one button

**Finish river does the three chores in the order that works.** First the water's fall is set from the ground (`FollowTerrainLevels`). Then the plane is fitted around the curve (`FitRiverPlane`). Then the channel is carved. Between the last two, `RefreshRiverFlow()` is called by hand, because the carve reads the curve in the world, and the water normally measures that only once a frame.

**Why not fold it into one undo step?** The settings change and the terrain carve use two different undo systems (an object snapshot and a terrain patch). Merging them would mean teaching one about the other. Two Ctrl+Z presses is the honest cost of keeping them separate.

## Alternatives, and why not

**Flow accumulation over the whole heightmap.** Real hydrology gives every cell a flow direction, adds up how many cells drain through each one, and calls the high-count cells rivers. It finds every river at once, and tributaries come free. It needs the depression-filling step done right, and the result is a raster you would still have to turn into curves. Walking one river from a spring is simpler and fits this tool.

**Hydraulic erosion.** Simulate rain and sediment and let the valleys carve themselves. Beautiful, slow, and hard to control: you cannot say "the river goes here".

**Painting a river mask.** Fine for art direction, but the water needs a curve (flow direction, distance along, width), and a mask has none of that.

## Check yourself

- If a click lands where you pointed, which two things must the editor know about the ray?
- Why does the walk look at a second point twice as far away?
- Why is "trim back to the lowest point" better than "stop where you gave up"?
- A river can be at most about 2000 units across (`WaterSettings::kSizeMax`). What does that tell you about a world much bigger than that?

## What is not done

**One river from one spring.** Tributaries, a river that ends in a lake, and merging two rivers are separate steps.

**The plane limit.** The water is still a rectangle at most 2000 across. A found river is capped at 1500 units for that reason. Very large worlds need the river to stop being one plane, which belongs with the world-scale lesson.
