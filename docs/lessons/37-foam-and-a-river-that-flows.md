# Lesson 37 - Foam you can believe, and a river that flows

## Understand

**Why the foam looked unreal.** Three separate faults, all in `water_ps.hlsl`:
1. The pattern was drawn on the world position and scrolled in ONE fixed direction, whatever the water did. Foam that slides over the waves is paint on glass.
2. It was soft round blobs with one flat opacity. Real foam is bubbles: thin at the fringe, thick in the middle, with gaps.
3. Crest foam existed only at the instant a wave folded. Real foam stays behind the crest and thins out.

**Why the river looked like a plane.** Also three faults:
1. Everything on it moved at exactly the flow speed: a rigid conveyor belt. Water drags on its bed and banks, so a real current is fast in the deep middle and slow at the edges.
2. The ripples were a texture scrolling at one speed, round, and never changing.
3. Its outline is a rectangle. **This patch does not fix that one** - see "What this patch does not do".

## Visualize

**Foam as a threshold.** Make one bubble pattern with values 0..1. Foam is the part of the pattern ABOVE a threshold, and the threshold falls as the foam amount rises. Weak foam: only the peaks poke over (lacy islands). Strong foam: almost everything is over (a sheet with a fine web). One pattern, one number, every look in between.

**Foam that stays on its water.** Gerstner waves are written in REST positions: a rest position labels a parcel of water and keeps following it as the surface moves. So drawing the pattern at the rest position makes it ride the waves. And "was this parcel squeezed half a second ago?" is simply the fold (Jacobian) at an EARLIER time at the same rest position. Three looks back, each weaker, make the trail.

**Two layers that take turns.** A river pattern is carried downstream by the local speed. A single layer would drift further and further from its start and stretch. So there are two layers half a cycle apart: each is reset to its start while it is invisible (weight 0), and the two weights always add to 1. Because the weights are not 0.5/0.5 all the time, the blend loses contrast mid-cycle; dividing by `sqrt(w0^2 + w1^2)` puts it back.

## Why it is built this way

**Why a threshold and not alpha?** Fading the alpha of a blob gives a soft smear. Cutting a pattern at a moving threshold gives real edges that thin out into lace. That is the whole trick.

**Why not only Voronoi cells?** We tried: round cells with walls read as clover-leaf confetti. Organic warped noise gives the shapes; the cells are only the fine speckle and lace on top. We tuned the pattern on the CPU first and looked at it at weak, medium and strong foam before writing the HLSL.

**Why the trail is gated.** The trail costs three extra passes over the wave table, so it runs only up close (70% of the ripple distance) and only where the surface is at least slightly squeezed right now. A parcel that folded a moment ago is still tight; a stretching surface cannot carry a fresh trail. Foam trail = 0 skips it completely.

**Why `WaterFlow.h` exists.** The speed profile, the layer weights and the trail weights are the same formulas as in the shader, in plain C++. `tests/water_test.cpp` checks them (full speed in deep middle, never below 8%, symmetric, never faster for being shallower; weights add to 1; the trail sees a crest the instant foam cannot). And gameplay can ask `Water::SampleFlow(x, z)` for the current at a spot: that is what lets a boat drift downstream later.

## What changed

- `noise.hlsli`: PCG integer hash (stays random far from the origin, where `sin` hashes break) and `Voronoi`.
- `waves.hlsli`: `WaveFold` - the Jacobian alone, for the trail.
- `water_ps.hlsl`: new foam (three sources, one pattern, threshold, thickness, trail, distance fade), river speed profile, flowing stretched ripples.
- `Water.h/.cpp`, `SceneJson.h`: `bankDrag`, `channelProfile`, `rippleStretch`, `rapids`, `foamScale`, `foamTrail` (old scenes keep defaults); per-type starting values; constants upload.
- Editor: "How it runs" under Current; Bubble size and Foam trail under Foam.

## What this patch does not do

**The river is still a rectangle.** The speed profile uses the depth map, so it follows banks that exist in the terrain, but the plane itself is straight. That needs a river that is a CURVE: carve the terrain along it, build a ribbon mesh that follows it, and store the flow direction per point. It is patch 15 in `docs/water-roadmap.md`.

**The foam trail is an approximation.** Exact foam needs memory: a texture the foam is written into and that decays. That is patch 16.

## Questions before you read the code

1. In `SpeedFactor`, what must `across` be for the middle of the river, and why does the depth term still matter when the plane is wider than the channel?
2. At `time = 0` layer 0 has weight 0 and layer 1 has weight 1. What happens at `time = kPhaseCycle / 2`, and why can the reset never be seen?
3. The foam trail looks BACK in time at the same rest position. Why would looking back at the same WORLD position give the wrong answer for a moving wave?
4. Raise "Bubble size" to 3. What happens to the pixel-size fade (`detail`) and why?
5. A boat wake needs foam to appear where no wave is squeezed. Which of the three sources would it be, and what is missing today?
