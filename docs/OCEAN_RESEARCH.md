# How engines build oceans, and what we take from them

Research for roadmap M2 and M3 (November 2026). Everything below is paraphrased from the sources listed with it. Where a statement is standard physics or
my own recommendation rather than something a source says, it is marked **(physics)** or **(our call)**.

## 1. The systems compared

| System | Waves | Surface mesh | Shore | Foam |
|---|---|---|---|---|
| **Unity HDRP Water** | FFT, up to three frequency bands: two swell bands and one ripple band. The patch size sets how visible the repetition is (a bigger patch repeats less). | One rectangular grid per water surface. | A water mask removes swell around an island; decals place foam on the beach. | Generated from the simulation and the distant wind speed; a "smoothness" value sets how long foam lasts and whether it leaves a trail; a mask removes foam; a foam generator adds it behind waves and around islands. |
| **Crest** (open-source, Unity) | FFT or Gerstner components; a separate dynamic wave simulation for boats and splashes. | Levels of detail that scale with the camera. | A **sea-floor depth** map (the ground height under the sea) is used to damp big waves in shallows, to make shore foam, and to shade shallow water. Waves feel the seabed when the water is shallower than about half their wavelength. The seabed should fall to a great depth (hundreds of metres) away from islands, so there is no visible "step" in the waves or a line of foam. Shore waves are called "a challenging open problem": the suggested answer is a few Gerstner waves along a spline. | A **simulated foam texture** that fades over time. It is fed by (a) pinched wave crests (whitecaps), (b) shallow water, as a depth threshold, not a distance from the shore, and (c) any foam artists add. |
| **Unreal Water** | Gerstner-style waves on water bodies (not verified here). | A **Water Zone** meshes only the tiles the camera can see, plus a cheap **far-distance mesh** to the horizon kept just below the base height. | A **water-info texture** built from the water bodies *and* the landscape height; **Island** bodies keep land dry. | Shore effects read the water-info texture. |
| **Sea of Thieves** | FFT after Tessendorf (2001). Waves are the same for every player. | (not stated) | Foam where objects meet the water, found by comparing the depth buffer. | From wave folding ("chop" in the FFT) and from objects touching the water, blurred over time; the amount depends on the weather (calm, normal, stormy). Water colour blends a deep colour and a sub-surface colour using view angle, sun direction and a **wave-peak mask**. A broad specular highlight is added by art direction. |
| **Atlas** (GDC 2019) | Spectrum method evolved from Tessendorf; wakes and explosions on top. | (not covered) | (not covered) | (not covered) |
| **Godot FFT ocean** (community) | GPU compute FFT in cascades. | (not covered) | (not covered) | Foam **accumulates linearly and decays exponentially** on a texture over many updates (a grow rate and a decay rate). |
| **Frostbite** (Chalmers thesis, 2017) | Tiled deep-water heightmaps layered at several scales to hide repetition; this "is excellent at wind-driven deep-water waves but limited for other waves". | Coarse mesh tree. | Waves **refract** near the coast because their speed depends on depth; the thesis interpolates wavefront parameters instead of tiling. | (not covered) |
| **Bruneton, Neyret, Holzschuch (2010)** | Choppy-wave model; the sum of many waves gives Gaussian slopes with a known variance. | Projected grid. | (not covered) | Whitecap coverage from a wave-deformation measure that can be pre-filtered, so it filters well at any distance. |

Sources: Unity docs, [Water System simulation](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/water-water-system-simulation.html) and
[Unity's blog on the system](https://unity.com/blog/engine-platform/new-hdrp-water-system-in-2022-lts-and-2023-1);
[Crest: shorelines and shallows](https://crest.readthedocs.io/en/stable/user/shallows-and-shorelines.html), [Crest: ocean simulation and foam](https://crest.readthedocs.io/en/4.11/user/ocean-simulation.html), [Crest: waves](https://crest.readthedocs.io/en/4.23.0/user/waves.html);
Epic's [Water meshing system](https://dev.epicgames.com/documentation/unreal-engine/water-meshing-system-and-surface-rendering-in-unreal-engine) and [Water body actors](https://dev.epicgames.com/documentation/unreal-engine/water-body-actors-in-unreal-engine);
[The Technical Art of Sea of Thieves (SIGGRAPH 2018)](https://history.siggraph.org/wp-content/uploads/2022/09/2018-Talks-Ang_The-Technical-Art-of-Sea-of-Thieves.pdf) and the [Rare dev diary](https://mcvuk.com/development-news/rares-sea-of-thieves-started-with-photorealistic-water/);
[Atlas, GDC 2019](https://gpuopen.com/gdc-presentations/2019/gdc-2019-agtd6-interactive-water-simulation-in-atlas.pdf);
[GodotOceanWaves](https://github.com/sebbasim/GodotOceanWaves);
[Olsson, Chalmers 2017](https://odr.chalmers.se/server/api/core/bitstreams/9d785794-205c-4797-bb2c-e66be0accb2a/content);
[Bruneton et al., Real-time realistic ocean lighting](https://cgg.mff.cuni.cz/~jaroslav/teaching/2012-npgr031/prezentace/Realtime%20Realistic%20Ocean%20Lighting.pdf) and the [whitecaps paper](https://liris.cnrs.fr/Documents/Liris-5812.pdf).

**What they agree on.** Every large-scale game ocean above uses a **spectrum** (a sum of many waves with random phases, usually by FFT), not a handful of waves. Foam is
a **simulated, persistent texture**, not a function of the wave phase at one pixel. The **seabed** (depth) feeds three things: wave damping, foam, and colour. Shore waves
are the hard part, and nobody gets them from the open-sea spectrum alone.

## 2. How a wave behaves coming ashore

Standard coastal physics, checked against the breaker-index reference and the graphics surveys ([breaker index](https://marinespecies.org/traits/wiki/Breaker_index),
[Darles et al., survey of ocean rendering](https://arxiv.org/pdf/1109.6494)):

1. **Depth changes the wave's speed (physics).** A wave of wavenumber k has angular frequency w, with w squared equal to g k tanh(k d). In deep water
   (d larger than half a wavelength) the depth does not matter; in shallow water the speed falls to the square root of g d. The period stays the same, so the
   **wavelength shortens** as the wave comes ashore.
2. **Refraction.** Different parts of one crest meet different depths, so they move at different speeds and the crest bends; crests turn to run parallel to the
   beach. This is why waves arrive almost head-on to a coast whatever the wind did. (Frostbite thesis.)
3. **Shoaling.** In shallow water the wave gets taller (the standard result is that height grows as depth to the power of minus one quarter), steeper, with a sharper crest and a flatter trough.
4. **Breaking.** A wave breaks when its height reaches a fixed fraction of the local depth. That fraction is the **breaker index**, defined as height at breaking divided by
   depth at breaking; it is an empirical number (commonly near 0.8, **physics**) that grows with beach slope and falls with wave steepness. Gentle beaches give **spilling** breakers, steep ones give **plunging** breakers.
5. **The surf zone.** After breaking, wave height is limited by the depth (it keeps about the same height-to-depth ratio as it runs in). Whitewater is made here
   and decays behind the break line.
6. **Swash (physics).** What is left runs up the beach and back, so the waterline moves up and down with each wave; the sand it wets is darker and glossier for a while.

## 3. Foam, as a model

Combining Crest, Sea of Thieves, Godot, Unity and the whitecaps paper:

- **State:** a foam value per texel of a world-space window around the camera.
- **Sources (add to the state):** (a) wave folding, from the **Jacobian** (how much the choppy displacement squeezes the surface together: when it drops under about zero the crest
  folds) (Crest's "pinched crests", Sea of Thieves' "chop"); (b) **breaking** in shallow water, from depth and wave height, not from the phase of a single wave
  (Crest's shallow-water threshold); (c) **objects** crossing the surface, from the depth buffer (Sea of Thieves); (d) interaction later: wakes and splashes.
- **Decay:** exponential, with a rate (Godot, Crest's fade rate). The rate is the "trail" length. Foam is also carried by the current.
- **Weather:** wind speed scales the whitecaps (Unity's wind-speed dimmer, Sea of Thieves' calm, normal and stormy).
- **Look:** a bubble texture whose coverage is set by the foam value; bright, rough, little reflection.

## 4. Why a water surface looks like plastic, and the fix

- A **single sharp specular lobe** with a fixed exponent (ours is one Blinn-Phong lobe with an exponent of 128) is the classic cause. The fix used by Atlas is a
  **microfacet BRDF**: Fresnel times a normal distribution times a masking term, divided by 4 cos(light) cos(view), with **Schlick Fresnel** whose base reflectance
  comes from the index of refraction of water (about 0.02).
- Detail smaller than a pixel must stop being geometry and become **roughness**. Bruneton's approach: the waves' slopes are close to Gaussian, so their **variance**
  (summed over the waves too small to resolve, which grows with distance) feeds the BRDF's roughness. This is also what turns a field of glitter near the camera into a soft, broad reflection of the sun at the horizon.
- High specular powers on normal maps alias and flicker; **LEAN mapping** keeps the second moment of the normal in the map's filtered levels
  ([LEAN](https://www.cse.chalmers.se/edu/year/2011/course/TDA361/Advanced%20Computer%20Graphics/LEANMapping.pdf)), and there is work on temporal and spatial anti-aliasing of water reflections
  ([Podee et al.](https://link.springer.com/article/10.1007/s41095-021-0204-1)).
- **Colour is not a tint:** it comes from light scattered back out of the water (deep colour), mixed with the sub-surface colour at wave crests lit from behind
  (Sea of Thieves' wave-peak mask), and **absorption** with depth (Crest's shallow scatter colour; Unity's absorption distance).
- Small ripples come from **two normal maps at different scales moving in different directions** (Epic's Single Layer Water documentation), not one noise pattern moving with the wind.

## 5. Keeping an ocean without an edge, and without repetition

- **Mesh:** a dense mesh that follows the camera, plus a large cheap plane beyond it that reaches the horizon (Unreal's far-distance mesh; the community "ocean project" pattern),
  or concentric rings that thin out with distance ("geometry clipmap" rings; the projected grid by Johanson is the other classic answer). Either way the ocean has no size.
- **Repetition:** an FFT patch tiles, so engines add several patches at different scales (Unity's bands and patch size, Frostbite's layered heightmaps). The distance at which
  waves change scale hides the rest (a community note on the Unreal forum).

## 6. What we take (decisions for the roadmap)

1. **D7 (our call).** The sea's **waves are a spectrum in three bands** (swell, wind sea, ripples), as in Unity. Today's 16 waves is the part that repeats and
   looks like drifting structures. FFT is what every engine above uses, but our boats need the wave height on the CPU, deterministically (Sea of Thieves had to keep waves the
   same for every player). So: first a **structured buffer of 64 or more Gerstner components** over the three bands (cheap, exact on the CPU); FFT cascades only if that
   proves not enough, and then with a CPU mirror. Stated as a trade-off to confirm.
2. **D8.** The seabed is the **world's ground function**: wave damping, shore foam, colour and wave refraction all read the world water info (S1); the ground is
   given a great depth outside the world so there is no step (Crest).
3. **D9.** **Shore waves are modelled, not faked from the wind sea:** depth-dependent dispersion and shoaling, refraction toward the shore, breaking by breaker index, a
   surf zone and swash (section 2). These are pure functions of depth and the wave, tested without a GPU.
4. **D10.** **Foam is a simulation:** one world-space foam field around the camera with the sources and decay of section 3.
5. **D11.** **Shading is a microfacet BRDF with slope variance** (section 4); the single Blinn-Phong lobe is deleted.
6. **D12.** **Meshes:** a camera-following ring mesh and a far mesh, no `size` (section 5).

Not planned yet: caustics, wakes and boat interaction (the dynamic wave simulation), the underwater volume, spray particles, FFT cascades.
