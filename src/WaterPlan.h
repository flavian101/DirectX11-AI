#pragma once
// What the scene decided about its water bodies for ONE frame. Built by Scene::PlanWater before any drawing,
// read by the renderer (which renders the mirror images asked for) and by each Water (which picks its own image).
//
// Why a plan: every water body that mirrors the scene needs a mirror image of its own level, and each image costs a
// whole extra scene draw. With one lake that was free to decide; with many it needs a BUDGET: bodies at the same level
// share one image, the nearest levels get one, the rest fall back to the cheap sky reflection.
#include <d3d11.h>
#include <vector>

class Water;

struct WaterFramePlan {
	static constexpr int kMaxReflections = 2;   // planar reflections per frame (each one is a full extra scene draw)

	struct Reflection {
		float planeHeight = 0.0f;               // world Y of the mirror
		float resolutionScale = 1.0f;           // fraction of the screen size (the largest any body sharing it asked for)
		ID3D11ShaderResourceView* image = nullptr;   // filled in by the renderer once the image is drawn; null = it failed
	};
	struct Entry {
		const Water* water = nullptr;
		int reflection = -1;                    // index into `reflections`, or -1 = sky reflection only
	};

	Reflection reflections[kMaxReflections];
	int reflectionCount = 0;
	std::vector<Entry> entries;                 // every water body that is on screen this frame
	bool refraction = false;                    // some water on screen bends the view of the bottom: copy the opaque scene

	// The mirror image for `water`, or null if it has none this frame.
	ID3D11ShaderResourceView* ImageFor(const Water* water, int* slotOut = nullptr) const
	{
		for (const Entry& entry : entries) {
			if (entry.water != water) continue;
			if (slotOut) *slotOut = entry.reflection;
			return entry.reflection >= 0 ? reflections[entry.reflection].image : nullptr;
		}
		if (slotOut) *slotOut = -1;
		return nullptr;
	}
};
