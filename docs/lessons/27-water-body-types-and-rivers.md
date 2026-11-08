# Lesson 27 - Kinds of water, and a river that actually flows

## Understand

**Until now every water body was an ocean patch.** A "river" was a long thin rectangle whose waves travelled one way because you set the wind to point along it. Nothing in the data said *what kind* of water it was, so nothing could behave differently.

**A water body now has a type.** `WaterBodyType` is Ocean, Lake, River or Pond. The type does two jobs: it loads a starting point that suits it (`WaterSettings::ApplyBodyType`), and for a river it switches the motion model from *swell* to *current*.

## Visualize

**Add > Water body > River**, then turn it with Rotation Y in the Inspector. Watch the "Flows toward ... deg" line under *Current*: it changes as you rotate. Now switch the Type to Ocean and back: the ripples stop riding the current, the waves start crossing each other, and your river loses its direction.

**Turn on Debug > Normals** on a river and an ocean side by side. The ocean's normals form crossing diagonal ridges. The river's form lines that all slide the same way.

## Why it is built this way

**Why not just a wind direction?** Wind is a world-space angle. Rotate the river and the wind stays where it was, so the flow no longer follows the banks. The river's flow is stored in the *body's own space* (`flowAngle`, 0 = along its +Z length) and turned into a world angle by `WaterWaves::RiverTravelDegrees(flowAngle, yaw)`. That function is plain C++ so a test can check it against what the world matrix really does.

**Why did the ocean model look wrong on a river?** Two reasons, both visible in `WaterWaves.h`. The wave table scatters directions up to ±107° times the spread, so some waves ran sideways or upstream. And deep-water dispersion makes long waves outrun short ones (`omega = sqrt(k)`), so the pattern tore itself apart as it moved. A current carries *everything* at the same speed, so for a river `omega = k * flowSpeed`: every wave moves at exactly `flowSpeed`, and the spread is capped at 0.25.

**The ripples ride the current too.** `RippleSlope` already drifted two noise layers with the wind. It now reads `waterFlow.x` (a speed multiplier) and `waterFlow.y` (sideways wander on or off). Both are 1 for every non-river type, so existing water looks exactly as before. For a river the multiplier is `flowSpeed * detailScale / 0.35`: the shader drifts layer 1 by 0.35 *noise* units per second, and one world unit is `detailScale` noise units.

**The CPU and the GPU still agree.** `Water::SampleHeight` builds its numbers from the same table, so a boat floating on the river rides the same waves you see. The table is rebuilt when the body is rotated, because the flow direction depends on it.

**Types are data, not code branches.** `ApplyBodyType` only writes plain settings values, so a lake is still a perfectly ordinary `WaterSettings` you can tweak afterwards. The type is saved by name (`"river"`), and old scene files, which lack the field, load as Ocean.

**The inspector follows the type.** Type is the first control. A river shows a *Current* section instead of Speed and Wind direction (it has its own), and hides the Calm/Choppy/Stormy presets, which are open-water swells. Performance and Debug moved under *Advanced*. Changing the type is a normal edit, so Ctrl+Z brings the old values back.

## Other ways, and the trade-offs

**A flow map** (a texture of 2D vectors painted along the river) lets the water bend with a winding bed and slow down near banks. It needs a two-phase blend to hide the texture stretching as it advects, and a tool to paint or generate it. We kept a straight, uniform current; a spline-based river is the natural next step and would *generate* the flow map.

**Real fluid simulation** (shallow-water equations on a grid) gives eddies and wakes but costs a lot and is hard to author. Games usually fake it with flow maps and foam streaks.

## Predict, then check

1. A river has Flow direction 0 and Rotation Y = 90 degrees. Toward which world axis does it flow? (Work it out from `RiverTravelDegrees`, then check the readout.)
2. Set a river's Direction spread to 1. Why does the inspector's value not change the look much?
3. Why is the multiplier for a river's ripples `flowSpeed * detailScale / 0.35` and not just `flowSpeed`?

**Answers.** (1) Toward +X: local +Z turned by +90 degrees about Y lands on +X; the readout says 0 deg. (2) `CurrentWaveParams` caps a river's spread at 0.25, so nothing can run upstream. (3) The shader moves the noise in noise units, not world units; one world unit is `detailScale` noise units, and 0.35 is the speed the shader multiplies by.
