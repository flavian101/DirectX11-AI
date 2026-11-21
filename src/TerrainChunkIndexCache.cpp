#include "TerrainChunkIndices.h"
#include <map>
#include <mutex>
#include <tuple>

namespace {
std::mutex g_mutex;   // tiles may one day be built on a worker thread (roadmap C4); CreateBuffer itself is thread-safe
ID3D11Device* g_device = nullptr;
std::map<std::tuple<int, int, int>, SharedIndexLevels> g_sets;   // a std::map: pointers to its values stay valid

void ReleaseLocked()
{
	for (auto& entry : g_sets) ReleaseSharedIndexLevels(entry.second);
	g_sets.clear();
	g_device = nullptr;
}
}

const SharedIndexLevels* GetSharedChunkIndices(ID3D11Device* device, int cellsInX, int cellsInZ, int lodCount)
{
	if (!device || cellsInX < 1 || cellsInZ < 1 || lodCount < 1) return nullptr;
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_device != device) {   // a different device: the old buffers are of no use to it
		ReleaseLocked();
		g_device = device;
	}
	const auto key = std::make_tuple(cellsInX, cellsInZ, lodCount);
	const auto found = g_sets.find(key);
	if (found != g_sets.end()) return &found->second;

	SharedIndexLevels set;
	if (!CreateSharedIndexLevels(device, BuildChunkIndexLevels(cellsInX, cellsInZ, lodCount), set)) return nullptr;
	return &g_sets.emplace(key, std::move(set)).first->second;
}

void ReleaseSharedChunkIndices()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	ReleaseLocked();
}
