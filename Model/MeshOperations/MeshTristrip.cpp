#include "MeshTristrip.h"
#include "3rdparty/NvTriStrip/NvTriStrip.h"

namespace {

// Appends triangle (a, b, c) onto the end of an existing strip index array,
// inserting degenerate (zero-area) indices so the appended triangle keeps
// its original winding order. Standard tristrip-concatenation technique.
static void AppendTriangleToStrip(std::vector<uint16_t> &strip, uint16_t a, uint16_t b, uint16_t c) {
    if (strip.empty()) {
        strip.push_back(a);
        strip.push_back(b);
        strip.push_back(c);
        return;
    }
    const bool startedEven = (strip.size() % 2 == 0);
    const uint16_t last = strip.back();
    strip.push_back(last);  // degenerate: repeats current last vertex
    strip.push_back(a);     // degenerate: repeats next triangle's first vertex
    if (!startedEven) {
        strip.push_back(a); // one more repeat needed to keep winding parity correct
    }
    strip.push_back(a);
    strip.push_back(b);
    strip.push_back(c);
}

}

namespace MeshTristrip {

// Generates a single tristrip index array from triangles.
// It's expected that all polygons are triangles (non-triangle entries are skipped).
// It's expected that all vertex indices are in range [0; 65535].
// Returns an empty vector on failure or if there are no triangles.
// The number of resulting triangles is calculated as (numIndices - 2)
std::vector<uint16_t> GenerateTristrips(const std::vector<std::vector<uint32_t>> &polygons) {
    SetListsOnly(false);
    SetCacheSize(CACHESIZE_GEFORCE3);
    SetStitchStrips(true);
    SetMinStripSize(0);
    std::vector<uint16_t> indexBuffer;
    indexBuffer.reserve(polygons.size() * 3);
    for (auto const &p : polygons) {
        if (p.size() == 3) {
            indexBuffer.push_back(static_cast<uint16_t>(p[0]));
            indexBuffer.push_back(static_cast<uint16_t>(p[1]));
            indexBuffer.push_back(static_cast<uint16_t>(p[2]));
        }
    }
    if (indexBuffer.empty())
        return {};
    PrimitiveGroup *prims = nullptr;
    unsigned short numprims = 0;
    if (!GenerateStrips(indexBuffer.data(), static_cast<unsigned int>(indexBuffer.size()),
        &prims, &numprims, false)) {
        return {};
    }
    std::vector<uint16_t> result;
    for (unsigned short i = 0; i < numprims; ++i) {
        const PrimitiveGroup &g = prims[i];
        if (g.type == PT_STRIP) {
            // Stitching guarantees at most one of these; just take it as-is.
            result.assign(g.indices, g.indices + g.numIndices);
        }
        else {
            // PT_LIST: leftover triangles that couldn't be fit into the strip.
            // Stitch each one onto the end so the caller only ever sees one array.
            for (unsigned int j = 0; j + 2 < g.numIndices; j += 3) {
                AppendTriangleToStrip(result, g.indices[j], g.indices[j + 1], g.indices[j + 2]);
            }
        }
    }
    delete[] prims;
    return result;
}

}
