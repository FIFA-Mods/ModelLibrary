#include "MeshTriangulation.h"
#include <3rdparty/mapbox/earcut.hpp>
#include <array>
#include <cmath>

namespace {

inline float Dot(const Vector3 &a, const Vector3 &b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vector3 Cross(const Vector3 &a, const Vector3 &b) {
    return Vector3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

// Triangulates a single polygon (list of global vertex indices).
std::vector<std::vector<uint32_t>> TriangulatePolygon(
    const std::vector<uint32_t> &polygon,
    const std::vector<Vertex> &vertices) {
    const size_t n = polygon.size();

    std::vector<Vector3> pts(n);
    for (size_t i = 0; i < n; ++i)
        pts[i] = vertices[polygon[i]].pos;

    Vector3 normal(0.f, 0.f, 0.f);
    for (size_t i = 0; i < n; ++i) {
        const Vector3 &a = pts[i];
        const Vector3 &b = pts[(i + 1) % n];
        normal.x += (a.y - b.y) * (a.z + b.z);
        normal.y += (a.z - b.z) * (a.x + b.x);
        normal.z += (a.x - b.x) * (a.y + b.y);
    }

    if (normal.SquareLength() < 1e-12f)
        return {}; // degenerate / zero-area polygon: drop it

    normal.NormalizeSafe();

    Vector3 arbitrary = (std::fabs(normal.x) < 0.9f) ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
    Vector3 u = Cross(arbitrary, normal);
    u.NormalizeSafe();
    Vector3 v = Cross(normal, u);

    using Point = std::array<double, 2>;
    std::vector<std::vector<Point>> rings(1);
    rings[0].reserve(n);
    for (size_t i = 0; i < n; ++i)
        rings[0].push_back({ (double)Dot(pts[i], u), (double)Dot(pts[i], v) });

    std::vector<uint32_t> local = mapbox::earcut<uint32_t>(rings);

    std::vector<std::vector<uint32_t>> result;
    result.reserve(local.size() / 3);
    for (size_t i = 0; i + 2 < local.size(); i += 3)
        result.push_back({ polygon[local[i]], polygon[local[i + 1]], polygon[local[i + 2]] });
    return result;
}

} // namespace

namespace MeshTriangulation {

std::vector<std::vector<uint32_t>> Triangulate(
    const std::vector<std::vector<uint32_t>> &polygons,
    const std::vector<Vertex> &vertices) {
    std::vector<std::vector<uint32_t>> out;
    out.reserve(polygons.size());
    for (auto &poly : polygons) {
        if (poly.size() < 3) continue;
        if (poly.size() == 3) { out.push_back(poly); continue; }
        for (auto &tri : TriangulatePolygon(poly, vertices))
            out.push_back(std::move(tri));
    }
    return out;
}

std::vector<std::vector<uint32_t>> LeaveTrisAndQuads(
    const std::vector<std::vector<uint32_t>> &polygons,
    const std::vector<Vertex> &vertices) {
    std::vector<std::vector<uint32_t>> out;
    out.reserve(polygons.size());
    for (auto &poly : polygons) {
        if (poly.size() < 3) continue;
        if (poly.size() <= 4) { out.push_back(poly); continue; }
        for (auto &tri : TriangulatePolygon(poly, vertices))
            out.push_back(std::move(tri));
    }
    return out;
}

} // namespace MeshTriangulation
