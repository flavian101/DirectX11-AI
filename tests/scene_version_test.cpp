// Unit tests for src/SceneVersion.h (roadmap T3): a scene file is read only at exactly the current version, and every other version
// gets one clear message that says which way it is wrong. No Direct3D, no framework: a failed CHECK prints and the program exits 1.
#include "SceneVersion.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

static bool Mentions(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

int main()
{
	// The current version reads.
	CHECK(SceneVersionProblem(kSceneFileVersion).empty());

	// Every older version, and a file with no version field (0), is refused, and the message says OLD and names both formats.
	for (int version = 0; version < kSceneFileVersion; ++version) {
		const std::string message = SceneVersionProblem(version);
		CHECK(!message.empty());
		CHECK(Mentions(message, "old file format") && Mentions(message, std::to_string(version)) && Mentions(message, std::to_string(kSceneFileVersion)));
		CHECK(Mentions(message, "not converted"));
	}

	// A newer version is refused too (it is not "read what you can"), and the message says NEWER.
	for (int version = kSceneFileVersion + 1; version <= kSceneFileVersion + 3; ++version) {
		const std::string message = SceneVersionProblem(version);
		CHECK(!message.empty());
		CHECK(Mentions(message, "newer version") && Mentions(message, std::to_string(version)));
	}

	// It is one rule: exactly one version reads.
	int readable = 0;
	for (int version = -5; version < 50; ++version) if (SceneVersionProblem(version).empty()) ++readable;
	CHECK(readable == 1);

	if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
	std::printf("scene version tests: all passed\n");
	return 0;
}
