#pragma once
#include <vector>
#include <cstdint>
#include "ModelClass.h"

namespace MeshTriangulation {

std::vector<std::vector<uint32_t>> Triangulate(
    const std::vector<std::vector<uint32_t>> &polygons,
    const std::vector<Vertex> &vertices);

std::vector<std::vector<uint32_t>> LeaveTrisAndQuads(
    const std::vector<std::vector<uint32_t>> &polygons,
    const std::vector<Vertex> &vertices);

}
