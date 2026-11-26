#include "WorldGroundSource.h"
#include "HeightSample.h"
#include <algorithm>

RasterHeightSource::RasterHeightSource(std::shared_ptr<const HeightRaster> raster, float lowestY, float relief)
    : raster_(std::move(raster)), lowestY_(lowestY), relief_((std::max)(relief, 1e-3f))
{
}

float RasterHeightSource::Height01(double worldX, double worldZ) const
{
    if (!raster_) return 0.0f;
    const double metres = SampleHeightBicubic(*raster_, worldX, worldZ);
    return std::clamp(static_cast<float>((metres - static_cast<double>(lowestY_)) / static_cast<double>(relief_)), 0.0f, 1.0f);
}

DetailedHeightSource::DetailedHeightSource(std::shared_ptr<const WorldHeightSource> base, const HeightDetailSettings& detail, float relief)
    : base_(std::move(base)), detail_(detail), relief_((std::max)(relief, 1e-3f))
{
    detail_.Clamp();
}

float DetailedHeightSource::Height01(double worldX, double worldZ) const
{
    if (!base_) return 0.0f;
    const float ground = base_->Height01(worldX, worldZ);
    if (detail_.IsIdentity()) return ground;
    const double bump = DetailMetres(detail_, worldX, worldZ) / static_cast<double>(relief_);
    return std::clamp(static_cast<float>(static_cast<double>(ground) + bump), 0.0f, 1.0f);
}

bool ImportWorldRaster(const std::string& path, const HeightImportSettings& settings, double cellMetres, double centreX, double centreZ,
                       std::shared_ptr<const HeightRaster>& raster, WorldPictureInfo& info, std::string& error)
{
    info = WorldPictureInfo{};
    HeightPicture picture;
    if (!LoadHeightPicture(path, picture, error)) return false;
    HeightRaster imported;
    HeightImportInfo importInfo;
    if (!ImportHeightRaster(picture, settings, imported, &importInfo)) {
        error = "the picture could not be turned into ground";
        return false;
    }
    imported.originX = centreX - 0.5 * imported.SizeX();   // placed by its centre (H8)
    imported.originZ = centreZ - 0.5 * imported.SizeZ();
    info.valid = true;
    info.widthPixels = picture.width;
    info.heightPixels = picture.height;
    info.bits = picture.bits;
    info.metresPerPixel = imported.spacingX;
    info.stats = ComputeHeightStats(imported);
    info.shareBelowZero = FractionBelow(imported, 0.0f);
    info.flat = importInfo.flat;
    info.deterraced = picture.bits == 8 && settings.deterrace && !importInfo.flat;
    info.cellPixels = (cellMetres > 0.0 && imported.spacingX > 0.0) ? cellMetres / imported.spacingX : 0.0;
    raster = std::make_shared<const HeightRaster>(cellMetres > 0.0 ? ResampleToSpacing(imported, cellMetres) : std::move(imported));
    return true;
}
