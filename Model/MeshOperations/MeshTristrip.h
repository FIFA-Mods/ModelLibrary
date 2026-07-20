#pragma once
#include <vector>

namespace MeshTristrip {

std::vector<uint16_t> GenerateTristrips(const std::vector<std::vector<uint32_t>> &polygons);

}
