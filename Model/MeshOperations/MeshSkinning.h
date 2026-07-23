#pragma once
#include "ModelClass.h"

namespace MeshSkinning {

enum eSkeletonBoneSplitType {
    SKELETON_BONE_SPLIT_SIMPLE,
    SKELETON_BONE_SPLIT_AABB,
    SKELETON_BONE_SPLIT_OBB
};

enum eSkeletonBoneConversionCommand {
    SKELETON_BONE_CONVERSION_COMMAND_FILTER, // filter(0.2,1.0)
    SKELETON_BONE_CONVERSION_COMMAND_CLAMP,  // clamp (0.2,0.8)
    SKELETON_BONE_CONVERSION_COMMAND_NORMALIZE, // normalize
    SKELETON_BONE_CONVERSION_COMMAND_MULTIPLY, // mul(0.5)
    SKELETON_BONE_CONVERSION_COMMAND_DIVIDE, // div(2.0)
    SKELETON_BONE_CONVERSION_COMMAND_ADD, // add(0.1)
    SKELETON_BONE_CONVERSION_COMMAND_SUBSTRACT, // sub(0.1)
};

struct SkeletonBoneConversionCommand {
    eSkeletonBoneConversionCommand type;
    std::vector<float> params;
};

struct SkeletonBoneSplit {
    std::string boneName;
    std::vector<SkeletonBoneConversionCommand> commands;
    float weight = 1.0f;
    float points[3] = {};
};

struct SkeletonBoneConversion {
    eSkeletonBoneSplitType type = SKELETON_BONE_SPLIT_SIMPLE;
    std::vector<SkeletonBoneConversionCommand> commands;
    std::vector<SkeletonBoneSplit> split;
    // sourceBone, commands, splitType, targetBone1, splitParams1, commands1, targetBone2, ...
};

void SetVertexBones(Vertex &v, std::vector<std::pair<uint16_t, float>> newBones, bool normalize = true);
void RetargetSkeleton(Model &model, Skeleton const &newSkeleton);
//void ConvertSkeleton(Model &model, Skeleton const &newSkeleton, std::map<std::string, SkeletonBoneConversion> const &conversion);

}
