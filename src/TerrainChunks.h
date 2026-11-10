#pragma once
// Which terrain chunks hold which height samples. Pure logic (no Direct3D), tested in tests/terrain_chunks_test.cpp.
//
// The terrain mesh is cut into square chunks of kTerrainChunkCells x kTerrainChunkCells cells. Chunk k along an axis
// covers the samples 32k .. 32k+32 INCLUSIVE, so a sample on a border (32, 64, ...) belongs to TWO chunks: each keeps
// its own copy of that vertex. Editing a sample therefore has to refresh both of them.
#include "HeightfieldEdit.h"
#include <algorithm>

constexpr int kTerrainChunkCells = 32;

struct ChunkRange {
    int firstX = 0, lastX = -1, firstZ = 0, lastZ = -1;   // inclusive chunk indices; empty when last < first
    bool IsEmpty() const { return lastX < firstX || lastZ < firstZ; }
};

// First and last chunk index along one axis that hold a sample in [first, last] (sample indices, inclusive).
inline void ChunkSpan(int firstSample, int lastSample, int chunkCount, int& firstChunk, int& lastChunk)
{
    // The chunk that ENDS at firstSample (when it sits on a border) counts, hence the "- 1" on the ceiling division;
    // the chunk that STARTS at lastSample counts, hence the plain floor division.
    firstChunk = (std::max)(0, (firstSample + kTerrainChunkCells - 1) / kTerrainChunkCells - 1);
    lastChunk = (std::min)(chunkCount - 1, lastSample / kTerrainChunkCells);
}

// The chunks whose vertices must be rebuilt after the samples in `edited` changed. A vertex's normal reads the sample
// on each side of it, so the affected vertices reach ONE sample past the edit; the range is widened by that much.
inline ChunkRange ChunksToRefresh(const GridRect& edited, int cellsX, int cellsZ)
{
    ChunkRange range;
    if (edited.IsEmpty()) return range;
    const GridRect vertices = edited.Grown(1, cellsX, cellsZ);
    const int chunksX = (cellsX + kTerrainChunkCells - 1) / kTerrainChunkCells;
    const int chunksZ = (cellsZ + kTerrainChunkCells - 1) / kTerrainChunkCells;
    ChunkSpan(vertices.x0, vertices.x1, chunksX, range.firstX, range.lastX);
    ChunkSpan(vertices.z0, vertices.z1, chunksZ, range.firstZ, range.lastZ);
    return range;
}
