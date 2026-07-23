#include "Model.h"
#include <iostream>
#include <fstream>
#include "ModelFbxSdkHeader.h"
#include "ModelTypeConversion.h"
#include "MeshOperations/MeshTriangulation.h"
#include "MeshOperations/MeshSkinning.h"

namespace model_helper::model_utils {

std::wstring ToLower(std::wstring const &str) {
    std::wstring result;
    for (size_t i = 0; i < str.length(); i++)
        result += tolower(static_cast<unsigned short>(str[i]));
    return result;
}

std::string ToLower(std::string const &str) {
    std::string result;
    for (size_t i = 0; i < str.length(); i++)
        result += tolower(static_cast<unsigned char>(str[i]));
    return result;
}

}

using namespace model_helper::model_utils;

void SetNumTexCoords(uint32_t &format, uint8_t n) {
    format = (format & ~(0xF << 3)) | ((n & 0xF) << 3);
}

void SetNumColors(uint32_t &format, uint8_t n) {
    format = (format & ~(0xF << 7)) | ((n & 0xF) << 7);
}

void SetNumBones(uint32_t &format, uint8_t n) {
    format = (format & ~(0xF << 11)) | ((n & 0xF) << 11);
}

Material::Material() {}

Material::Material(std::string const &n) : name(n) {}

Material::Material(std::string const &n, std::string const &tex) : name(n), texture(tex) {}

Material::Material(std::string const &n, std::string const &tex, RGBA const &col) : name(n), texture(tex), color(col) {}

//const size_t MAX_VERTICES_UINT16 = 65'535;
//const size_t MAX_VERTICES_INT16 = 32'767;

Model::Model() {}

Model::Model(std::filesystem::path const &fbxFileName, ModelOptions const &options) {
    Read(fbxFileName, options);
}

void Model::Read(std::filesystem::path const &filename, ModelOptions const &options) {
    Clear();
    auto ext = ToLower(filename.extension().string());
    if (ext == ".fbx")
        ReadFbx(filename, options);
    else if (ext == ".obj")
        ReadObj(filename, options);
}

void Model::Write(std::filesystem::path const &filename, ModelOptions const &options) {
    auto ext = ToLower(filename.extension().string());
    if (ext == ".fbx")
        WriteFbx(filename, options);
    else if (ext == ".obj")
        WriteObj(filename, options);
}

std::string Model::GenerateObjectName() const {
    std::string baseName = "object";
    std::set<std::string> existingNames;
    for (const auto &obj : objects)
        existingNames.insert(obj.name);
    if (!existingNames.count(baseName))
        return baseName;
    for (size_t index = 1; ; ++index) {
        std::ostringstream oss;
        oss << baseName << '.' << std::setw(3) << std::setfill('0') << index;
        std::string candidate = oss.str();
        if (!existingNames.count(candidate))
            return candidate;
    }
    return std::string();
}

void Model::Clear() {
    name.clear();
    objects.clear();
    materials.clear();
    textures.clear();
    skeleton.bones.clear();
    properties.clear();
}

void ModelPostLoadProcess(Model &model, ModelOptions const &options) {
    if (options.MergeMeshes) {
        for (auto &o : model.objects)
            o.MergeMeshes();
    }
}

void Model::DumpSkeletonHierarchy(const Skeleton &skeleton) {
    std::cout << std::endl << std::endl << "Skeleton Hierarchy:" << std::endl;
    if (skeleton.bones.empty()) {
        std::cout << "(no bones)\n";
        return;
    }

    // name -> index for quick lookup
    std::unordered_map<std::string, size_t> nameToIndex;
    nameToIndex.reserve(skeleton.bones.size());
    for (size_t i = 0; i < skeleton.bones.size(); ++i)
        nameToIndex[skeleton.bones[i].name] = i;

    // build parent -> children lists (preserve insertion order)
    std::unordered_map<std::string, std::vector<std::string>> children;
    std::vector<std::string> roots;
    roots.reserve(skeleton.bones.size());
    std::unordered_set<std::string> seenRoot;

    for (const Bone &b : skeleton.bones) {
        const std::string &name = b.name;
        const std::string &parent = b.parent;
        if (!parent.empty() && nameToIndex.find(parent) != nameToIndex.end()) {
            children[parent].push_back(name);
        }
        else {
            // parent missing or empty -> treat as root (preserve first-seen order)
            if (seenRoot.insert(name).second)
                roots.push_back(name);
        }
    }

    // If we found no roots (odd case), fallback to printing all bones in original order
    if (roots.empty()) {
        for (const Bone &b : skeleton.bones)
            std::cout << b.name << '\n';
        return;
    }

    // recursive emitter
    std::function<void(const std::string &, int)> emit = [&](const std::string &nm, int depth) {
        std::cout << std::string(depth * 2, ' ') << nm << '\n';
        auto it = children.find(nm);
        if (it == children.end()) return;
        for (const std::string &childName : it->second)
            emit(childName, depth + 1);
    };

    // emit each root
    for (const std::string &r : roots)
        emit(r, 0);
}

Model &Model::operator+=(Model const &other) {
    auto make_unique_name = [](std::string const &base, std::unordered_set<std::string> &existing) {
        if (existing.find(base) == existing.end()) {
            existing.insert(base);
            return base;
        }
        for (int i = 1; i <= 999; ++i) {
            std::ostringstream oss;
            oss << base << '.' << std::setw(3) << std::setfill('0') << i;
            std::string candidate = oss.str();
            if (existing.find(candidate) == existing.end()) {
                existing.insert(candidate);
                return candidate;
            }
        }
        std::string fallback = base + "_dup";
        int suffix = 0;
        while (existing.find(fallback + std::to_string(suffix)) != existing.end()) ++suffix;
        fallback += std::to_string(suffix);
        existing.insert(fallback);
        return fallback;
    };
    auto bone_equal = [](Bone const &A, Bone const &B) {
        if (A.name != B.name)
            return false;
        if (A.parent != B.parent)
            return false;
        if (A.properties.size() != B.properties.size()) // TODO: check this
            return false;
        for (auto const &[k, v] : A.properties) {
            auto it = B.properties.find(k);
            if (it == B.properties.end())
                return false;
        }
        return true;
    };
    auto skeleton_equal = [&bone_equal](Skeleton const &S1, Skeleton const &S2) {
        if (S1.bones.size() != S2.bones.size())
            return false;
        for (size_t i = 0; i < S1.bones.size(); ++i) {
            if (!bone_equal(S1.bones[i], S2.bones[i]))
                return false;
        }
        return true;
    };
    std::unordered_set<std::string> existing_object_names;
    existing_object_names.reserve(objects.size() + other.objects.size());
    for (auto const &o : objects) existing_object_names.insert(o.name);
    for (auto obj : other.objects) {
        if (existing_object_names.find(obj.name) == existing_object_names.end()) {
            existing_object_names.insert(obj.name);
            objects.push_back(std::move(obj));
        }
        else {
            std::string newname = make_unique_name(obj.name, existing_object_names);
            obj.name = std::move(newname);
            objects.push_back(std::move(obj));
        }
    }
    std::unordered_set<std::string> material_names;
    material_names.reserve(materials.size() + other.materials.size());
    for (auto const &m : materials) material_names.insert(m.name);
    for (auto const &m : other.materials) {
        if (material_names.insert(m.name).second)
            materials.push_back(m);
    }
    std::unordered_set<std::string> texture_names;
    texture_names.reserve(textures.size() + other.textures.size());
    for (auto const &t : textures) texture_names.insert(t.name);
    for (auto const &t : other.textures) {
        if (texture_names.insert(t.name).second)
            textures.push_back(t);
    }
    bool this_has = !skeleton.bones.empty();
    bool other_has = !other.skeleton.bones.empty();
    if (!this_has && other_has)
        skeleton = other.skeleton;
    else if (this_has && other_has && !skeleton_equal(skeleton, other.skeleton))
        throw std::runtime_error("Cannot merge models: skeletons differ");
    for (auto const &[k, v] : other.properties) {
        if (properties.find(k) == properties.end())
            properties.emplace(k, v);
    }
    return *this;
}

Model operator+(Model lhs, Model const &rhs) {
    lhs += rhs;
    return lhs;
}

const Mesh &Object::firstMesh() const {
    if (meshes.empty())
        meshes.push_back(Mesh());
    return meshes[0];
}

Mesh &Object::firstMesh() {
    return const_cast<Mesh &>(std::as_const(*this).firstMesh());
}

void Object::ClearSkinning() {
    SetNumBones(vertexFormat, 0);
    for (auto &v : vertices) {
        for (size_t i = 0; i < 8; i++) {
            v.boneIndices[i] = 0;
            v.boneWeights[i] = 0.0f;
        }
    }
}
void Object::SetBone(int boneIndex) {
    ClearSkinning();
    if (boneIndex != -1) {
        SetNumBones(vertexFormat, 1);
        for (auto &v : vertices) {
            v.boneIndices[0] = boneIndex;
            v.boneWeights[0] = 1.0f;
        }
    }
}

void Object::MergeMeshes() {
    if (meshes.size() <= 1)
        return;
    Mesh newMesh = meshes[0];
    for (size_t i = 1; i < meshes.size(); i++) {
        auto const &m = meshes[i];
        if (!m.polygons.empty())
            newMesh.polygons.insert(newMesh.polygons.end(), m.polygons.begin(), m.polygons.end());
        if (!m.properties.empty()) {
            for (auto const &[k, v] : m.properties) {
                if (newMesh.properties.find(k) == newMesh.properties.end())
                    newMesh.properties.emplace(k, v);
            }
        }
    }
    meshes.clear();
    meshes.push_back(newMesh);
}

bool Object::IsTriangulated() const {
    for (auto &m : meshes) if (!m.IsTriangulated()) return false;
    return true;
}

bool Object::IsOnlyTrisAndQuads() const {
    for (auto &m : meshes) if (!m.IsOnlyTrisAndQuads()) return false;
    return true;
}

void Object::Triangulate() {
    for (auto &m : meshes) m.Triangulate(vertices);
}

void Object::LeaveTrisAndQuads() {
    for (auto &m : meshes) m.LeaveTrisAndQuads(vertices);
}

int Skeleton::GetBoneIndex(std::string const &boneName) const {
    auto it = std::find_if(bones.begin(), bones.end(), [&boneName](Bone const &bone) {
        return bone.name == boneName;
    });
    return (it == bones.end()) ? -1 : std::distance(bones.begin(), it);
}

int Model::GetBoneIndex(std::string const &boneName) const {
    return skeleton.GetBoneIndex(boneName);
}

void TrimObjectName(std::string &str) {
    size_t dot_pos = str.find('.');
    if (dot_pos != std::string::npos)
        str.erase(dot_pos);
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        str.clear();
        return;
    }
    size_t end = str.find_last_not_of(" \t\r\n");
    str = str.substr(start, end - start + 1);
}

std::string TrimmedObjectName(std::string const &str) {
    std::string result = str;
    TrimObjectName(result);
    return result;
}

Object const *Model::GetObjectByName(std::string const &objectName, bool trimNames) const {
    auto searchName = objectName;
    if (trimNames)
        TrimObjectName(searchName);
    for (auto &o : objects) {
        auto objName = o.name;
        if (trimNames)
            TrimObjectName(objName);
        if (searchName == objName)
            return &o;
    }
    return nullptr;
}

Object *Model::GetObjectByName(std::string const &objectName, bool trimNames) {
    return const_cast<Object *>(std::as_const(*this).GetObjectByName(objectName, trimNames));
}

bool Model::IsSkeleton() const {
    return objects.empty() && !skeleton.bones.empty();
}

bool Model::HasShapeKeys() const {
    bool hasShapeKeys = false;
    for (auto const &o : objects) {
        if (!o.shapeKeys.empty()) {
            hasShapeKeys = true;
            break;
        }
    }
    return hasShapeKeys;
}

bool Model::IsTriangulated() const {
    for (auto &o : objects) if (!o.IsTriangulated()) return false;
    return true;
}

bool Model::IsOnlyTrisAndQuads() const {
    for (auto &o : objects) if (!o.IsOnlyTrisAndQuads()) return false;
    return true;
}

void Model::Triangulate() {
    for (auto &o : objects) o.Triangulate();
}

void Model::LeaveTrisAndQuads() {
    for (auto &o : objects) o.LeaveTrisAndQuads();
}

void Model::RetargetSkeleton(Skeleton const &newSkeleton) {
    MeshSkinning::RetargetSkeleton(*this, newSkeleton);
}

bool Mesh::IsTriangulated() const {
    for (auto &p : polygons) if (p.size() != 3) return false;
    return true;
}

bool Mesh::IsOnlyTrisAndQuads() const {
    for (auto &p : polygons) if (p.size() != 3 && p.size() != 4) return false;
    return true;
}

void Mesh::Triangulate(const std::vector<Vertex> &vertices) {
    if (IsTriangulated()) return;
    polygons = MeshTriangulation::Triangulate(polygons, vertices);
}

void Mesh::LeaveTrisAndQuads(const std::vector<Vertex> &vertices) {
    if (IsOnlyTrisAndQuads()) return;
    polygons = MeshTriangulation::LeaveTrisAndQuads(polygons, vertices);
}
