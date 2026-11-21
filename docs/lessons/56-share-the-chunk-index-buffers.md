# Lesson 56 - Share the chunk index buffers (T3)

## Understand

**A chunk's triangles do not depend on its heights.** A chunk of 32 x 32 cells is always the same grid of quads: the vertex numbers that form each triangle are the same whether the chunk is a flat plain or a mountain. Only the vertex positions differ.

**The terrain rebuilt them anyway.** For every chunk of every tile, `BuildTerrainModel` made three levels of detail, each a surface and four skirts, then asked Direct3D for three index buffers. A tile has 64 chunks, so that is 192 buffers and the index data behind them, again for every tile that streams in.

**One set per size is enough.** Index buffers are read-only once made, and a draw call only binds one. Many meshes can bind the same buffer, so the terrain needs one set for each chunk size it uses, almost always 32 x 32.

## How it works

**The index maths moved out, unchanged.** `BuildChunkIndexLevels` in `TerrainChunkIndices.cpp` is the old code, moved and not rewritten, so the triangles cannot change by accident. It needs no Direct3D, which makes it testable.

**A cache makes the buffers once.** `GetSharedChunkIndices` looks up the size and level count, builds the GPU buffers the first time and returns the same set after that. It keeps a mutex because tiles will be built on worker threads one day (C4).

**Meshes share by reference counting.** A Direct3D object counts its users. Each `Mesh` adds its own reference to a shared buffer and releases it when the mesh goes. The cache holds one more. The buffers are freed only when the last user lets go, so removing a tile never breaks its neighbours.

**The device must outlive nothing it should not.** The cached buffers keep the device alive, so `Direct3D11Renderer::Release` empties the cache before it releases the device. If the device ever changes, the cache drops its old buffers on the next lookup.

## What the test shows

**The shapes are pinned down.** For a 32 x 32 chunk the three levels have 6144, 1536 and 384 surface indices, and each skirt a sixth of that. Every index stays inside the vertex range for square, wide and edge-sized chunks, the last grid line is kept at every level, the surface triangles all face up and the skirts face outward.

**The old output is frozen.** A golden hash of the moved code protects the exact triangles from now on, as in lesson 52.

**Not checked: the editor.** Load a world: the terrain should look the same, with no holes at tile borders and no cracks between levels. "mesh" in the inspector's last tile build should drop.

## Check yourself

1. Why can a `Mesh` release its reference to a shared buffer while the cache still holds one, and what happens when the cache lets go first?
2. A single terrain's last chunk can be 10 x 7 cells. Does it share the 32 x 32 set? What does the cache do for it?
3. Why does the renderer empty the cache before releasing the device?
