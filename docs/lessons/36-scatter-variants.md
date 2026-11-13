# Lesson 36 - Scatter variants: one species, many looks

## Understand

**A forest of one tree looks fake.** Lesson 31 gave a species exactly one model. A hundred thousand identical boulders, all with the same stone, read as a pattern within seconds. Real ground has a few kinds of rock, each in a few sizes, some mossy, some cracked, and which one you find depends on where you are standing.

A **variant** is one *look* of a species: its own mesh, its own textures, and a small amount of placement information: how often it is chosen, and on what ground.

```
Species "Boulders"      where it grows, spacing, density, how it leans, level of detail, shadows
  |- Variant "Boulder"        mesh A, weight 3
  |- Variant "Slab"           mesh B, weight 1.5
  |- Variant "Block"          mesh C, weight 1, only on slopes >= 15 deg
  `- Variant "Mossy boulder"  mesh A (or D) with a green-grey texture set, weight 2, likes grass (x3)
```

**Choosing a look.** Every candidate that survives lesson 31's three questions (does the ground want it, is there room) rolls once more: *which look?* Each variant's weight is multiplied by

- the terrain layers under the candidate, weighted by the variant's `layerBias` (a mossy rock: 3 on grass, 0.2 on rock), and
- 0 if the slope is outside the variant's slope band (a cliff fragment: 50 to 90 degrees),

and one is picked in proportion. If no variant allows the ground (a species of only cliff fragments on a meadow), the candidate is dropped.

**The roll is a hash of the square, like everything else.** It uses its own hash stream (salt 9). So adding a fourth variant to a species changes *which look* some instances get, but never moves, rotates or resizes any instance (`TestAddingVariantsMovesNothing`). Cells can still be generated alone and equal the whole (`TestVariantsAreWeightedRepeatableAndSeamFree`).

## How it is drawn

The GPU instance is still 32 bytes, with no room for a variant number. The variant number lives on the CPU, in a parallel array that only the *upload* uses.

```
instance buffer of one species (variant-major):

  [ variant 0: cell 0 | cell 1 | cell 2 ... ][ variant 1: cell 0 | cell 1 ... ][ variant 2: ... ]
```

Cells of **one variant** are neighbours in the buffer, so the run merging of lesson 31 (`AppendRun`) still turns a whole stretch of forest into one range. A species with V variants costs V times the draw calls of one with a single variant (per level of detail and per mesh part), no more. `PlanScatterLayout` and `GroupScatterByVariant` (`ScatterPlacement.h`) do this bookkeeping with no Direct3D, and are tested, including the instance cap.

**Models are shared, materials are not.** Two variants that name the same model file share its vertex buffers (`ScatterObject::ModelAsset`, cached by name). Each variant builds *its own* materials from the model's, then applies the artist's replacements per material: diffuse, normal, ORM (occlusion/roughness/metallic), emissive, a tint, and a roughness. So three stones can be one mesh and three texture sets.

**Level of detail is per variant.** A slab and a tall block are different sizes, so from the same spot they cover different fractions of the screen and may choose different levels. The visibility test (frustum, shadow distance, mirror clip) is per cell, because the cell's box covers every variant.

## Old scene files

A species with no `"variants"` key is a species with one look made from its `"model"`, exactly as before (`TestOldSceneFilesLoad`). The editor shows such a species as it always did, plus a **Several looks...** button that moves its model into the list.

## Where it lives

| File | What |
|---|---|
| `src/ScatterPlacement.{h,cpp}` | `ScatterVariantRules`, `ChooseScatterVariant`, variant output of `GenerateScatter`, `GroupScatterByVariant`, `PlanScatterLayout` |
| `src/ScatterSettings.cpp` | settings limits, the starter scatter, `MakeScatterRules` (settings to rules): no Direct3D |
| `src/ScatterObject.{h,cpp}` | `ScatterVariantSettings`, `ScatterMaterialOverride`, `ModelAsset`, `Variant`, the variant-major upload and draw |
| `src/ProceduralModels.cpp` | `builtin:rock_flat`, `builtin:rock_tall`, `builtin:rock_mossy` |
| `src/SceneJson.h`, `src/SceneSerializer.cpp` | the new fields; model and texture paths are stored relative like every other asset path |
| `editor/EditorLayer.cpp` | the **Looks** section of the scatter inspector |

## Not done yet (and why)

- **Per-variant spacing.** `minDistance` is still one number for the species, so a huge boulder can stand closer to a neighbour than looks right. A per-variant radius that feeds the thinning is the next step.
- **Texture arrays.** Variants that share a mesh and differ only by texture could be ONE draw call with a `Texture2DArray` and a per-instance slice. Not done: it needs a bigger instance (or a second stream) and every variant's maps at one size.
- **Identical materials are built twice.** Two variants of one model with no replacements each load the model's textures. A texture cache keyed by file would share them.
- **Past the instance cap, the last variants lose first** (the buffer is variant-major). Raise the cap or thin the species.

## The tests

`ScatterPlacementTests` (variant choice, proportions, no seams, nothing moves, slope and layer steering, per-variant scale and sink, grouping and layout) and `ScatterSettingsTests` (old scene files, round trip, limits, settings to rules). `ProceduralModelsTests` covers the three new rocks. Not tested (needs a GPU): the instanced draw. Check it by eye with the *Statistics* panel: draw calls should rise by about the number of variants and instance counts per variant show in the inspector.
