#pragma once
// Cutting a river's channel into the terrain (lesson 38). Pure logic like HeightfieldEdit.h: it works on a Heightfield in its
// LOCAL space and on sample values (0..1), and records every sample it changes into a StrokeRecorder, so the whole carve is
// ONE undo step exactly like a brush stroke. The terrain turns world units into local ones (Terrain::CarveRiver).
#include "Heightfield.h"
#include "HeightfieldEdit.h"
#include "RiverPath.h"

struct RiverCarveSettings {
	float level = 0.5f;       // the water's surface, as a sample value
	float depth = 0.05f;      // how far BELOW the level the bed is in the middle of the river, sample value
	float bankWidth = 4.0f;   // local units: how far beyond the water's edge the bank rises
	float bankHeight = 0.05f; // sample value: how high above the level the bank has risen by the end of its width (ground higher than that stays as a steep wall)
};

// How high the ground should be at distance `d` from the middle line of a river whose half width is `halfWidth`, if it was
// `ground` before. Inside the water: a rounded bed, deepest in the middle, exactly at the level at the water's edge. Beyond
// the edge: ground may rise no faster than a smooth bank that climbs `bankHeight` over `bankWidth`. Never HIGHER than the old
// ground (a river only digs). The limit depends only on the distance, never on the ground, so carving the same river twice
// changes nothing the second time.
float CarvedHeight(float d, float halfWidth, float ground, const RiverCarveSettings& settings);

// Digs the river into the field. `path` is in the field's LOCAL space (positions and halfWidth). Returns the block of samples
// that changed (empty if none did).
GridRect CarveRiver(Heightfield& field, const std::vector<RiverPath::Sample>& path, const RiverCarveSettings& settings,
                    StrokeRecorder* recorder);
