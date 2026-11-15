# Lesson 38 - A river that is a curve

## Understand

**The problem.** After lesson 37 the water MOVED like a river, but its outline was still a rectangle and its current still ran straight. A river needs the outline, the banks and the flow to all come from one thing: a curve.

**The idea.** The author places control points. The river passes through every one of them (a Catmull-Rom spline), is `width` wide at each, and runs from point 1 (upstream) to the last point. Three systems then ask the SAME question of that curve: "I am at (x, z): how far along the river am I, which side of the middle, which way does the water run here?" That question is `RiverPath::Closest`.

**Three answers, three uses.**
1. The terrain carve asks it per heightfield sample and digs a channel.
2. The flow map asks it per water texel and stores the answer for the shader.
3. `Water::SampleFlow` asks it for one point, so a boat can drift downstream.

## Visualize

**The terrain draws the outline.** We did NOT build a ribbon mesh. The water is still the same plane; it is only visible where the ground is below it. Carve a winding channel below the water level and the plane fills it: a winding river with real banks, shore foam, depth colour and dry-tile culling, all from systems that already existed. The curve only has to say where to dig and which way to flow.

**Two numbers instead of a direction.** The flow map stores `s` (distance along the river) and `d` (distance across it, signed). Ripples and foam are drawn in (d, s) coordinates, so on a bend they curve with the river. Lesson 37 drew them in a frame turned to face the flow; that frame is fine for a straight river, but on a curve the direction changes from pixel to pixel, and `dot(position, direction)` then changes by `|position| * turn` - hundreds of times faster than it should. (d, s) has no frame to turn.

**A carve is a projection.** Digging the same river twice must change nothing the second time. The bank is therefore a CAP that depends only on the distance to the middle line (it climbs `bankHeight` over `bankWidth`), never on the old ground: `new = min(ground, cap(distance))`. We first eased the bank from the old ground, and the test caught it: every click on "Carve" lowered the banks again.

## Why it is built this way

**Why Catmull-Rom?** It passes THROUGH every point, so the author drags a point to where the river goes, with nothing to guess. Béziers need handles.

**Why the water level stays flat.** The river is level, like every water body here. On flat or gently sloping ground that is exactly right. Through high ground it is a canyon, so the inspector shows how far above the water the ground is along the middle line.

**Why a per-segment bake.** Asking every texel about every piece of the curve is a few hundred million questions for a long river. `Bake` and `CarveRiver` instead walk the pieces and visit only the texels or samples inside each piece's box; the test compares the fast bake with the plain per-point question.

**What the texel holds.** `s`, `d`, and the flow direction times a strength. The strength packs two facts into one number: "is there a river here" (> 0) and "how fast across the width" (1 in the middle, 0 at the bank, with a floor of 0.1 so the direction is still known at the bank).

## What changed

- `RiverPath.h`: the curve, `Closest`, `TightestBendRatio`. `RiverCarve.h/.cpp`: the channel. `WaterRiver.h`: the flow map. All plain C++, tested in `tests/river_test.cpp`.
- `Terrain::CarveRiver`: world-space wrapper, one undo patch like a stroke.
- `Water`: `riverPath` (saved with the scene), flow-map bake and upload (slot t17), `BodyToWorld`/`WorldToBody`, `SampleFlow` follows the curve.
- `water_ps.hlsl`: `RiverFrame` reads the map; ripples, foam and speed use it.
- Editor: "River curve" under Current. Draw in viewport (click = add, drag = move, right-click = delete, Esc = done), point list, Reverse, Fit plane to river, Carve channel, a warning for tight bends.

## How to use it

1. Add a water body of type River, put its level at the height of the ground where the river should run.
2. Current > River curve > Draw in viewport. Click along where the river should go.
3. Fit plane to river. Carve channel into terrain. Ctrl+Z undoes the carve; edits to the points undo like any other setting.

## What this patch does not do

**Waves still travel straight.** The vertex shader's swell runs in the body's flow direction, not along the curve. River waves are small (the River type's amplitude is low), and the ripples carry the look, but a strong swell on a winding river will not follow the bends.

**Tight bends pinch.** Where the bend radius is smaller than the half width, "across" and "along" fold over on the inside. The inspector warns; space the points out.

**The ends are round.** Past the first and last point the nearest point is the end itself, so ripples turn into circles there. End the river at the plane's edge, or in a lake.

**The river is level, not sloped.** A river that falls along its length (and waterfalls where it steps) is roadmap item 20.

## Questions before you read the code

1. Why does `CarvedHeight` take the MINIMUM of the old ground and the cap, and what would break if it eased the bank from the old ground?
2. In `Bake`, why must the box around each piece of the curve grow by `1.8 * halfWidth` and not just `halfWidth`?
3. Two parts of a river run side by side, 10 units apart, each 12 wide. What does the nearest-point question return between them, and what does that do to (d, s)?
4. The shader falls back to the straight frame where the map has no data. Where on the plane is that, and why is a seam there acceptable?
5. `SampleFlow` and the shader must agree on the speed. Which numbers are the same in both, and which test checks one of them?
