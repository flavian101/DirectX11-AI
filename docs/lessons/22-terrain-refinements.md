# Lesson 22 - Terrain refinements: skirts, tints, and your own images

## Understand

**What the screenshot showed.** Looking at the terrain from below you could see a lattice of hanging walls with the sky between them. Those are *skirts*: strips of triangles that hang under the edge of every terrain tile. They exist to hide a crack, but there were skirts on *every* tile edge, including the great majority where no crack can ever open.

**Why a crack opens at all.** Each 32x32 tile can be drawn at a different level of detail (lesson 14). A coarse tile skips every second or fourth vertex along its edge, so its edge is a straight line between the few vertices it keeps, while its finer neighbour's edge follows every little bump. Between the two lines there can be a thin gap, and you see through it to the sky.

## Visualize

**Turn on the terrain's Wireframe and the LOD slider.** Fly over the terrain: where two tiles of different detail meet, a skirt is drawn. Where they match, nothing is. Look at the triangle count in the terrain's stats: it is lower than before, and the walls under the terrain are gone except along the outer rim.

## Why it is built this way

**Equal neighbours share exactly the same edge vertices.** Two tiles at the same level of detail have identical edges, so no crack can exist and a skirt there is pure waste. A crack is possible only between tiles at *different* levels. So the rule is: a tile gets a skirt on an edge only if the neighbour across that edge is drawn at another level, or if the edge is the rim of the whole terrain (which is visible from the side). The rule is symmetric: both tiles cover their side of the crack, so it is never left half-covered.

**Skirts became parts of the index buffer.** Before, one index buffer held the surface and all four skirts, always drawn together. Now each level of detail holds the surface first and then the four skirts as separate *parts*, and a draw says which parts it wants. Parts that are wanted and sit side by side are merged into one draw call, so the extra control costs nothing when everything is drawn.

**The decision uses the whole picture, so it runs after the loop.** To know whether a tile's neighbour has another level, every tile's level must be known first. The draw code first decides every level, then asks `ComputeSkirtMask` per tile. That function is pure (no Direct3D), which is why it has a unit test.

**What it saves.** A skirt costs 256 triangles at full detail (against 2048 for the surface), 128 at half detail (512) and 64 at quarter detail (128). So skirts were about 11 %, 20 % and 33 % of a tile's triangles, and now most tiles carry none. That saving applies to the camera view, the sun's shadow pass and the water's reflection, because they all draw through the same code.

**Why tints looked like the wrong colours.** A tint *multiplies* a texture's colour, and the colour map blended its own colours on top (`macroInfluence`). Both were left over from when the layers were grey placeholders that needed colouring. With real coloured textures they double-colour the result. The defaults are now white tints and no colour-map blend, so each layer shows its own image; the tints and the blend are optional adjustments, and "Use original colours" resets them.

**Importing your own images.** In the terrain's *Texture files* table, click a button to choose an image for a layer's albedo, normal or ORM map; right-click a button to go back to the default. `Terrain::SetLayerMap` rebuilds only that kind's texture array, and the old one stays if the new files cannot be read. The heightmap and the colour map can be replaced the same way. Files outside the `assets` folder work, but warn you, because they will not ship with the game. The scene file stores only the images you replaced.

**16-bit heightmaps.** An 8-bit image has 256 height steps, so a gentle hill becomes a staircase. 16-bit greyscale has 65,536. The loader now accepts both.

## Other ways, and the trade-offs

**Stitching.** Instead of hanging a skirt, change the coarser or finer tile's edge triangles so the two edges use the same vertices. No extra triangles and no hidden geometry, but you need an index layout for each combination of neighbour levels. **Geomorphing** blends vertices smoothly between levels so there is no popping. **Clipmaps** and **tessellation** are the techniques big terrains use.

## Predict, then check

1. Two neighbouring tiles are both at the coarsest level. Does the edge between them get a skirt?
2. A tile is at the edge of the camera's view and its neighbour is culled. Does it draw a skirt toward the culled one?
3. You tick "Use original colours". Which two things change?

**Answers.** (1) No: same level, same edge vertices, no crack. (2) No: the neighbour is not drawn in that view, so there is nothing to crack against. (3) The four layer tints become white and the colour map's influence becomes 0.
