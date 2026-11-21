#include "WorldEdits.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

void LatticeRect::Include(LatticeIndex x, LatticeIndex z)
{
    if (IsEmpty()) { minX = maxX = x; minZ = maxZ = z; return; }
    minX = (std::min)(minX, x); maxX = (std::max)(maxX, x);
    minZ = (std::min)(minZ, z); maxZ = (std::max)(maxZ, z);
}

void WorldEdits::Reset(int cellsPerTile)
{
    cellsPerTile_ = (std::max)(cellsPerTile, 1);
    blocks_.clear();
    ++revision_;
}

bool WorldEdits::HasHeightEdits() const
{
    for (const auto& entry : blocks_) if (!entry.second.height.empty()) return true;
    return false;
}

bool WorldEdits::HasPaint() const
{
    for (const auto& entry : blocks_) if (!entry.second.paint.empty()) return true;
    return false;
}

const WorldEdits::Block* WorldEdits::Find(LatticeIndex x, LatticeIndex z, int& localX, int& localZ) const
{
    const LatticeIndex tx = FloorDivide(x, cellsPerTile_), tz = FloorDivide(z, cellsPerTile_);
    localX = static_cast<int>(x - tx * cellsPerTile_);
    localZ = static_cast<int>(z - tz * cellsPerTile_);
    const auto found = blocks_.find(Key(static_cast<int>(tx), static_cast<int>(tz)));
    return found == blocks_.end() ? nullptr : &found->second;
}

WorldEdits::Block& WorldEdits::Touch(LatticeIndex x, LatticeIndex z, int& localX, int& localZ)
{
    const LatticeIndex tx = FloorDivide(x, cellsPerTile_), tz = FloorDivide(z, cellsPerTile_);
    localX = static_cast<int>(x - tx * cellsPerTile_);
    localZ = static_cast<int>(z - tz * cellsPerTile_);
    return blocks_[Key(static_cast<int>(tx), static_cast<int>(tz))];
}

float WorldEdits::HeightDelta(LatticeIndex x, LatticeIndex z) const
{
    int lx = 0, lz = 0;
    const Block* block = Find(x, z, lx, lz);
    if (!block || block->height.empty()) return 0.0f;
    return block->height[static_cast<size_t>(lz) * cellsPerTile_ + lx];
}

void WorldEdits::SetHeightDelta(LatticeIndex x, LatticeIndex z, float delta)
{
    if (std::fabs(delta) < 1e-7f) delta = 0.0f;
    int lx = 0, lz = 0;
    if (delta == 0.0f) {   // clearing a point that holds nothing must not create a block
        const Block* existing = Find(x, z, lx, lz);
        if (!existing || existing->height.empty()) return;
    }
    Block& block = Touch(x, z, lx, lz);
    if (block.height.empty()) block.height.assign(static_cast<size_t>(cellsPerTile_) * cellsPerTile_, 0.0f);
    block.height[static_cast<size_t>(lz) * cellsPerTile_ + lx] = delta;
    ++revision_;
}

void WorldEdits::GetPaint(LatticeIndex x, LatticeIndex z, float out[4]) const
{
    int lx = 0, lz = 0;
    const Block* block = Find(x, z, lx, lz);
    if (!block || block->paint.empty()) { out[0] = out[1] = out[2] = out[3] = 0.0f; return; }
    const size_t base = (static_cast<size_t>(lz) * cellsPerTile_ + lx) * 4;
    for (int c = 0; c < 4; ++c) out[c] = block->paint[base + c];
}

void WorldEdits::SetPaint(LatticeIndex x, LatticeIndex z, const float in[4])
{
    int lx = 0, lz = 0;
    const bool none = in[0] == 0.0f && in[1] == 0.0f && in[2] == 0.0f && in[3] == 0.0f;
    if (none) {
        const Block* existing = Find(x, z, lx, lz);
        if (!existing || existing->paint.empty()) return;
    }
    Block& block = Touch(x, z, lx, lz);
    if (block.paint.empty()) block.paint.assign(static_cast<size_t>(cellsPerTile_) * cellsPerTile_ * 4, 0.0f);
    const size_t base = (static_cast<size_t>(lz) * cellsPerTile_ + lx) * 4;
    for (int c = 0; c < 4; ++c) block.paint[base + c] = in[c];
    ++revision_;
}

bool WorldEdits::TileHasHeightEdits(TileCoord tile) const
{
    const auto found = blocks_.find(Key(tile.x, tile.z));
    return found != blocks_.end() && !found->second.height.empty();
}

bool WorldEdits::TileHasPaint(TileCoord tile) const
{
    const auto found = blocks_.find(Key(tile.x, tile.z));
    return found != blocks_.end() && !found->second.paint.empty();
}

PaintMap WorldEdits::PaintForTile(TileCoord tile, int cellsAtLod) const
{
    PaintMap map;
    const auto found = blocks_.find(Key(tile.x, tile.z));
    if (found == blocks_.end() || found->second.paint.empty() || cellsAtLod < 1 || cellsPerTile_ % cellsAtLod != 0) return map;
    const int stride = cellsPerTile_ / cellsAtLod;
    map.Resize(cellsAtLod, cellsAtLod);
    const std::vector<float>& fine = found->second.paint;
    const float norm = 1.0f / static_cast<float>(stride * stride);
    for (int z = 0; z < cellsAtLod; ++z) {
        for (int x = 0; x < cellsAtLod; ++x) {
            float sum[4] = { 0, 0, 0, 0 };
            for (int dz = 0; dz < stride; ++dz) {
                for (int dx = 0; dx < stride; ++dx) {
                    const size_t base = (static_cast<size_t>(z * stride + dz) * cellsPerTile_ + (x * stride + dx)) * 4;
                    for (int c = 0; c < 4; ++c) sum[c] += fine[base + c];
                }
            }
            const size_t out = map.TexelIndex(x, z) * kSplatChannels;
            for (int c = 0; c < 4; ++c) map.weights[out + c] = sum[c] * norm;
        }
    }
    return map;
}

std::vector<TileCoord> WorldEdits::TilesAffectedBy(const LatticeRect& rect, bool heights) const
{
    std::vector<TileCoord> tiles;
    if (rect.IsEmpty()) return tiles;
    const LatticeIndex margin = heights ? (static_cast<LatticeIndex>(1) << WorldGridSettings::kTileLodMax) : 0;   // apron and coarse levels reach this far
    const LatticeIndex firstX = CeilDivide(rect.minX - margin - cellsPerTile_, cellsPerTile_), lastX = FloorDivide(rect.maxX + margin, cellsPerTile_);
    const LatticeIndex firstZ = CeilDivide(rect.minZ - margin - cellsPerTile_, cellsPerTile_), lastZ = FloorDivide(rect.maxZ + margin, cellsPerTile_);
    for (LatticeIndex z = firstZ; z <= lastZ; ++z)
        for (LatticeIndex x = firstX; x <= lastX; ++x) tiles.push_back({ static_cast<int>(x), static_cast<int>(z) });
    return tiles;
}

void WorldEdits::ForEachBlock(const std::function<void(TileCoord, const Block&)>& visit) const
{
    // A fixed order, so saving the same edits twice gives the same file.
    std::vector<std::uint64_t> keys;
    keys.reserve(blocks_.size());
    for (const auto& entry : blocks_) keys.push_back(entry.first);
    std::sort(keys.begin(), keys.end());
    for (const std::uint64_t key : keys) {
        TileCoord tile{ static_cast<int>(static_cast<std::int32_t>(key >> 32)), static_cast<int>(static_cast<std::int32_t>(key & 0xFFFFFFFFu)) };
        visit(tile, blocks_.at(key));
    }
}

void WorldEdits::SetBlock(TileCoord tile, Block block)
{
    const size_t cells = static_cast<size_t>(cellsPerTile_) * cellsPerTile_;
    if (!block.height.empty() && block.height.size() != cells) return;
    if (!block.paint.empty() && block.paint.size() != cells * 4) return;
    if (block.height.empty() && block.paint.empty()) return;
    blocks_[Key(tile.x, tile.z)] = std::move(block);
    ++revision_;
}

EditedHeightSource::EditedHeightSource(std::shared_ptr<const WorldHeightSource> base, std::shared_ptr<const WorldEdits> edits, double metresPerCell)
    : base_(std::move(base)), edits_(std::move(edits)), metresPerCell_((std::max)(metresPerCell, 1e-6))
{
}

float EditedHeightSource::Height01(double worldX, double worldZ) const
{
    if (!base_) return 0.0f;
    const float ground = base_->Height01(worldX, worldZ);
    if (!edits_ || !edits_->HasHeightEdits()) return ground;
    const double fx = worldX / metresPerCell_, fz = worldZ / metresPerCell_;
    const double rx = std::round(fx), rz = std::round(fz);
    float delta;
    if (std::fabs(fx - rx) < 1e-6 && std::fabs(fz - rz) < 1e-6) {
        delta = edits_->HeightDelta(static_cast<LatticeIndex>(rx), static_cast<LatticeIndex>(rz));   // the case tiles produce: exact
    } else {
        const LatticeIndex x0 = static_cast<LatticeIndex>(std::floor(fx)), z0 = static_cast<LatticeIndex>(std::floor(fz));
        const float tx = static_cast<float>(fx - static_cast<double>(x0)), tz = static_cast<float>(fz - static_cast<double>(z0));
        const float top = edits_->HeightDelta(x0, z0) * (1.0f - tx) + edits_->HeightDelta(x0 + 1, z0) * tx;
        const float bottom = edits_->HeightDelta(x0, z0 + 1) * (1.0f - tx) + edits_->HeightDelta(x0 + 1, z0 + 1) * tx;
        delta = top * (1.0f - tz) + bottom * tz;
    }
    if (delta == 0.0f) return ground;
    return std::clamp(ground + delta, 0.0f, 1.0f);
}

void WorldStrokeRecorder::RememberHeight(LatticeIndex x, LatticeIndex z, float deltaBefore)
{
    heights_.emplace(std::make_pair(x, z), deltaBefore);   // emplace keeps the FIRST value
}

void WorldStrokeRecorder::RememberPaint(LatticeIndex x, LatticeIndex z, const float before[4])
{
    paints_.emplace(std::make_pair(x, z), std::array<float, 4>{ before[0], before[1], before[2], before[3] });
}

WorldEditPatch WorldStrokeRecorder::Finish(const WorldEdits& edits) const
{
    WorldEditPatch patch;
    for (const auto& entry : heights_) {
        const float after = edits.HeightDelta(entry.first.first, entry.first.second);
        if (std::fabs(after - entry.second) <= 1e-6f) continue;   // went somewhere and came back
        patch.heights.push_back({ entry.first.first, entry.first.second, entry.second, after });
    }
    for (const auto& entry : paints_) {
        PaintEdit change;
        change.x = entry.first.first; change.z = entry.first.second;
        float after[4];
        edits.GetPaint(change.x, change.z, after);
        bool same = true;
        for (int c = 0; c < 4; ++c) { change.before[c] = entry.second[c]; change.after[c] = after[c]; if (std::fabs(after[c] - entry.second[c]) > 1e-6f) same = false; }
        if (!same) patch.paints.push_back(change);
    }
    return patch;
}

LatticeRect ApplyWorldHeightBrush(WorldEdits& edits, const WorldHeightSource& base, double metresPerCell, const BrushSettings& brush,
                                  double worldX, double worldZ, float seconds, WorldStrokeRecorder* recorder)
{
    LatticeRect changed;
    if (!(brush.radius > 0.0f) || !(seconds > 0.0f) || !(metresPerCell > 0.0)) return changed;
    const double radius = brush.radius;
    const LatticeIndex x0 = static_cast<LatticeIndex>(std::floor((worldX - radius) / metresPerCell)), x1 = static_cast<LatticeIndex>(std::ceil((worldX + radius) / metresPerCell));
    const LatticeIndex z0 = static_cast<LatticeIndex>(std::floor((worldZ - radius) / metresPerCell)), z1 = static_cast<LatticeIndex>(std::ceil((worldZ + radius) / metresPerCell));
    const float strength = std::clamp(brush.strength, 0.0f, 1.0f);
    const float lift = strength * kRaiseRatePerSecond * seconds;
    const float blend = strength * kBlendRatePerSecond * seconds;

    // What the eye sees at a lattice point (the ground plus the edits), WITHOUT the rivers.
    auto visible = [&](LatticeIndex x, LatticeIndex z) {
        const float ground = base.Height01(static_cast<double>(x) * metresPerCell, static_cast<double>(z) * metresPerCell);
        return std::clamp(ground + edits.HeightDelta(x, z), 0.0f, 1.0f);
    };

    // TWO PASSES, like the old brush: every new value is worked out from the old ones, then they are written.
    struct Pending { LatticeIndex x, z; float value; float ground; };
    std::vector<Pending> pending;
    for (LatticeIndex z = z0; z <= z1; ++z) {
        for (LatticeIndex x = x0; x <= x1; ++x) {
            const double dx = static_cast<double>(x) * metresPerCell - worldX, dz = static_cast<double>(z) * metresPerCell - worldZ;
            const float weight = BrushWeight(static_cast<float>(std::sqrt(dx * dx + dz * dz)), brush.radius, brush.falloff);
            if (weight <= 0.0f) continue;
            const float old = visible(x, z);
            float value = old;
            switch (brush.mode) {
            case BrushMode::Raise: value = old + weight * lift; break;
            case BrushMode::Lower: value = old - weight * lift; break;
            case BrushMode::Flatten: value = old + (brush.targetHeight - old) * (std::min)(1.0f, weight * blend); break;
            case BrushMode::Smooth: {
                float sum = 0.0f;
                for (int nz = -1; nz <= 1; ++nz)
                    for (int nx = -1; nx <= 1; ++nx) sum += visible(x + nx, z + nz);   // across tile borders: there are none here
                value = old + (sum / 9.0f - old) * (std::min)(1.0f, weight * blend);
                break;
            }
            }
            value = std::clamp(value, 0.0f, 1.0f);
            if (value != old) pending.push_back({ x, z, value, base.Height01(static_cast<double>(x) * metresPerCell, static_cast<double>(z) * metresPerCell) });
        }
    }
    for (const Pending& change : pending) {
        const float before = edits.HeightDelta(change.x, change.z);
        const float delta = change.value - change.ground;   // chosen so that ground + delta is exactly the new value
        if (delta == before) continue;
        if (recorder) recorder->RememberHeight(change.x, change.z, before);
        edits.SetHeightDelta(change.x, change.z, delta);
        changed.Include(change.x, change.z);
    }
    return changed;
}

LatticeRect ApplyWorldPaintBrush(WorldEdits& edits, double metresPerCell, const PaintBrush& brush, double worldX, double worldZ,
                                 float seconds, WorldStrokeRecorder* recorder)
{
    LatticeRect changed;
    if (!(brush.radius > 0.0f) || !(seconds > 0.0f) || !(metresPerCell > 0.0)) return changed;
    if (!brush.erase && (brush.layer < 0 || brush.layer >= kSplatChannels)) return changed;
    const double radius = brush.radius;
    // Cell (i, j) covers lattice points i..i+1, its centre is at i + 0.5.
    const LatticeIndex x0 = static_cast<LatticeIndex>(std::floor((worldX - radius) / metresPerCell - 0.5)), x1 = static_cast<LatticeIndex>(std::ceil((worldX + radius) / metresPerCell - 0.5));
    const LatticeIndex z0 = static_cast<LatticeIndex>(std::floor((worldZ - radius) / metresPerCell - 0.5)), z1 = static_cast<LatticeIndex>(std::ceil((worldZ + radius) / metresPerCell - 0.5));
    const float amount = std::clamp(brush.strength, 0.0f, 1.0f) * kPaintRatePerSecond * seconds;
    for (LatticeIndex z = z0; z <= z1; ++z) {
        for (LatticeIndex x = x0; x <= x1; ++x) {
            const double dx = (static_cast<double>(x) + 0.5) * metresPerCell - worldX, dz = (static_cast<double>(z) + 0.5) * metresPerCell - worldZ;
            const float weight = BrushWeight(static_cast<float>(std::sqrt(dx * dx + dz * dz)), brush.radius, brush.falloff);
            if (weight <= 0.0f) continue;
            const float t = (std::min)(1.0f, weight * amount);
            float old[4], now[4];
            edits.GetPaint(x, z, old);
            bool texelChanged = false;
            for (int channel = 0; channel < kSplatChannels; ++channel) {
                const float target = (!brush.erase && channel == brush.layer) ? 1.0f : 0.0f;
                float value = old[channel] + (target - old[channel]) * t;
                if (value < 1e-4f) value = 0.0f;   // the old paint brush's own floor
                now[channel] = value;
                if (value != old[channel]) texelChanged = true;
            }
            if (!texelChanged) continue;
            if (recorder) recorder->RememberPaint(x, z, old);
            edits.SetPaint(x, z, now);
            changed.Include(x, z);
        }
    }
    return changed;
}

WorldPatchExtent ApplyWorldEditPatch(WorldEdits& edits, const WorldEditPatch& patch, bool useAfter)
{
    WorldPatchExtent extent;
    for (const HeightEdit& change : patch.heights) {
        edits.SetHeightDelta(change.x, change.z, useAfter ? change.after : change.before);
        extent.heights.Include(change.x, change.z);
    }
    for (const PaintEdit& change : patch.paints) {
        edits.SetPaint(change.x, change.z, useAfter ? change.after : change.before);
        extent.paint.Include(change.x, change.z);
    }
    return extent;
}

MigrationStats MigrateHeightsIntoEdits(const std::vector<float>& heights, const OldTerrainPlacement& placement, const WorldHeightSource& base,
                                       double metresPerCell, WorldEdits& edits, float threshold)
{
    MigrationStats stats;
    const int cx = placement.cellsX, cz = placement.cellsZ;
    if (cx < 1 || cz < 1 || !(placement.sizeX > 0.0) || !(placement.sizeZ > 0.0) || !(metresPerCell > 0.0)) return stats;
    if (heights.size() < static_cast<size_t>(cx + 1) * static_cast<size_t>(cz + 1)) return stats;
    const double minX = placement.centerX - 0.5 * placement.sizeX, minZ = placement.centerZ - 0.5 * placement.sizeZ;
    const double eps = 1e-6;
    const LatticeIndex gx0 = static_cast<LatticeIndex>(std::ceil((minX) / metresPerCell - eps)), gx1 = static_cast<LatticeIndex>(std::floor((minX + placement.sizeX) / metresPerCell + eps));
    const LatticeIndex gz0 = static_cast<LatticeIndex>(std::ceil((minZ) / metresPerCell - eps)), gz1 = static_cast<LatticeIndex>(std::floor((minZ + placement.sizeZ) / metresPerCell + eps));
    auto at = [&](int x, int z) { return heights[static_cast<size_t>(z) * (cx + 1) + x]; };
    for (LatticeIndex gz = gz0; gz <= gz1; ++gz) {
        for (LatticeIndex gx = gx0; gx <= gx1; ++gx) {
            const double wx = static_cast<double>(gx) * metresPerCell, wz = static_cast<double>(gz) * metresPerCell;
            const double fx = std::clamp((wx - minX) / placement.sizeX * cx, 0.0, static_cast<double>(cx));
            const double fz = std::clamp((wz - minZ) / placement.sizeZ * cz, 0.0, static_cast<double>(cz));
            // Where the lattice meets the old grid (the usual case) this is the old sample itself; otherwise a bilinear blend.
            const double rx = std::round(fx), rz = std::round(fz);
            float old;
            if (std::fabs(fx - rx) < 1e-6 && std::fabs(fz - rz) < 1e-6) {
                old = at(static_cast<int>(rx), static_cast<int>(rz));
            } else {
                const int ix = (std::min)(static_cast<int>(fx), cx - 1), iz = (std::min)(static_cast<int>(fz), cz - 1);
                const float tx = static_cast<float>(fx - ix), tz = static_cast<float>(fz - iz);
                const float top = at(ix, iz) + (at(ix + 1, iz) - at(ix, iz)) * tx;
                const float bottom = at(ix, iz + 1) + (at(ix + 1, iz + 1) - at(ix, iz + 1)) * tx;
                old = top + (bottom - top) * tz;
            }
            const float delta = std::clamp(old, 0.0f, 1.0f) - base.Height01(wx, wz);
            if (std::fabs(delta) <= threshold) continue;
            edits.SetHeightDelta(gx, gz, delta);
            ++stats.heightSamples;
        }
    }
    return stats;
}

MigrationStats MigratePaintIntoEdits(const PaintMap& paint, const OldTerrainPlacement& placement, double metresPerCell, WorldEdits& edits)
{
    MigrationStats stats;
    if (paint.IsEmpty() || paint.width < 1 || paint.height < 1 || !(placement.sizeX > 0.0) || !(placement.sizeZ > 0.0) || !(metresPerCell > 0.0)) return stats;
    const double minX = placement.centerX - 0.5 * placement.sizeX, minZ = placement.centerZ - 0.5 * placement.sizeZ;
    const LatticeIndex cx0 = static_cast<LatticeIndex>(std::floor(minX / metresPerCell + 1e-6)), cx1 = static_cast<LatticeIndex>(std::ceil((minX + placement.sizeX) / metresPerCell - 1e-6)) - 1;
    const LatticeIndex cz0 = static_cast<LatticeIndex>(std::floor(minZ / metresPerCell + 1e-6)), cz1 = static_cast<LatticeIndex>(std::ceil((minZ + placement.sizeZ) / metresPerCell - 1e-6)) - 1;
    for (LatticeIndex cz = cz0; cz <= cz1; ++cz) {
        for (LatticeIndex cx = cx0; cx <= cx1; ++cx) {
            const double centreX = (static_cast<double>(cx) + 0.5) * metresPerCell, centreZ = (static_cast<double>(cz) + 0.5) * metresPerCell;
            const double u = (centreX - minX) / placement.sizeX, v = (centreZ - minZ) / placement.sizeZ;
            if (u < 0.0 || u >= 1.0 || v < 0.0 || v >= 1.0) continue;
            const int tx = (std::min)(static_cast<int>(u * paint.width), paint.width - 1), tz = (std::min)(static_cast<int>(v * paint.height), paint.height - 1);
            const size_t base = paint.TexelIndex(tx, tz) * kSplatChannels;
            const float w[4] = { paint.weights[base], paint.weights[base + 1], paint.weights[base + 2], paint.weights[base + 3] };
            if (w[0] + w[1] + w[2] + w[3] < 1e-4f) continue;
            edits.SetPaint(cx, cz, w);
            ++stats.paintCells;
        }
    }
    return stats;
}

namespace {
constexpr char kMagic[4] = { 'W', 'E', 'D', 'T' };
constexpr std::uint32_t kVersion = 1;
template <typename T> bool WriteValue(std::FILE* file, const T& value) { return std::fwrite(&value, sizeof(T), 1, file) == 1; }
template <typename T> bool ReadValue(std::FILE* file, T& value) { return std::fread(&value, sizeof(T), 1, file) == 1; }
}   // namespace

bool SaveWorldEdits(const std::string& path, const WorldEdits& edits, std::string& error)
{
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) { error = "could not open the file for writing: " + path; return false; }
    bool ok = std::fwrite(kMagic, 1, 4, file) == 4 && WriteValue(file, kVersion) && WriteValue(file, static_cast<std::uint32_t>(edits.CellsPerTile())) &&
              WriteValue(file, static_cast<std::uint32_t>(edits.BlockCount()));
    edits.ForEachBlock([&](TileCoord tile, const WorldEdits::Block& block) {
        if (!ok) return;
        const std::uint32_t flags = (block.height.empty() ? 0u : 1u) | (block.paint.empty() ? 0u : 2u);
        ok = WriteValue(file, static_cast<std::int32_t>(tile.x)) && WriteValue(file, static_cast<std::int32_t>(tile.z)) && WriteValue(file, flags);
        if (ok && !block.height.empty()) ok = std::fwrite(block.height.data(), sizeof(float), block.height.size(), file) == block.height.size();
        if (ok && !block.paint.empty()) {
            std::vector<std::uint8_t> bytes(block.paint.size());
            for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(std::lround(std::clamp(block.paint[i], 0.0f, 1.0f) * 255.0f));
            ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
        }
    });
    const bool closed = std::fclose(file) == 0;
    if (!ok || !closed) { error = "could not write the file: " + path; return false; }
    return true;
}

bool LoadWorldEdits(const std::string& path, WorldEdits& edits, std::string& error)
{
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) { error = "could not open the file: " + path; return false; }
    struct Closer { std::FILE* f; ~Closer() { std::fclose(f); } } closer{ file };
    char magic[4] = {};
    std::uint32_t version = 0, cells = 0, count = 0;
    if (std::fread(magic, 1, 4, file) != 4 || std::memcmp(magic, kMagic, 4) != 0 || !ReadValue(file, version) || !ReadValue(file, cells) || !ReadValue(file, count)) {
        error = "not a world edits file: " + path;
        return false;
    }
    if (version != kVersion) { error = "unknown world edits version " + std::to_string(version) + ": " + path; return false; }
    if (cells < 1 || cells > 4096 || count > 100000) { error = "the world edits file is damaged: " + path; return false; }
    WorldEdits loaded(static_cast<int>(cells));
    const size_t cellCount = static_cast<size_t>(cells) * cells;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::int32_t tx = 0, tz = 0;
        std::uint32_t flags = 0;
        if (!ReadValue(file, tx) || !ReadValue(file, tz) || !ReadValue(file, flags)) { error = "the world edits file ends too early: " + path; return false; }
        WorldEdits::Block block;
        if (flags & 1u) {
            block.height.resize(cellCount);
            if (std::fread(block.height.data(), sizeof(float), cellCount, file) != cellCount) { error = "the world edits file ends too early: " + path; return false; }
        }
        if (flags & 2u) {
            std::vector<std::uint8_t> bytes(cellCount * 4);
            if (std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size()) { error = "the world edits file ends too early: " + path; return false; }
            block.paint.resize(bytes.size());
            for (size_t k = 0; k < bytes.size(); ++k) block.paint[k] = bytes[k] / 255.0f;
        }
        loaded.SetBlock({ tx, tz }, std::move(block));
    }
    edits = std::move(loaded);
    return true;
}
