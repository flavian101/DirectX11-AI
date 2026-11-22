#include "HeightRaster.h"
#include "HeightDeterrace.h"
#include <algorithm>

void HeightImportSettings::Clamp()
{
    if (!(widthMetres >= kWidthMin)) widthMetres = kWidthMin;   // also catches NaN
    widthMetres = (std::min)(widthMetres, kWidthMax);
    if (!(lowestY >= kYMin)) lowestY = kYMin;
    lowestY = (std::min)(lowestY, kYMax);
    if (!(relief >= kReliefMin)) relief = kReliefMin;
    relief = (std::min)(relief, kReliefMax);
}

bool ImportHeightRaster(const HeightPicture& picture, const HeightImportSettings& requested, HeightRaster& raster, HeightImportInfo* info)
{
    if (!picture.IsValid()) return false;
    HeightImportSettings settings = requested;
    settings.Clamp();

    // The levels as floats: for an 8-bit picture the stairs come out first. The fit below uses the range of what is left, so the lowest point
    // is still exactly lowestY and the highest exactly lowestY + relief (the blur can shave a peak by less than half a level).
    const std::vector<float> values = DeterraceLevels(picture, (picture.bits == 8 && settings.deterrace) ? kDeterraceIterations : 0);
    const auto range = std::minmax_element(values.begin(), values.end());
    const float minLevel = *range.first, maxLevel = *range.second;

    HeightRaster result;
    result.width = picture.width;
    result.height = picture.height;
    result.originX = settings.originX;
    result.originZ = settings.originZ;
    result.spacingX = result.spacingZ = settings.widthMetres / static_cast<double>(picture.width - 1);
    result.metres.resize(values.size());

    // Done in double, rounded to float once. t is exactly 0 for the lowest level and exactly 1 for the highest (n / n), so those two land on
    // lowestY and lowestY + relief to the last bit.
    const bool flat = maxLevel == minLevel;
    const double span = flat ? 1.0 : static_cast<double>(maxLevel) - static_cast<double>(minLevel);
    for (size_t i = 0; i < values.size(); ++i) {
        const double t = (static_cast<double>(values[i]) - static_cast<double>(minLevel)) / span;
        result.metres[i] = static_cast<float>(static_cast<double>(settings.lowestY) + t * static_cast<double>(settings.relief));
    }

    if (info) { info->minLevel = minLevel; info->maxLevel = maxLevel; info->flat = flat; }
    raster = std::move(result);
    return true;
}
