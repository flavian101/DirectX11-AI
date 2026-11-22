#pragma once
// The ground of a WORLD as the scatter sees it (roadmap S12): the finished tiles that are loaded, side by side. Pure C++, no Direct3D.
//
// A world has no single heightfield, only tiles that come and go as the camera moves. This class holds one ScatterGround (a tile's
// heightfield and layer weights, borrowed) for each loaded tile of full detail and answers the placement's three questions from
// whichever tile a point lies in. It is a function of the POSITION: the same point gets the same answer whatever order the tiles
// were added in, and a tile that leaves and comes back gives the same answer again. That is what keeps scatter deterministic.
//
// Only tiles at full detail belong here. A tile drawn at a coarser level has a coarser heightfield and coarser layer weights, so
// trees placed on it would stand somewhere else once it sharpens. The scene leaves such tiles out, and IsReady says "not yet".
#include "ScatterPlacement.h"
#include <unordered_map>

class ScatterTileGround final : public ScatterGroundSource {
public:
    // `tileMetres`: the side of a tile in world metres. Tile (x, z) covers [x * tileMetres, (x + 1) * tileMetres) in X and Z.
    explicit ScatterTileGround(float tileMetres) : tileMetres_(tileMetres) {}

    // The world's edge, if it has one: tiles outside it are never waited for. Tile numbers, inclusive.
    void SetBounds(bool bounded, int minX, int minZ, int maxX, int maxZ);
    // A loaded tile of full detail. `ground` is copied (its pointers are borrowed: they must outlive this object).
    void AddTile(int tileX, int tileZ, const ScatterGround& ground);

    int TileCount() const { return static_cast<int>(tiles_.size()); }
    bool HasTile(int tileX, int tileZ) const { return tiles_.count(Key(tileX, tileZ)) != 0; }

    bool IsValid() const override { return tileMetres_ > 1e-3f && !tiles_.empty(); }
    ScatterRect Extent() const override { return extent_; }
    bool Sample(float x, float z, ScatterSample& out) const override;
    // Every tile of the world that touches `area` is loaded (tiles beyond a bounded world's edge do not count).
    bool IsReady(const ScatterRect& area) const override;

private:
    static std::uint64_t Key(int x, int z) { return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) | static_cast<std::uint32_t>(z); }
    int TileOf(float world) const;
    float tileMetres_;
    bool bounded_ = false;
    int minX_ = 0, minZ_ = 0, maxX_ = 0, maxZ_ = 0;
    std::unordered_map<std::uint64_t, ScatterGround> tiles_;
    ScatterRect extent_;   // the bounding box of the added tiles
    bool haveExtent_ = false;
};
