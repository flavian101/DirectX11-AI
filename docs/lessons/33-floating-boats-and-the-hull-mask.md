# Lesson 33 - A boat that floats, and water that stays out of it

## Understand

**Two different problems hide in "the boat is sinking".** The first: the boat does not follow the waves, so crests pass through its deck. The second: even at the right height, the water is a plane that runs straight through the hull, so you see waves inside the boat. A real boat keeps water out. Buoyancy fixes the first, a hull mask fixes the second.

**Your idea was the right one: clip the water out of the hull.** The standard way is not to cut the water mesh (it is a big shared grid) but to write a lid over the inside of the hull into the depth buffer before the water is drawn. The water's depth test then fails behind the lid.

**A boat needs three numbers, not fluid physics.** How high the water is under it, and how steeply the surface leans along and across it. We read the real wave height at nine points under the hull (the same waves the shaders draw), fit a plane through them, and let springs chase that plane.

## Visualize

**Picture the lid as a sheet of glass laid across the rim.** From above, every ray that would have hit the water inside the hull passes through the glass first, so the glass is nearer than the water and wins the depth test. Rays that miss the glass (water outside the walls) are unaffected. The glass is invisible: it writes depth, not colour.

**Picture the plane fit as a tray balanced on nine water heights.** The tray's height is the average, its two tilts are how it leans. The boat is glued to the tray, but through springs, so it lags and overshoots a little instead of snapping.

## Why it is built this way

**Why the convex outline of the hull part?** A boat seen from above is nearly convex, and a convex polygon is easy to fan into triangles and to shrink. It is computed once at load from the part's vertices (`HullMask::ConvexHull`), so only a handful of points are kept. The hull part defaults to the part with the biggest outline, because oars and masts have thin ones.

**Why shrink the lid (inset)?** If the lid is as wide as the outline, its edge pokes through the walls and hides a strip of water beside the boat. `InsetConvex` moves every edge inward and puts each corner where its two moved edges meet. The distance is limited so a huge inset collapses the polygon instead of turning it inside out. The inset is a fraction of the hull's width so it survives scaling.

**Why "a fraction of the model's height" for draft and lid height?** Models come in any unit. A fraction keeps the setting right when you scale the boat; the tooltip shows the world units.

**Why springs, and why that spring?** The target is where the water is; the value is where the boat is. Acceleration = `omega^2 * (target - value) - 2 * zeta * omega * velocity`. `omega` is the stiffness, `zeta` the friction: 1 settles without bounce, below 1 it overshoots like a real boat. Semi-implicit Euler (velocity first, then position from the NEW velocity) stays stable, and long frames are cut into steps of at most 1/120 s, so a hitch cannot launch the boat.

**Why smooth the slopes and not the angles?** Angles wrap and twist; slopes are plain numbers. We smooth the two slopes, then build the normal `(-slopeX, 1, -slopeZ)` and turn "up" into it with the shortest rotation (`RotationFromUp`). That turn is about the axis perpendicular to both, so it adds no twist: the boat's heading never changes when it leans.

**Why does the gizmo show the floating height?** Otherwise the gizmo would sit where you placed the boat and the boat would be elsewhere. While floating, the water and the draft own the height; you still move it in x and z.

## Setting it up on your rowboat

**1. Float on water.** Select the boat, tick Float on water. Raise Draft until the waterline sits where the hull should (start near 0.15 to 0.25 of height).

**2. Hide water inside hull.** The part chosen as hull is shown in the combo. If "Auto" picked the wrong part, pick the hull by hand.

**3. Tune the lid.** Auto lid height puts it just under the hull part's top. If you see a hole in the water BESIDE the boat, raise Lid inset. If a ring of water shows inside the rim, lower it.

## Other ways, and the trade-offs

**An authored mask mesh.** An artist models a simplified inside of the hull. It fits perfectly, also for hulls with a tapered or curved rim, but it needs a modelling step per boat. A good next step if the outline lid is not accurate enough.

**Stencil volumes.** Count front and back faces of the hull to know if a pixel is "inside". It works only for closed meshes; an open boat is exactly the case it fails.

**Real buoyancy (volume under the waterline).** Gives the right behaviour for loading and ramming, costs far more, and a visual lean-and-bob already reads as floating.

**Limits.** The lid is drawn in the camera view only (the mirror image shows no water anyway). The convex outline cannot follow a concave deck. If oars are in the same mesh as the hull they widen the outline, so split them into their own part.

## Predict, then check

1. A boat 4 units long and 1.2 wide, with a lid inset of 0.05. How far in from the outline is the lid at the middle of the long side?
2. The spring has omega = 4 and zeta = 1. Roughly how long until it has covered most of a step in the target?
3. Why must the lid sit above the water inside the hull, and why must it not stick up above the rim?

**Answers.** (1) 0.05 * 1.2 = 0.06 units (the inset is a fraction of the beam, the shorter side). (2) About 1/omega = 0.25 s per time constant, so most of the step in roughly three-quarters of a second. (3) Below the water surface it would be behind the water and win nothing. Above the rim it would hide water behind the boat as well, as an invisible hovering plate.

## Rebuild cold

Without looking: write the spring step, then write the three lines that turn a slope pair into the unit normal.
