#include "Model.h"
#include "Error.h"
#include "Utils.h"
#include <queue>

void ConvertTrisToQuads(Mesh &mesh) {
    std::set<int> triIDs = { 2531,2545,2557,2565,2569,2570,2571,2573,2578,2579,2584,2585,2586,2589,2590,2592,2594,2599,2600,2602,2607,2608,2609,2610,2611,2614,2617,2619,2622,2626,2627,2628,2634,2635,2638,2640,2641,2643,2653,2668,2671,2673,2674,2678,2679,2681,2688,2689,2691,2692,2693,2694,2704,2705,2707,2708,2709,2710,2711,2712,2715,2716,2717,2718,2722,2723,2725,2726,2727,2728,2729,2731,2735,2736,2737,2738,2739,2740,2742,2743,2744,2745,2746,2747,2749,2750,2757,2758,2762,2763,2764,2766,2767,2770,2771,2774,2775,2776,2777,2778,2779,2780,2785,2791,2792,2793,2794,2795,2796,2797,2798,2799,2800,2801,2802,2803,2804,2806,2807,2808,2809,2810,2811,2812,2813,2814,2815,2818,2819,2820,2821,2824,2825,2826,2827,2841,2853,2861,2865,2866,2867,2869,2874,2875,2880,2881,2882,2885,2886,2888,2890,2895,2896,2898,2903,2904,2905,2906,2907,2910,2913,2915,2918,2922,2923,2924,2930,2931,2934,2936,2937,2939,2949,2964,2967,2969,2970,2974,2975,2977,2984,2985,2987,2988,2989,2990,3000,3001,3003,3004,3005,3006,3007,3008,3011,3012,3013,3014,3018,3019,3021,3022,3023,3024,3025,3027,3031,3032,3033,3034,3035,3036,3038,3039,3040,3041,3042,3043,3045,3046,3053,3054,3058,3059,3060,3062,3063,3066,3067,3070,3071,3072,3073,3074,3075,3076,3081,3087,3088,3089,3090,3091,3092,3093,3094,3095,3096,3097,3098,3099,3100,3102,3103,3104,3105,3106,3107,3108,3109,3110,3111,3114,3115,3116,3117,3120,3121,3122,3123,3124,3125,3126,3127,3128,3129,3130,3131,3132,3133,3134,3135,3136,3137,3138,3139,3140,3141,3142,3143,3144,3145,3146,3147,3148,3149,3150,3151,3152,3153,3154,3155,3156,3157,3158,3159,3160 };
    int quadID = 1;
    size_t i = 0;
    auto AddQuad = [&mesh](uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        mesh.quads.push_back(std::array{ a, b, c, d });
    };
    while (i < mesh.triangles.size()) {
        if (triIDs.contains(quadID)) {
            const auto &tri = mesh.triangles[i];
            AddQuad(tri[0], tri[1], tri[2], tri[2]);
            i += 1;
        }
        else {
            if (i + 1 < mesh.triangles.size()) {
                const auto &t1 = mesh.triangles[i];
                const auto &t2 = mesh.triangles[i + 1];
                int u2 = -1;
                for (int v : t2) {
                    if (v != t1[0] && v != t1[1] && v != t1[2]) {
                        u2 = v;
                        break;
                    }
                }
                int u1_idx = -1;
                for (int j = 0; j < 3; ++j) {
                    if (t1[j] != t2[0] && t1[j] != t2[1] && t1[j] != t2[2]) {
                        u1_idx = j;
                        break;
                    }
                }
                if (u2 != -1 && u1_idx != -1) {
                    if (u1_idx == 0)
                        AddQuad(t1[0], t1[1], u2, t1[2]);
                    else if (u1_idx == 1)
                        AddQuad(t1[1], t1[2], u2, t1[0]);
                    else if (u1_idx == 2)
                        AddQuad(t1[2], t1[0], u2, t1[1]);
                }
                else {
                    AddQuad(t1[0], t1[1], t1[2], t1[2]);
                    i -= 1;
                }
                i += 2;
            }
            else {
                const auto &tri = mesh.triangles[i];
                AddQuad(tri[0], tri[1], tri[2], tri[2]);
                i += 1;
            }
        }
        quadID++;
    }
    mesh.triangles.clear();
}

namespace restore_vert_indices_detail {

inline int FaceDegree(const std::array<uint32_t, 4> &q) {
    return (q[2] == q[3]) ? 3 : 4;
}

// Search only the first n (3 or 4) logical slots -- for a tri, slot 3 is
// just a duplicate of slot 2 and isn't part of the cycle.
inline int FindPos(const std::array<uint32_t, 4> &arr, int n, uint32_t val) {
    for (int i = 0; i < n; ++i)
        if (arr[i] == val) return i;
    return -1;
}

struct PropagateResult {
    bool ok = false;
    std::unordered_map<uint32_t, uint32_t> additions;  // baseVert -> destVert
    std::vector<uint32_t> touchedFaces;
};

// Attempts to resolve the whole island reachable from face f0, assuming
// dest.quads[f0] is rotated by `seedRot` relative to base.quads[f0].
// Pure function: on failure, returns ok=false with no side effects on the
// caller's state (all work is staged in the returned PropagateResult).
template <typename MeshT>
inline PropagateResult TryPropagate(
    const MeshT &base, const MeshT &dest,
    const std::vector<std::vector<uint32_t>> &vertFaces,
    const std::vector<int64_t> &baseToDest,
    const std::vector<char> &faceResolved,
    uint32_t f0, int seedRot) {
    PropagateResult result;
    auto &local = result.additions;

    auto getCorr = [&](uint32_t v) -> int64_t {
        auto it = local.find(v);
        if (it != local.end()) return (int64_t)it->second;
        return baseToDest[v];
    };
    auto setCorr = [&](uint32_t v, uint32_t d) -> bool {
        int64_t cur = getCorr(v);
        if (cur != -1) return cur == (int64_t)d;  // must agree with what we already know
        local[v] = d;
        return true;
    };

    std::vector<char> queuedOrResolved(base.quads.size(), 0);
    std::queue<std::pair<uint32_t, int>> q;
    q.push({ f0, seedRot });
    queuedOrResolved[f0] = 1;

    while (!q.empty()) {
        auto [f, rot] = q.front();
        q.pop();

        int n = FaceDegree(base.quads[f]);
        int dn = FaceDegree(dest.quads[f]);
        if (n != dn) return result;  // shape mismatch -> contradiction (ok stays false)

        // Assign this poly's vertices under the hypothesized rotation.
        for (int k = 0; k < n; ++k) {
            uint32_t bv = base.quads[f][k];
            uint32_t dv = dest.quads[f][(k + rot) % n];
            if (!setCorr(bv, dv)) return result;
        }
        result.touchedFaces.push_back(f);

        // Propagate to every other poly touching any of these vertices.
        for (int k = 0; k < n; ++k) {
            uint32_t bv = base.quads[f][k];
            for (uint32_t f2 : vertFaces[bv]) {
                if (faceResolved[f2] || queuedOrResolved[f2]) continue;

                int n2 = FaceDegree(base.quads[f2]);
                int k2 = FindPos(base.quads[f2], n2, bv);
                if (k2 < 0) continue;  // shouldn't happen

                int dn2 = FaceDegree(dest.quads[f2]);
                int64_t dv = getCorr(bv);
                int p2 = FindPos(dest.quads[f2], dn2, (uint32_t)dv);
                if (p2 < 0) return result;  // bv's known counterpart isn't where it should be

                int rot2 = ((p2 - k2) % dn2 + dn2) % dn2;
                queuedOrResolved[f2] = 1;
                q.push({ f2, rot2 });
            }
        }
    }

    result.ok = true;
    return result;
}

}  // namespace restore_vert_indices_detail

template <typename Vertex, typename MeshT>
void RestoreVertIndices(MeshT const &base, std::vector<Vertex> const &baseVerts,
    MeshT &dest, std::vector<Vertex> &destVerts) {
    using namespace restore_vert_indices_detail;

    const size_t numVerts = baseVerts.size();
    const size_t numFaces = base.quads.size();

    if (destVerts.size() != numVerts)
        throw std::runtime_error("RestoreVertIndices: vertex counts differ");
    if (dest.quads.size() != numFaces)
        throw std::runtime_error("RestoreVertIndices: poly counts differ");

    // Validate shapes/ranges up front so failures are reported clearly.
    for (size_t f = 0; f < numFaces; ++f) {
        int nb = FaceDegree(base.quads[f]);
        int nd = FaceDegree(dest.quads[f]);
        if (nb != nd)
            throw std::runtime_error("RestoreVertIndices: poly " + std::to_string(f) +
                " is a tri in one mesh but a quad in the other");
        for (int k = 0; k < nb; ++k) {
            if (base.quads[f][k] >= numVerts)
                throw std::runtime_error("RestoreVertIndices: base poly " + std::to_string(f) +
                    " references an out-of-range vertex");
            if (dest.quads[f][k] >= destVerts.size())
                throw std::runtime_error("RestoreVertIndices: dest poly " + std::to_string(f) +
                    " references an out-of-range vertex");
        }
    }

    // base vertex -> polys that reference it.
    std::vector<std::vector<uint32_t>> vertFaces(numVerts);
    for (uint32_t f = 0; f < numFaces; ++f) {
        int n = FaceDegree(base.quads[f]);
        for (int k = 0; k < n; ++k)
            vertFaces[base.quads[f][k]].push_back(f);
    }

    std::vector<int64_t> baseToDest(numVerts, -1);
    std::vector<char> faceResolved(numFaces, 0);

    for (uint32_t f0 = 0; f0 < numFaces; ++f0) {
        if (faceResolved[f0]) continue;

        int n = FaceDegree(base.quads[f0]);
        std::vector<PropagateResult> successes;
        for (int r = 0; r < n; ++r) {
            auto res = TryPropagate(base, dest, vertFaces, baseToDest, faceResolved, f0, r);
            if (res.ok) successes.push_back(std::move(res));
        }

        if (successes.empty()) {
            throw std::runtime_error(
                "RestoreVertIndices: topology mismatch in the island starting at poly " +
                std::to_string(f0) + " -- base and dest don't actually correspond there");
        }
        if (successes.size() > 1) {
            //std::cerr << "RestoreVertIndices: island at poly " << f0
            //    << " is combinatorially ambiguous (" << successes.size()
            //    << " equally valid vertex orderings -- likely a fully isolated poly "
            //    "with no shared vertices to disambiguate it); picking one arbitrarily.\n";
        }

        for (auto &kv : successes[0].additions) baseToDest[kv.first] = kv.second;
        for (uint32_t f : successes[0].touchedFaces) faceResolved[f] = 1;
    }

    // Rebuild destVerts in base's vertex order.
    std::vector<Vertex> newDestVerts(numVerts);
    std::vector<char> used(destVerts.size(), 0);
    for (size_t v = 0; v < numVerts; ++v) {
        if (baseToDest[v] >= 0) {
            newDestVerts[v] = destVerts[(size_t)baseToDest[v]];
            used[(size_t)baseToDest[v]] = 1;
        }
    }
    // Vertices untouched by any poly can't be resolved topologically (there's
    // nothing to propagate through). Fill from leftovers so no data is silently
    // dropped; this is a last resort and should rarely, if ever, trigger.
    size_t leftover = 0;
    for (size_t v = 0; v < numVerts; ++v) {
        if (baseToDest[v] < 0) {
            while (leftover < destVerts.size() && used[leftover]) ++leftover;
            if (leftover < destVerts.size()) {
                newDestVerts[v] = destVerts[leftover];
                used[leftover] = 1;
            }
        }
    }

    destVerts = std::move(newDestVerts);
    dest.quads = base.quads;  // indices now aligned; base's polys are valid for dest too
}

void NormalizeObject(Object &dest, Object const &base) {
    if (dest.firstMesh().quads.empty())
        ConvertTrisToQuads(dest.firstMesh());
    try {
        RestoreVertIndices(base.firstMesh(), base.vertices, dest.firstMesh(), dest.vertices);
    }
    catch (std::exception e) {
        ::Error(e.what());
    }
}

void ConvertSkinning(Object const &base, Object &dest) {
    if (base.meshes.empty() || dest.meshes.empty()) {
        ::Error("Empty meshes");
        return;
    }
    if (base.vertices.size() != dest.vertices.size()) {
        ::Error("Vertex count doesn't match: %d (base) - %d (dest)", base.vertices.size(), dest.vertices.size());
        return;
    }
    NormalizeObject(dest, base);
    for (size_t q = 0; q < base.firstMesh().quads.size(); q++) {
        for (size_t v = 0; v < 4; v++) {
            for (size_t i = 0; i < 8; i++) {
                dest.vertices[dest.firstMesh().quads[q][v]].boneIndices[i] = base.vertices[base.firstMesh().quads[q][v]].boneIndices[i];
                dest.vertices[dest.firstMesh().quads[q][v]].boneWeights[i] = base.vertices[base.firstMesh().quads[q][v]].boneWeights[i];
            }
        }
    }
    SetNumBones(dest.vertexFormat, NumBones(base.vertexFormat));
}

Object FindLargestObject(Model const &model, std::string const &prefix) {
    std::vector<Object const *> objects;
    for (auto &o : model.objects) {
        if (o.name.starts_with(prefix))
            objects.push_back(&o);
    }
    if (!objects.empty()) {
        std::sort(objects.begin(), objects.end(),
            [](Object const *a, Object const *b) { return a->vertices.size() > b->vertices.size(); });
        return *objects[0];
    }
    ::Error("Unable to find " + prefix + " in " + model.name);
    return Object();
}

struct DeformationData {
    std::vector<Vector3> vertexDeform;
    Vector3 baseDeform;
};

DeformationData GetDeformationData(Model const &base, Model const &deformed, Object const &source) {
    DeformationData data;
    auto headBase = FindLargestObject(base, "head");
    auto headDeformed = FindLargestObject(deformed, "head");
    if (headBase.meshes.empty() || headDeformed.meshes.empty()) {
        ::Error("Empty meshes");
        return data;
    }
    ::Error("%d", headBase.firstMesh().quads.size());
    NormalizeObject(headBase, source);
    NormalizeObject(headDeformed, source);
    if (headBase.vertices.size() != headDeformed.vertices.size() || headBase.vertices.size() != source.vertices.size()) {
        ::Error("GetDeformationData: Vertex count doesn't match (%d/%d/%d)", headBase.vertices.size(), headDeformed.vertices.size(), source.vertices.size());
        return data;
    }
    if (headBase.firstMesh().quads.size() != headDeformed.firstMesh().quads.size() ||
        headBase.firstMesh().quads.size() != source.firstMesh().quads.size())
    {
        ::Error("GetDeformationData: Poly count doesn't match (%d/%d/%d)",
            headBase.firstMesh().quads.size(), headDeformed.firstMesh().quads.size(), source.firstMesh().quads.size());
        return data;
    }
    int ExpectedPolyCount = 3160;
    if (headBase.firstMesh().quads.size() != ExpectedPolyCount) {
        ::Error("GetDeformationData: Poly count is not %d", ExpectedPolyCount);
        return data;
    }
    if (headBase.firstMesh().quads.back()[2] != headBase.firstMesh().quads.back()[3]) {
        ::Error("GetDeformationData: Base poly is not a triangle in base object", ExpectedPolyCount);
        return data;
    }
    if (headDeformed.firstMesh().quads.back()[2] != headDeformed.firstMesh().quads.back()[3]) {
        ::Error("GetDeformationData: Base poly is not a triangle in deformed object", ExpectedPolyCount);
        return data;
    }
    data.vertexDeform.resize(headBase.vertices.size());
    for (size_t i = 0; i < headBase.vertices.size(); i++)
        data.vertexDeform[i] = headDeformed.vertices[i].pos - headBase.vertices[i].pos;
    size_t BaseVertexID = headBase.firstMesh().quads.back()[2];
    data.baseDeform = headDeformed.vertices[BaseVertexID].pos - headBase.vertices[BaseVertexID].pos;
    return data;
}

void ApplyDeformations(Object &obj, DeformationData const &deformation, bool headDeformation) {
    if (obj.meshes.empty()) {
        ::Error("ApplyDeformations: Empty meshes");
        return;
    }
    if (headDeformation && obj.vertices.size() != deformation.vertexDeform.size()) {
        ::Error("ApplyDeformations: Vertex count doesn't match (%d/%d)", obj.vertices.size(), deformation.vertexDeform.size());
        return;
    }
    if (headDeformation) {
        for (size_t i = 0; i < obj.vertices.size(); i++)
            obj.vertices[i].pos -= deformation.vertexDeform[i];
    }
    else {
        for (size_t i = 0; i < obj.vertices.size(); i++)
            obj.vertices[i].pos -= deformation.baseDeform;
    }
}

int main() {
    //std::filesystem::path folder = R"(C:\Users\User\Desktop\faces_fbx)";
    //ModelOptions options;
    //options.AllowQuads = true;
    //Model dest(folder / "fc26.fbx", options);
    //Model source(folder / "fifa16.fbx", options);
    //auto head = FindLargestObject(dest, "head");
    //auto eyes = FindLargestObject(dest, "eyes");
    //auto sourceHead = FindLargestObject(source, "head");
    //ConvertSkinning(sourceHead, head);
    //auto deformation = GetDeformationData(
    //    Model(folder / "deform_base.fbx", options), 
    //    Model(folder / "deform_target.fbx", options), sourceHead);
    //ApplyDeformations(head, deformation, true);
    //ApplyDeformations(eyes, deformation, false);
    //eyes.SetBone(source.GetBoneIndex("Face"));
    //dest.objects = { head, eyes };
    //dest.skeleton = source.skeleton;
    //dest.WriteFbx(folder / "fc26to16.fbx");
    Model m;
    m.ReadFbx("untitled.fbx");
    for (auto const &o : m.objects)
        ::Message(o.name);
}
