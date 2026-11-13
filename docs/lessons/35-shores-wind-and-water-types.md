# Lesson 35 - Shores, one wind for the scene, and water that shows only what matters

## Understand

**What you saw at the shore was a real gap.** Depth changed how TALL a wave was (it grows in shallow water, then breaks) but never which way it TRAVELLED. So on the windward side a wave runs at the beach and breaks, which looks right. On the lee side, the same wave runs away from the beach, yet it still swells and dies at the waterline, as if it were being pulled out of the sand. Same code, opposite look.

**Why not simply bend the waves toward the shore?** Real waves do refract that way, but a Gerstner wave has one fixed direction: its phase is `k * dot(direction, position)`. Make the direction depend on position and the phase gradient is no longer `k * direction`, so crests tear and the normals lie. Doing it properly means solving for a phase field (an eikonal equation). Too much for a game, and the CPU and shader would have to agree on it.

**What we do instead: shelter.** Waves cannot come FROM the land. A wave running away from a shore it is close to has no open water behind it to have built up in, which is exactly what the lee of an island is. So such a wave is turned down. A wave running toward the shore is untouched. Far from any shore, nothing changes.

## Visualize

**The direction field.** For every texel of the depth map we find which way the land lies. Smooth the depth over about 12 units, take its slope (depth falls toward the land), and store a unit vector toward the land times how sure we are: 0 on a flat bottom, 1 on a clear beach. It is premultiplied on purpose: at a headland two texels point opposite ways, and a plain blend would invent a direction, while the premultiplied blend cancels to "not sure".

**The rule per wave.** `dir` points to where the crests come FROM. A wave runs away from the land when `dir` points toward it, so `away = saturate(dot(dir, toward))`. Only waves that feel the bottom count: `ShallowWeight(k, depth)`, so long waves feel a shore from farther out. Factor = `1 - shelter * ShallowWeight * away`, never below 0.05.

## Why it is built this way

**Why compute the direction in WORLD space at bake time?** The body can be turned and scaled. The bake knows how a texel step moves you in the world, so it solves for the world slope once. The shader then needs no rotation maths at all: it reads three numbers and does one dot product per wave.

**Why one texture with four floats?** The shader already fetched four texels to blend the depth. Making each texel (depth, toward x, toward z, unused) gives the direction for the same four fetches. The CPU twin (`DepthField::SampleToward`) uses the same bilinear weights, so floating boats read the sheltered surface that is drawn.

**Why does shelter keep the safety guarantees?** It only multiplies the heights and the sideways pull by a number between 0.05 and 1, like the wave groups. The height bound and the no-fold-over limit assume no wave is taller than its table amplitude, and that is still true.

**Why does wind live in the scene?** Wind moves the sea, and will move grass, smoke and clouds. One setting in the scene (`Weather` in the Hierarchy) is read by all of them. A water body has a Wind choice: follow the scene wind, or an own direction. The scene's strength scales only the HEIGHT of the waves. A river ignores both, because its direction is its current. Scenes saved before this keep the direction they had (they load as "own direction").

**Why a table for what each type shows?** `WaterTypeUi.h` is one function: given a type and a control, is it shown? A river hides swell and wind direction; an ocean shows both; Custom shows everything, including the river's current; "Show all settings" overrides the table. Keeping the rule in one tested place means a new control is a one-line decision, not a hunt through the drawing code for `if (river)`.

## Other ways, and the trade-offs

**Fetch-based shelter.** March upwind from every texel and see if land blocks the way, for a true wave shadow whose length depends on how far the land extends. More correct for big islands, but the result depends on the wind direction, so it must be re-baked whenever the wind changes. The direction field only changes when the terrain does.

**Proper refraction and diffraction.** The real thing: an eikonal solve, or a wave simulation on a grid. Beautiful, expensive, and needs a different structure than a sum of waves.

**Per-type settings as separate classes.** A `RiverSettings`, `OceanSettings`, and so on gives type safety, but every shared setting (colour, foam, ripples) is then copied or inherited, and changing a type means converting data. One settings struct plus a visibility table keeps conversion trivial (Ctrl+Z restores the old type's values).

## Predict, then check

1. The wind blows toward +x. An island lies to the east of a water point. Is the land toward `+x` or `-x` from the point, and what is `dot(dir, toward)` for the wind's dominant wave? Is it damped?
2. On the far (east) side of the island, the land lies to the west. Same wave: damped or not?
3. Two neighbouring texels hold toward-land vectors `(1, 0)` and `(-1, 0)` (each fully sure). What does a point halfway between them read, and what does the shelter rule do with it?

**Answers.** (1) Land is toward `+x`. The dominant wave travels toward `+x`, so its `dir` (where the crests come FROM) is `(-1, 0)`, and `dot(dir, toward) = -1`: not away, `saturate` gives 0, no damping: it runs at the beach. (2) Land toward `-x`, `dir = (-1, 0)`: `dot = +1`, away = 1: damped by `shelter * ShallowWeight`: calm lee. (3) `(0, 0)`: "not sure". `away` is 0 for every wave, so nothing is damped there. That is why the vector is premultiplied.

## Rebuild cold

Without looking: write the factor that damps one wave given its direction, the shore sample and the shelter strength, then say which sign convention makes "running away from the land" a positive dot product.
