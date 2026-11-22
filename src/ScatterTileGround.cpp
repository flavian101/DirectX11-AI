#include "ScatterTileGround.h"
#include <algorithm>
#include <cmath>

int ScatterTileGround::TileOf(float world) const { return static_cast<int>(std::floor(world / tileMetres_)); }

void ScatterTileGround::SetBounds(bool bounded, int minX, int minZ, int maxX, int maxZ)
{
    bounded_ = bounded; minX_ = minX; minZ_ = minZ; maxX_ = maxX; maxZ_ = maxZ;
}

void ScatterTileGround::AddTile(int tileX, int tileZ, const ScatterGround& ground)
{
    if (!ground.IsValid()) return;
    tiles_[Key(tileX, tileZ)] = ground;
    const ScatterRect r = ground.Extent();
    if (!haveExtent_) { extent_ = r; haveExtent_ = true; return; }
    extent_.minX = (std::min)(extent_.minX, r.minX); extent_.minZ = (std::min)(extent_.minZ, r.minZ);
    extent_.maxX = (std::max)(extent_.maxX, r.maxX); extent_.maxZ = (std::max)(extent_.maxZ, r.maxZ);
}

bool ScatterTileGround::Sample(float x, float z, ScatterSample& out) const
{
    if (tileMetres_ <= 1e-3f) return false;
    const auto found = tiles_.find(Key(TileOf(x), TileOf(z)));   // the tile that CONTAINS the point (a shared edge belongs to the next tile)
    if (found == tiles_.end()) return false;
    return found->second.Sample(x, z, out);
}

bool ScatterTileGround::IsReady(const ScatterRect& area) const
{
    if (tileMetres_ <= 1e-3f || area.IsEmpty()) return true;
    // The tiles the half-open area touches: a point exactly on its far edge is not in it.
    const int x0 = TileOf(area.minX), z0 = TileOf(area.minZ);
    int x1 = TileOf(area.maxX), z1 = TileOf(area.maxZ);
    if (static_cast<float>(x1) * tileMetres_ >= area.maxX) --x1;
    if (static_cast<float>(z1) * tileMetres_ >= area.maxZ) --z1;
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            if (bounded_ && (x < minX_ || x > maxX_ || z < minZ_ || z > maxZ_)) continue;   // beyond the edge of the world: nothing to wait for
            if (!HasTile(x, z)) return false;
        }
    }
    return true;
}
