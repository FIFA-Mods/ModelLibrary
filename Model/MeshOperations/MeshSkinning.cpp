#include "MeshSkinning.h"

namespace MeshSkinning {

std::vector<std::pair<uint16_t, float>> GetVertexBones(Vertex const &v, uint8_t numBonesPerVertex) {
    std::vector<std::pair<uint16_t, float>> result;
    for (uint8_t b = 0; b < numBonesPerVertex; b++) {
        if (v.boneWeights[b] > 0.0f)
            result.emplace_back(v.boneIndices[b], v.boneWeights[b]);
    }
    return result;
}

void SetVertexBones(Vertex &v, std::vector<std::pair<uint16_t, float>> newBones, bool normalize) {
    if (!newBones.empty()) {
        if (normalize) {
            if (newBones.size() == 1)
                newBones[0].second = 1.0f;
            else {
                std::stable_sort(newBones.begin(), newBones.end(),
                    [](std::pair<uint16_t, float> const &a, std::pair<uint16_t, float> const &b) {
                    return a.second > b.second;
                });
                float sum = 0.0f;
                for (auto const &bone : newBones)
                    sum += bone.second;
                if (sum > 0.0f) {
                    for (auto &bone : newBones)
                        bone.second /= sum;
                }
            }
        }
    }
    for (size_t b = 0; b < 8; b++) {
        if (b < newBones.size()) {
            v.boneIndices[b] = newBones[b].first;
            v.boneWeights[b] = newBones[b].second;
        }
        else {
            v.boneIndices[b] = 0;
            v.boneWeights[b] = 0.0f;
        }
    }
}

void RetargetSkeleton(Model &model, Skeleton const &newSkeleton) {
    std::map<std::string, uint16_t> newBoneIndices;
    for (uint16_t bi = 0; bi < newSkeleton.bones.size(); bi++)
        newBoneIndices[newSkeleton.bones[bi].name] = bi;
    for (auto &o : model.objects) {
        uint8_t numBonesPerVertex = 0;
        for (auto &v : o.vertices) {
            std::vector<std::pair<uint16_t, float>> newBones;
            for (size_t b = 0; b < NumBones(o.vertexFormat); b++) {
                if (v.boneWeights[b] > 0.0f && v.boneIndices[b] < model.skeleton.bones.size()) {
                    auto tbiIt = newBoneIndices.find(model.skeleton.bones[v.boneIndices[b]].name);
                    if (tbiIt != newBoneIndices.end()) {
                        uint16_t targetIdx = tbiIt->second;
                        auto existing = std::find_if(newBones.begin(), newBones.end(),
                            [targetIdx](auto const &p) { return p.first == targetIdx; });
                        if (existing != newBones.end())
                            existing->second += v.boneWeights[b];
                        else
                            newBones.emplace_back(targetIdx, v.boneWeights[b]);
                    }
                }
            }
            MeshSkinning::SetVertexBones(v, newBones, true);
            numBonesPerVertex = std::max(static_cast<uint8_t>(newBones.size()), numBonesPerVertex);
        }
        SetNumBones(o.vertexFormat, numBonesPerVertex);
    }
    model.skeleton = newSkeleton;
}

void LimitBonesPerVertex(Object &object, uint8_t maxBonesPerVertex) {
    uint8_t numBones = NumBones(object.vertexFormat);
    if (numBones > maxBonesPerVertex) {
        for (auto &v : object.vertices) {
            auto bones = GetVertexBones(v, numBones);
            if (bones.size() > maxBonesPerVertex) {
                bones.resize(maxBonesPerVertex);
                SetVertexBones(v, bones, true);
            }
        }
        SetNumBones(object.vertexFormat, maxBonesPerVertex);
    }
}

void LimitBonesPerVertex(Model &model, uint8_t maxBonesPerVertex) {
    for (auto &o : model.objects)
        LimitBonesPerVertex(o, maxBonesPerVertex);
}

std::vector<Matrix4x4> ComputeGlobalTransforms(const Skeleton &skel) {
    std::unordered_map<std::string, uint16_t> nameToIndex;
    for (uint16_t i = 0; i < skel.bones.size(); ++i)
        nameToIndex[skel.bones[i].name] = i;

    std::vector<Matrix4x4> global(skel.bones.size());
    std::vector<bool> computed(skel.bones.size(), false);

    std::function<Matrix4x4(uint16_t)> resolve = [&](uint16_t idx) -> Matrix4x4 {
        if (computed[idx]) return global[idx];
        const Bone &b = skel.bones[idx];

        Matrix4x4 result;
        auto it = b.parent.empty() ? nameToIndex.end() : nameToIndex.find(b.parent);
        if (it != nameToIndex.end()) {
            // Matches FBX SDK convention: Global_child = Global_parent * Local_child
            result = resolve(it->second) * b.matrix;
        }
        else {
            result = b.matrix; // root bone, no parent in this skeleton
        }

        global[idx] = result;
        computed[idx] = true;
        return result;
    };

    for (uint16_t i = 0; i < skel.bones.size(); ++i)
        resolve(i);

    return global;
}

std::unordered_map<std::string, Matrix4x4> ComputeBoneDiffMatrices(const Skeleton &poseFrom, const Skeleton &poseTo) {
    auto globalFrom = ComputeGlobalTransforms(poseFrom);
    auto globalTo = ComputeGlobalTransforms(poseTo);

    std::unordered_map<std::string, Matrix4x4> globalToByName;
    for (size_t i = 0; i < poseTo.bones.size(); ++i)
        globalToByName[poseTo.bones[i].name] = globalTo[i];

    std::unordered_map<std::string, Matrix4x4> diffs;
    for (size_t i = 0; i < poseFrom.bones.size(); ++i) {
        auto it = globalToByName.find(poseFrom.bones[i].name);
        if (it == globalToByName.end())
            continue; // bone not present in target pose skeleton — skip (treated as identity later)

        // Diff * G_from = G_to  =>  Diff = G_to * Inverse(G_from)
        diffs[poseFrom.bones[i].name] = it->second * globalFrom[i].Inversed();
    }
    return diffs;
}

Matrix4x4 BlendMatrices(const std::vector<std::pair<Matrix4x4, float>> &weighted) {
    float sum = 0.0f;
    for (auto &wm : weighted) sum += wm.second;
    if (sum <= 0.0f) return Matrix4x4::Identity();

    Matrix4x4 result;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            result.m[i][j] = 0.0f;

    for (auto &wm : weighted) {
        float w = wm.second / sum; // defensive re-normalize
        const Matrix4x4 &mat = wm.first;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                result.m[i][j] += mat.m[i][j] * w;
    }
    return result;
}

Vector3 Normalize(const Vector3 &v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return (len > 1e-8f) ? Vector3(v.x / len, v.y / len, v.z / len) : v;
}

void ApplyBoneDiffToObject(Object &obj, const Skeleton &modelSkeleton, const std::unordered_map<std::string, Matrix4x4> &boneDiffsByName) {
    std::vector<Matrix4x4> boneDiffs(modelSkeleton.bones.size(), Matrix4x4::Identity());
    for (size_t i = 0; i < modelSkeleton.bones.size(); ++i) {
        auto it = boneDiffsByName.find(modelSkeleton.bones[i].name);
        if (it != boneDiffsByName.end())
            boneDiffs[i] = it->second;
    }

    for (Vertex &v : obj.vertices) {
        auto bones = MeshSkinning::GetVertexBones(v, NumBones(obj.vertexFormat));
        if (bones.empty()) continue;

        std::vector<std::pair<Matrix4x4, float>> weighted;
        weighted.reserve(bones.size());
        for (auto &[boneIdx, weight] : bones) {
            if (boneIdx < boneDiffs.size())
                weighted.emplace_back(boneDiffs[boneIdx], weight);
        }
        if (weighted.empty()) continue;

        Matrix4x4 blended = BlendMatrices(weighted);

        v.pos = blended * v.pos;

        Matrix4x4 rotOnly = blended;
        rotOnly.SetTranslation(Vector3(0.0f, 0.0f, 0.0f));
        v.normal = Normalize(rotOnly * v.normal);
        v.tangent = Normalize(rotOnly * v.tangent);
        v.binormal = Normalize(rotOnly * v.binormal);
    }
}

void ChangePose(Model &model, const std::unordered_map<std::string, Matrix4x4> &boneDiffs) {
    for (Object &obj : model.objects)
        ApplyBoneDiffToObject(obj, model.skeleton, boneDiffs);
}

void ChangePose(Model &model, const Skeleton &poseFrom, const Skeleton &poseTo) {
    auto boneDiffs = ComputeBoneDiffMatrices(poseFrom, poseTo);
    for (Object &obj : model.objects)
        ApplyBoneDiffToObject(obj, model.skeleton, boneDiffs);
}

}
