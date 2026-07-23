#include "MeshSkinning.h"

namespace MeshSkinning {

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

}
