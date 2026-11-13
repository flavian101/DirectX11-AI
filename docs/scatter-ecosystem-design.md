# Scatter and ecosystem design

Where the scatter system is, where it should go, and in what order. Written after reading the code on master (`src/ScatterPlacement.*`, `src/ScatterObject.*`, `src/Terrain.*`, `src/TerrainSplat.*`, `src/TerrainPaint.*`, `editor/EditorLayer.cpp`, `src/SceneSerializer.cpp`). Lesson 36 is the first step of it.

## 1. The pipeline today (after lesson 36)

```
Terrain  --height + layer weights (splat) + surface-change log-->  ScatterGround (a read-only view)
                                                                        |
ScatterSettings (JSON, undoable) -> MakeScatterRules -> ScatterRules    |
                                                                        v
  GenerateScatter: lattice of candidates (hash of the square)
    -> ground test (layers, slope, height, clusters)   -> density dice
    -> which VARIANT? (weights x layers x slope band)   -> thinning (minDistance)
    -> ScatterInstance (32 bytes) + a variant number
                                                                        |
  per cell, per species: instances grouped by variant (GroupScatterByVariant)
  upload: ONE vertex buffer per species, variant-major then cell (PlanScatterLayout)
                                                                        |
  Draw (main view, shadow cascades, water mirror): per cell frustum cull -> per variant LOD by
  screen size -> merged runs -> DrawIndexedInstanced(StartInstanceLocation = run.start)
```

Properties worth protecting, because everything below leans on them:

1. **Placement is a pure function of (rules, ground, square index).** No state, no running random generator. Cells can be made alone, in any order, on any thread, and equal the whole.
2. **The ground is read through one narrow view** (`ScatterGround` / `ScatterSample`: layer weights, normal, height). Anything that wants to influence placement can be added as one more field of the sample.
3. **Editing is incremental.** The terrain keeps a log of changed rectangles; `ScatterObject` marks only the cells they touch; generation is time-budgeted and nearest-first.
4. **The GPU instance is 32 bytes** and stays that way.
5. **Settings are plain data, saved as JSON, undone by snapshot**, so anything added to them is saved and undoable for free.

## 2. Limitations found (and their status)

| # | Limitation | Where | Status |
|---|---|---|---|
| 1 | A species had ONE model and ONE material set | `ScatterSpeciesSettings::model`, `Species::materials` | Fixed: variants (lesson 36) |
| 2 | No per-instance choice at all: every instance of a species looked the same | `ScatterInstance` has no variant | Fixed: parallel variant array + variant-major buffer |
| 3 | The only ground input is the 4 splat layers, slope and height; no notion of region | `ScatterSample` | Open: biomes (section 4) |
| 4 | Rules are per species; no grouping, no shared defaults | `ScatterSettings::species` | Open: section 3 |
| 5 | Placement can only be steered by changing rules or painting the terrain's layers; there is no direct way to say "more here, none there" | whole design | Open: brush tools (section 5) |
| 6 | `minDistance` is one number per species | `ScatterRules` | Open: per-variant radius |
| 7 | No texture sharing between identical materials of different variants | `ScatterObject::LoadVariant` | Open |
| 8 | Past `maxInstancesPerSpecies` the cut is by buffer order, not by importance | `PlanScatterLayout` | Open, minor |
| 9 | The instanced draw (GPU side) has no automated test | `ScatterObject::Draw` | Open: needs a GPU test harness |

## 3. Species, genus, variants: does the hierarchy fit?

Short answer: **yes, but only as a two-level runtime and a three-level authoring model, and the middle level you call "genus" is what the code already calls "species".**

What each level has to *do* decides how many levels the engine needs:

| Concern | Decided at | Why |
|---|---|---|
| What mesh and textures to draw | **variant** | the draw call is (mesh, materials, level) |
| Which look an individual gets, where | **variant** (weight, layer bias, slope band) | per-candidate, cheap |
| Spacing, density, clustering, scale range, lean, sink, level-of-detail thresholds, shadows | **species** (today's `ScatterSpeciesSettings`) | one set of numbers per population |
| "Everything that is a tree", "everything that is a rock": shared defaults, one on/off, referenced by biomes | **group** (new, optional) | organisation and inheritance only |

So the hierarchy you describe maps like this:

```
Group      (your "Species":  Rocks, Trees, Ground cover)     organisation + shared defaults  [new, editor/data only]
 `- Species (your "Genus":   Granite, Mossy rock, Pine)      placement rules                 [exists: ScatterSpeciesSettings]
     `- Variant (look:       Small granite 1/2/3, Dead pine)  mesh + materials + weight/band [lesson 36]
```

The one design point that matters: **group and species are authoring concepts that compile away.** Placement and rendering only ever see a flat list of species with variants (what `ScatterObject` already iterates), so the group level costs nothing at run time and cannot slow the draw. (Unreal's PCG Biome Core also has asset hierarchies, but of a different kind: a *child* asset is spawned around its *parent's* points, a tree with its undergrowth, with weighted children and a depth limit. That is a "companion species" relation, worth adding later as `companions: [{species, count, radius}]`; it is not the same thing as grouping looks.)

Proposed data (phase 3), additive to the JSON, old files load unchanged:

```cpp
struct ScatterGroupSettings {            // "Rocks"
    std::string name;
    bool enabled = true;
    // Defaults that a species may leave unset (nullopt = inherit). Only the numbers artists tweak per family:
    std::optional<float> density, minSlope, maxSlope, shadowDistance, cullScreen;
    std::array<float, 3> lodScreen ...;
};
struct ScatterSpeciesSettings { ... int group = -1; /* index into groups, -1 = none */ ... };
```

Resolution is one function, `ResolveSpecies(group, species) -> ScatterSpeciesSettings`, run when the settings are applied, before `MakeScatterRules`. Not done in this series because it is pure organisation and the risk-free way to build it is after biomes decide what a group must reference.

## 4. Biomes

### What other tools do

- **Unreal PCG Biome Core** (an experimental plugin of the PCG framework): a *biome definition* is a name, a colour and a priority; the *region* where it applies is a biome actor, a volume, a spline or a texture (the texture matches pixels to biomes by colour); the actor lists the assets that spawn there, each with a weight and filters (density, height, distance to water); overlapping biomes are layered by priority; a blending range and blending noise soften the borders; exclusion volumes and splines remove what is under them; and landscape paint layer weights can select which assets a generator spawns, so painted layers and biomes combine. ([Epic docs: PCG Biome Core and Sample plugins](https://dev.epicgames.com/documentation/en-us/unreal-engine/procedural-content-generation-pcg-biome-core-and-sample-plugins-reference-guide-in-unreal-engine))
- **Unreal Foliage mode / Unity terrain trees and details:** biomes are implicit; the artist paints, per foliage type, with density, scale and slope/height/layer filters, and the instances are stored.
- **Common in shipped games:** a low-resolution *ecotope map* (a painted or computed texture of biome ids/weights) that the placement samples, plus rule-based classification (height, slope, moisture) for the parts nobody painted.

Three ideas recur: **a region definition is separate from what grows there**, **the region is data the placement can sample** (here a texture, volumes or splines), and **priority plus a blend range decide the borders**. Both fit this engine directly, because the placement already samples the ground per candidate.

### Proposal

```
BiomeSettings (saved with the scatter, undoable)         BiomeMap (a 2nd texture next to the splat map)
  name, color (for the overlay), priority                   one texel per terrain cell, like the splat map
  how it is found: [ rules | painted | both ]               up to 4 biomes per map as 4 weights adding to 255
    rules: height band, slope band, layer weights,          (more biomes = more maps; kMaxBiomes = 8 to start)
           noise (so edges are not straight)
  what grows:  list of { species, densityMul, overrides? }
```

Integration, in the order data flows:

1. **`ScatterSample` grows `float biome[kMaxBiomes]`**: the weights of the biomes at the point. They come from (a) the painted `BiomeMap`, bilinear like the splat, and/or (b) the biome's rules evaluated on the sample's own height/slope/layers. Blending between biomes is then free: weights just overlap.
2. **`ScatterRules` grows `float biomeDensity[kMaxBiomes]`**: how much THIS species wants each biome (0 = not there). `MakeScatterRules` builds it by inverting the biome tables: for species S, `biomeDensity[b] = densityMul of S in biome b`, 0 if absent. A species that appears in no biome at all keeps today's behaviour (all ones), so old scenes are unchanged.
3. **In `Evaluate`, the density chance is multiplied by `sum_b(sample.biome[b] * rules.biomeDensity[b])`**, next to where `layerDensity` is applied today. A candidate in a forest biome gets pines; the same square in a rocky biome gets boulders; at the edge both, in proportion. Per-biome overrides (e.g. smaller scale in a river edge) are a second step: pick the dominant biome and apply its override struct.
4. **Invalidation** reuses the surface-change log: painting biomes reports its rectangle like painting layers does, `MarkDirty` already knows what to do.
5. **Editor**: a biome list in the scatter inspector (like species), a biome overlay with the brush ring, and the same brush that paints layers (section 5).

Why not "one scatter object per biome"? It would multiply cells, buffers and draw calls, and make blending impossible. Why not store biome id per instance? Placement must stay a pure function of the ground; the biome is part of the ground.

### Phases for biomes

- **B1, rule-based only (no new storage).** `ScatterSample.biome[]` computed from height/slope/layers/noise. Lowest risk: no terrain file format change; testable like `TestLayersDriveIt`.
- **B2, painted `BiomeMap`.** Save format (a PNG next to the splat pack, as the splat map is stored), undo (reuse the paint stroke before/after tiles), editor overlay.
- **B3, transitions and exclusion.** Blend ranges, noise on edges, exclusion zones (roads, buildings) as a mask that multiplies everything to 0.

## 5. Brush painting

### Two honest options

| | **A. Density-mask painting** | **B. Direct instance painting** |
|---|---|---|
| What the brush edits | a per-species 8-bit mask (same resolution as the splat map) that multiplies density | a stored list of instances |
| Erase | mask to 0 | delete instances in the radius |
| Fits "pure function of the ground" | **yes**: the mask is part of the ground | no: stored instances are state next to the generator |
| Survives terrain edits | yes (placement re-runs) | instances float or must be re-snapped |
| Exact control (put THIS tree HERE) | no | yes |
| Cost | one more field in `ScatterSample`; undo reuses paint tiles | new storage, new save format, merge into cells, undo by instance lists |
| Live feedback | automatic: dirty cells regenerate nearest-first inside the time budget | must be written |

**Recommendation: A first.** It reuses the existing brush, the existing undo, the existing dirty-cell regeneration and keeps every property of section 1. It covers "paint trees here, erase them there, more density, less density". B (a hybrid: a manual instance list per cell merged with the generated ones) is worth doing later for hand-placed hero objects, and fits the same cell and buffer structure.

### Brush features mapped

| Feature | In A |
|---|---|
| Paint trees / rocks | the active species' mask goes towards 1 under the brush (candidates get a chance because the mask raises the effective density; a mask value above 1, up to 2, can exceed the species' density) |
| Erase | mask towards 0 for the active species, or for all species |
| Brush size, falloff | the existing brush ring and falloff curve |
| Density (strength) | how fast / how far the mask moves per stroke step |
| Randomization | per-candidate jitter of the mask test (a hash, so repeatable), plus an optional scale/lean jitter that the brush multiplies into the instance |
| Live feedback | cells under the brush regenerate every frame within `generationBudgetMs`; the brush ring is drawn like the layer-paint brush; an optional count readout from `GetVariantInstanceCount` |

Editor integration: the layer-paint tool already owns the pointer-to-terrain ray, the brush ring, the stroke begin/end and the undo entry. The scatter brush is a new *target* of that tool (layer / biome / scatter-species mask) rather than a new tool.

## 6. Phases, lowest risk first

| # | Phase | Risk | Why that risk |
|---|---|---|---|
| 1 | **Variants** (lesson 36) | low | additive JSON, one new roll in a pure function, upload/draw restructure with tested bookkeeping |
| 2 | Built-in rocks, starter scatter with looks | very low | data and procedural meshes |
| 3 | Groups with inherited defaults | low | settings and editor only; compiles away |
| 4 | **B1 rule-based biomes** | low-medium | touches `ScatterSample` and rules, but no storage and fully testable |
| 5 | **Density-mask brush (A)** incl. erase | medium | new per-species texture, new save format, editor tool; reuses undo and dirty cells |
| 6 | **B2 painted biome map** | medium | same machinery as 5, one more map |
| 7 | Per-variant radius, texture cache | low-medium | placement and loading detail |
| 8 | B3 transitions, exclusion masks | medium | needs 4 and 6 in use first |
| 9 | Direct instance painting (B) | high | new state beside the generator, snapping, merge, undo |
| 10 | GPU-driven culling, texture-array variants | high | a different render path; only worth it when draw calls or CPU culling show up in the profiler |

## 7. Performance

- **Placement**: one extra hash and at most 8 weights per *accepted* candidate. Measured: 1 km^2 at 4 m spacing, 46,416 instances in about 20 ms before and after (`ScatterPlacementTests`).
- **Draw calls** per species = variants x levels x mesh parts x runs. Runs per variant are as few as before because the buffer is variant-major. Budget variants like draw calls: 3 to 4 per tree species (which usually has 2+ mesh parts) and up to 8 for rocks (1 part) is comfortable. The shadow pass repeats the count, one level coarser.
- **Culling** is per cell (unchanged cost); only the level choice is per cell and variant (a few multiplies each; a few hundred cells times at most 8 variants).
- **GPU memory**: a model shared by several variants is stored once; each variant's materials are its own (identical textures are loaded again, see limitation 7). The instance buffer is the same size as before: 32 bytes per instance, independent of variant count.
- **Biomes (B1)**: a handful of multiplies per candidate. **B2** adds one texture sample per candidate (CPU bilinear fetch, like the splat map).
- **Density mask (A)**: one extra byte fetch per candidate; regeneration under the brush is the same cost as a sculpt stroke today.
- **DX11 ceiling**: no bindless textures, so variants that differ by texture cannot share a draw call without a `Texture2DArray` (phase 10). Below a few dozen draw calls per species this does not matter.

## 8. What is implemented

Phases 1 and 2, with tests (`ScatterPlacementTests`, `ScatterSettingsTests`, `ProceduralModelsTests`). Phases 3 to 10 are designed above, not written.
