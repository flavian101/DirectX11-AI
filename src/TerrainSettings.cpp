#include "TerrainSettings.h"

void TerrainSettings::Clamp()
{
	width = std::clamp(width, kWidthMin, kWidthMax);
	heightScale = std::clamp(heightScale, kHeightMin, kHeightMax);
	maxCells = std::clamp(maxCells, kCellsMin, kCellsMax);
	uvTiling = std::clamp(uvTiling, kTilingMin, kTilingMax);
	groundTiling = std::clamp(groundTiling, kRockTilingMin, kRockTilingMax);
	rockTiling = std::clamp(rockTiling, kRockTilingMin, kRockTilingMax);
	sandTiling = std::clamp(sandTiling, kRockTilingMin, kRockTilingMax);
	snowTiling = std::clamp(snowTiling, kRockTilingMin, kRockTilingMax);
	rockDarkness = std::clamp(rockDarkness, 0.1f, 1.5f);
	normalStrength = std::clamp(normalStrength, 0.0f, 3.0f);
	heightBlend = std::clamp(heightBlend, 0.0f, 1.0f);
	aoStrength = std::clamp(aoStrength, 0.0f, 1.0f);
	roughnessScale = std::clamp(roughnessScale, 0.0f, 2.0f);
	slopeStart = std::clamp(slopeStart, 0.0f, 1.0f);
	slopeFade = std::clamp(slopeFade, 0.01f, 1.0f);
	shoreHeight = std::clamp(shoreHeight, kBandHardMin, kBandHardMax);
	shoreWidth = std::clamp(shoreWidth, 0.1f, 30.0f);
	snowHeight = std::clamp(snowHeight, kBandHardMin, kBandHardMax);
	snowFade = std::clamp(snowFade, 0.1f, 50.0f);
	breakupScale = std::clamp(breakupScale, kBreakupScaleMin, kBreakupScaleMax);
	breakupAmount = std::clamp(breakupAmount, 0.0f, 1.0f);
	lodDistance0 = std::clamp(lodDistance0, kLodDistanceMin, kLodDistanceMax);
	lodDistance1 = std::clamp(lodDistance1, lodDistance0 * 1.2f, kLodDistanceMax * 2.0f);   // level 2 must start beyond level 1
}

void TerrainSettings::ResetLayers()
{
	const TerrainSettings defaults;
	groundTiling = defaults.groundTiling; rockTiling = defaults.rockTiling;
	sandTiling = defaults.sandTiling;     snowTiling = defaults.snowTiling;
	rockDarkness = defaults.rockDarkness;
	normalStrength = defaults.normalStrength; flipGreen = defaults.flipGreen;
	heightBlend = defaults.heightBlend;   aoStrength = defaults.aoStrength;
	roughnessScale = defaults.roughnessScale;
	groundTint = defaults.groundTint;     rockTint = defaults.rockTint;
	slopeStart = defaults.slopeStart;     slopeFade = defaults.slopeFade;
	shoreHeight = defaults.shoreHeight;   shoreWidth = defaults.shoreWidth;
	snowHeight = defaults.snowHeight;     snowFade = defaults.snowFade;
	breakupScale = defaults.breakupScale; breakupAmount = defaults.breakupAmount;
	shoreColor = defaults.shoreColor;     snowColor = defaults.snowColor;
}

bool TerrainSettings::SameShape(const TerrainSettings& o) const
{
	return width == o.width && heightScale == o.heightScale && maxCells == o.maxCells;
}

bool TerrainSettings::SameSplatRules(const TerrainSettings& o) const
{
	return slopeStart == o.slopeStart && slopeFade == o.slopeFade && shoreHeight == o.shoreHeight && shoreWidth == o.shoreWidth &&
		snowHeight == o.snowHeight && snowFade == o.snowFade && breakupScale == o.breakupScale && breakupAmount == o.breakupAmount;
}
