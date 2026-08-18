#include "ModelTransform.h"

namespace {

Matrix4x4 StripTranslation(Matrix4x4 mat) {
    mat.SetTranslation(Vector3(0.0f, 0.0f, 0.0f));
    return mat;
}

Matrix4x4 Transpose3x3(Matrix4x4 const &mat) {
    Matrix4x4 result = Matrix4x4::Identity();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            result.m[i][j] = mat.m[j][i];
    return result;
}

Matrix4x4 NormalMatrix(Matrix4x4 const &mat) {
    return Transpose3x3(mat.Inversed());
}

std::vector<size_t> DirectChildren(std::vector<Bone> const &bones, std::string const &parentName) {
    std::vector<size_t> result;
    for (size_t i = 0; i < bones.size(); ++i)
        if (bones[i].parent == parentName) result.push_back(i);
    return result;
}

void CollectSubtree(std::vector<Bone> const &bones, std::string const &parentName,
    std::vector<size_t> &out) {
    for (size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].parent == parentName) {
            out.push_back(i);
            CollectSubtree(bones, bones[i].name, out);
        }
    }
}

}

namespace ModelTransform {

void ApplyModelTransforms(Model &model, bool applySkeletonTransforms) {
    std::unordered_map<std::string, size_t> objectsByName;
    for (size_t i = 0; i < model.objects.size(); ++i)
        objectsByName[model.objects[i].name] = i;
    std::vector<Matrix4x4> globalTransforms(model.objects.size());
    std::vector<bool> computed(model.objects.size(), false);
    std::function<Matrix4x4 const &(size_t)> getGlobal = [&](size_t index) -> Matrix4x4 const & {
        if (computed[index])
            return globalTransforms[index];
        computed[index] = true; // guards against cyclic parent references
        Object const &obj = model.objects[index];
        auto it = obj.parent.empty() ? objectsByName.end() : objectsByName.find(obj.parent);
        globalTransforms[index] = (it == objectsByName.end() || it->second == index)
            ? obj.transform
            : getGlobal(it->second) * obj.transform;
        return globalTransforms[index];
    };
    for (size_t i = 0; i < model.objects.size(); ++i) {
        Object &obj = model.objects[i];
        Matrix4x4 const &global = getGlobal(i);
        if (!global.IsIdentity()) {
            Matrix4x4 const linear = StripTranslation(global);
            Matrix4x4 const normalMat = NormalMatrix(global);
            for (Vertex &v : obj.vertices) {
                v.pos = global * v.pos;
                if (obj.vertexFormat & V_Normal) {
                    v.normal = normalMat * v.normal;
                    v.normal.NormalizeSafe();
                }
                if (obj.vertexFormat & V_Tangent) {
                    v.tangent = linear * v.tangent;
                    v.tangent.NormalizeSafe();
                }
                if (obj.vertexFormat & V_Binormal) {
                    v.binormal = linear * v.binormal;
                    v.binormal.NormalizeSafe();
                }
            }
            for (ShapeKey &key : obj.shapeKeys) {
                for (ShapeKeyVertex &skv : key.vertices) {
                    skv.deltaPos = linear * skv.deltaPos;
                    if (obj.vertexFormat & V_Normal)
                        skv.deltaNormal = normalMat * skv.deltaNormal;
                }
            }
        }
        obj.transform = Matrix4x4::Identity();
    }
    if (applySkeletonTransforms) {
        std::vector<Bone> &bones = model.skeleton.bones;
        std::unordered_map<std::string, size_t> bonesByName;
        for (size_t i = 0; i < bones.size(); ++i)
            bonesByName[bones[i].name] = i;
        for (size_t i = 0; i < bones.size(); ++i) {
            Bone &root = bones[i];
            bool isRoot = root.parent.empty() || bonesByName.find(root.parent) == bonesByName.end();
            if (!isRoot || root.matrix.IsIdentity())
                continue;
            Vector3 const s0 = root.matrix.GetScaling();
            Matrix4x4 r0 = root.matrix;
            r0.SetScaling(Vector3(1.0f, 1.0f, 1.0f));
            root.matrix = Matrix4x4::Identity();
            std::vector<size_t> directChildren = DirectChildren(bones, root.name);
            for (size_t childIdx : directChildren)
                bones[childIdx].matrix = r0 * bones[childIdx].matrix;
            std::vector<size_t> subtree;
            CollectSubtree(bones, root.name, subtree);
            for (size_t idx : subtree) {
                Vector3 const t = bones[idx].matrix.GetTranslation();
                bones[idx].matrix.SetTranslation(Vector3(t.x * s0.x, t.y * s0.y, t.z * s0.z));
            }
        }
    }
}

}
