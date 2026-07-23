#pragma once
#include <vector>

namespace MeshTristrip {

// Generates a single tristrip index array from triangles.
// It's expected that all polygons are triangles (non-triangle entries are skipped).
// It's expected that all vertex indices are in range [0; 65535].
// Returns an empty vector on failure or if there are no triangles.
// The number of resulting triangles is calculated as (numIndices - 2)
std::vector<uint16_t> GenerateTristrips(const std::vector<std::vector<uint32_t>> &polygons);

}
