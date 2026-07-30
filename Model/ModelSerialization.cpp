#pragma once
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include "ModelClass.h"

template <typename BasicJsonType>
void to_json(BasicJsonType &j, Vector2 const &v) {
    j = { {"x", v.x}, {"y", v.y} };
}

template <typename BasicJsonType>
void from_json(BasicJsonType const &j, Vector2 &v) {
    j.at("x").get_to(v.x);
    j.at("y").get_to(v.y);
}

template <typename BasicJsonType>
void to_json(BasicJsonType &j, Vector3 const &v) {
    j = { {"x", v.x}, {"y", v.y}, {"z", v.z} };
}

template <typename BasicJsonType>
void from_json(BasicJsonType const &j, Vector3 &v) {
    j.at("x").get_to(v.x);
    j.at("y").get_to(v.y);
    j.at("z").get_to(v.z);
}

template <typename BasicJsonType>
void to_json(BasicJsonType &j, Vector4 const &v) {
    j = { {"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w} };
}

template <typename BasicJsonType>
void from_json(BasicJsonType const &j, Vector4 &v) {
    j.at("x").get_to(v.x);
    j.at("y").get_to(v.y);
    j.at("z").get_to(v.z);
    j.at("w").get_to(v.w);
}

template <typename BasicJsonType>
void to_json(BasicJsonType &j, RGBA const &c) {
    j = { {"r", c.r}, {"g", c.g}, {"b", c.b}, {"a", c.a} };
}

template <typename BasicJsonType>
void from_json(BasicJsonType const &j, RGBA &c) {
    j.at("r").get_to(c.r);
    j.at("g").get_to(c.g);
    j.at("b").get_to(c.b);
    j.at("a").get_to(c.a);
}

template <typename BasicJsonType>
void to_json(BasicJsonType &j, Matrix4x4 const &mat) {
    j = BasicJsonType::array();
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            j.push_back(mat.m[r][c]);
}

template <typename BasicJsonType>
void from_json(BasicJsonType const &j, Matrix4x4 &mat) {
    if (j.size() != 16) throw std::runtime_error("Matrix4x4: expected 16 elements");
    size_t idx = 0;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            j.at(idx++).get_to(mat.m[r][c]);
}

inline bool IsIdentityMatrix(Matrix4x4 const &mat) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float const expected = (r == c) ? 1.0f : 0.0f;
            if (mat.m[r][c] != expected) return false;
        }
    return true;
}

inline Matrix4x4 MakeIdentityMatrix() {
    Matrix4x4 mat{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            mat.m[r][c] = (r == c) ? 1.0f : 0.0f;
    return mat;
}

template <typename BasicJsonType>
void to_json(BasicJsonType &j, PropertyValue const &pv) {
    std::visit([&j](auto const &val) {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, int>)         j = { {"type", "int"},     {"value", val} };
        else if constexpr (std::is_same_v<T, float>)       j = { {"type", "float"},   {"value", val} };
        else if constexpr (std::is_same_v<T, double>)      j = { {"type", "double"},  {"value", val} };
        else if constexpr (std::is_same_v<T, std::string>) j = { {"type", "string"},  {"value", val} };
        else if constexpr (std::is_same_v<T, Vector2>)     j = { {"type", "vector2"}, {"value", val} };
        else if constexpr (std::is_same_v<T, Vector3>)     j = { {"type", "vector3"}, {"value", val} };
        else if constexpr (std::is_same_v<T, Vector4>)     j = { {"type", "vector4"}, {"value", val} };
        else if constexpr (std::is_same_v<T, Matrix4x4>)   j = { {"type", "matrix4x4"}, {"value", val} };
        else static_assert(!sizeof(T), "Unhandled PropertyValue alternative");
    }, pv);
}

template <typename BasicJsonType>
void from_json(BasicJsonType const &j, PropertyValue &pv) {
    std::string const type = j.at("type").template get<std::string>();
    BasicJsonType const &value = j.at("value");

    if (type == "int")            pv = value.template get<int>();
    else if (type == "float")     pv = value.template get<float>();
    else if (type == "double")    pv = value.template get<double>();
    else if (type == "string")    pv = value.template get<std::string>();
    else if (type == "vector2")   pv = value.template get<Vector2>();
    else if (type == "vector3")   pv = value.template get<Vector3>();
    else if (type == "vector4")   pv = value.template get<Vector4>();
    else if (type == "matrix4x4") pv = value.template get<Matrix4x4>();
    else throw std::runtime_error("PropertyValue: unknown type tag '" + type + "'");
}

inline std::string PluralizeCountToken(uint8_t n, char const *singular, char const *plural) {
    return std::to_string(n) + (n == 1 ? singular : plural);
}

inline std::string VertexFormatToString(uint32_t format) {
    if (format == 0)
        return std::string();

    std::string result = "Position";

    if (format & V_Normal)   result += "|Normal";
    if (format & V_Tangent)  result += "|Tangent";
    if (format & V_Binormal) result += "|Binormal";

    if (uint8_t const n = static_cast<uint8_t>(NumTexCoords(format)); n > 0)
        result += "|" + PluralizeCountToken(n, "TexCoord", "TexCoords");
    if (uint8_t const n = static_cast<uint8_t>(NumColors(format)); n > 0)
        result += "|" + PluralizeCountToken(n, "Color", "Colors");
    if (uint8_t const n = static_cast<uint8_t>(NumBones(format)); n > 0)
        result += "|" + PluralizeCountToken(n, "Bone", "Bones");

    return result;
}

inline uint32_t VertexFormatFromString(std::string const &s) {
    uint32_t format = 0;
    size_t start = 0;

    while (start <= s.size()) {
        size_t const bar = s.find('|', start);
        std::string const token = (bar == std::string::npos) ? s.substr(start) : s.substr(start, bar - start);

        if (!token.empty()) {
            auto endsWith = [&token](char const *suffix) {
                size_t const len = std::string(suffix).size();
                return token.size() > len && token.compare(token.size() - len, len, suffix) == 0;
            };

            if (token == "Position") {
                // implicit, no bit associated
            }
            else if (token == "Normal") {
                format |= V_Normal;
            }
            else if (token == "Tangent") {
                format |= V_Tangent;
            }
            else if (token == "Binormal") {
                format |= V_Binormal;
            }
            else if (endsWith("TexCoords")) {
                uint8_t const n = static_cast<uint8_t>(std::stoi(token.substr(0, token.size() - 9)));
                SetNumTexCoords(format, n);
            }
            else if (endsWith("TexCoord")) {
                uint8_t const n = static_cast<uint8_t>(std::stoi(token.substr(0, token.size() - 8)));
                SetNumTexCoords(format, n);
            }
            else if (endsWith("Colors")) {
                uint8_t const n = static_cast<uint8_t>(std::stoi(token.substr(0, token.size() - 6)));
                SetNumColors(format, n);
            }
            else if (endsWith("Color")) {
                uint8_t const n = static_cast<uint8_t>(std::stoi(token.substr(0, token.size() - 5)));
                SetNumColors(format, n);
            }
            else if (endsWith("Bones")) {
                uint8_t const n = static_cast<uint8_t>(std::stoi(token.substr(0, token.size() - 5)));
                SetNumBones(format, n);
            }
            else if (endsWith("Bone")) {
                uint8_t const n = static_cast<uint8_t>(std::stoi(token.substr(0, token.size() - 4)));
                SetNumBones(format, n);
            }
            else {
                throw std::runtime_error("VertexFormatFromString: unrecognized token '" + token + "'");
            }
        }

        if (bar == std::string::npos) break;
        start = bar + 1;
    }

    return format;
}

template <typename BasicJsonType>
void VertexToJson(BasicJsonType &j, Vertex const &v, uint32_t format) {
    j = BasicJsonType::object();

    j["pos"] = v.pos; // position is always present

    if (format & V_Normal)   j["normal"] = v.normal;
    if (format & V_Tangent)  j["tangent"] = v.tangent;
    if (format & V_Binormal) j["binormal"] = v.binormal;

    if (uint8_t const numUV = static_cast<uint8_t>(NumTexCoords(format)); numUV > 0) {
        auto arr = BasicJsonType::array();
        for (uint8_t i = 0; i < numUV; ++i) arr.push_back(v.uv[i]);
        j["uv"] = std::move(arr);
    }

    if (uint8_t const numColors = static_cast<uint8_t>(NumColors(format)); numColors > 0) {
        auto arr = BasicJsonType::array();
        for (uint8_t i = 0; i < numColors; ++i) arr.push_back(v.colors[i]);
        j["colors"] = std::move(arr);
    }

    if (uint8_t const numBones = static_cast<uint8_t>(NumBones(format)); numBones > 0) {
        auto weights = BasicJsonType::array();
        auto indices = BasicJsonType::array();
        for (uint8_t i = 0; i < numBones; ++i) {
            weights.push_back(v.boneWeights[i]);
            indices.push_back(v.boneIndices[i]);
        }
        j["boneWeights"] = std::move(weights);
        j["boneIndices"] = std::move(indices);
    }
}

template <typename BasicJsonType>
void VertexFromJson(BasicJsonType const &j, Vertex &v, uint32_t format) {
    v = Vertex{};

    j.at("pos").get_to(v.pos);

    if (format & V_Normal)   j.at("normal").get_to(v.normal);
    if (format & V_Tangent)  j.at("tangent").get_to(v.tangent);
    if (format & V_Binormal) j.at("binormal").get_to(v.binormal);

    if (uint8_t const numUV = static_cast<uint8_t>(NumTexCoords(format)); numUV > 0) {
        BasicJsonType const &arr = j.at("uv");
        for (uint8_t i = 0; i < numUV && i < arr.size(); ++i) arr.at(i).get_to(v.uv[i]);
    }

    if (uint8_t const numColors = static_cast<uint8_t>(NumColors(format)); numColors > 0) {
        BasicJsonType const &arr = j.at("colors");
        for (uint8_t i = 0; i < numColors && i < arr.size(); ++i) arr.at(i).get_to(v.colors[i]);
    }

    if (uint8_t const numBones = static_cast<uint8_t>(NumBones(format)); numBones > 0) {
        BasicJsonType const &weights = j.at("boneWeights");
        BasicJsonType const &indices = j.at("boneIndices");
        for (uint8_t i = 0; i < numBones && i < weights.size(); ++i) weights.at(i).get_to(v.boneWeights[i]);
        for (uint8_t i = 0; i < numBones && i < indices.size(); ++i) indices.at(i).get_to(v.boneIndices[i]);
    }
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ShapeKeyVertex, vertexIndex, deltaPos, deltaNormal)

template <typename BasicJsonType>
void to_json(BasicJsonType &j, Mesh const &mesh) {
    j = BasicJsonType::object();
    if (!mesh.material.empty())   j["material"] = mesh.material;
    if (!mesh.polygons.empty())   j["polygons"] = mesh.polygons;
    if (!mesh.properties.empty()) j["properties"] = mesh.properties;
}
template <typename BasicJsonType>
void from_json(BasicJsonType const &j, Mesh &mesh) {
    mesh = Mesh{};
    if (j.contains("material"))   j.at("material").get_to(mesh.material);
    if (j.contains("polygons"))   j.at("polygons").get_to(mesh.polygons);
    if (j.contains("properties")) j.at("properties").get_to(mesh.properties);
}

template <typename BasicJsonType>
void to_json(BasicJsonType &j, Material const &mat) {
    j = BasicJsonType::object();
    if (!mat.name.empty())       j["name"] = mat.name;
    if (!mat.texture.empty())    j["texture"] = mat.texture;
    if (!mat.normalMap.empty())  j["normalMap"] = mat.normalMap;
    j["color"] = mat.color;
    if (!mat.properties.empty()) j["properties"] = mat.properties;
}
template <typename BasicJsonType>
void from_json(BasicJsonType const &j, Material &mat) {
    mat = Material{};
    if (j.contains("name"))       j.at("name").get_to(mat.name);
    if (j.contains("texture"))    j.at("texture").get_to(mat.texture);
    if (j.contains("normalMap"))  j.at("normalMap").get_to(mat.normalMap);
    if (j.contains("color"))      j.at("color").get_to(mat.color);
    if (j.contains("properties")) j.at("properties").get_to(mat.properties);
}

template <typename BasicJsonType>
void to_json(BasicJsonType &j, Texture const &tex) {
    j = BasicJsonType::object();
    if (!tex.name.empty())       j["name"] = tex.name;
    if (!tex.filename.empty())   j["filename"] = tex.filename;
    if (!tex.properties.empty()) j["properties"] = tex.properties;
}
template <typename BasicJsonType>
void from_json(BasicJsonType const &j, Texture &tex) {
    tex = Texture{};
    if (j.contains("name"))       j.at("name").get_to(tex.name);
    if (j.contains("filename"))   j.at("filename").get_to(tex.filename);
    if (j.contains("properties")) j.at("properties").get_to(tex.properties);
}

template <typename BasicJsonType>
void to_json(BasicJsonType &j, ShapeKey const &sk) {
    j = BasicJsonType::object();
    if (!sk.name.empty())     j["name"] = sk.name;
    if (!sk.vertices.empty()) j["vertices"] = sk.vertices;
}
template <typename BasicJsonType>
void from_json(BasicJsonType const &j, ShapeKey &sk) {
    sk = ShapeKey{};
    if (j.contains("name"))     j.at("name").get_to(sk.name);
    if (j.contains("vertices")) j.at("vertices").get_to(sk.vertices);
}

template <typename BasicJsonType>
void to_json(BasicJsonType &j, Object const &obj) {
    j = BasicJsonType::object();

    if (!obj.name.empty())   j["name"] = obj.name;
    if (!obj.parent.empty()) j["parent"] = obj.parent;
    if (!IsIdentityMatrix(obj.transform)) j["transform"] = obj.transform;

    if (obj.vertexFormat != 0)
        j["vertexFormat"] = VertexFormatToString(obj.vertexFormat);

    if (!obj.vertices.empty()) {
        auto vertsJson = BasicJsonType::array();
        for (Vertex const &v : obj.vertices) {
            BasicJsonType vj;
            VertexToJson(vj, v, obj.vertexFormat);
            vertsJson.push_back(std::move(vj));
        }
        j["vertices"] = std::move(vertsJson);
    }

    if (!obj.meshes.empty())    j["meshes"] = obj.meshes;
    if (!obj.shapeKeys.empty()) j["shapeKeys"] = obj.shapeKeys;

    if (uint8_t const numUV = static_cast<uint8_t>(NumTexCoords(obj.vertexFormat)); numUV > 0) {
        bool anyNamed = false;
        for (uint8_t i = 0; i < numUV; ++i)
            if (!obj.uvLayerNames[i].empty()) { anyNamed = true; break; }
        if (anyNamed) {
            auto arr = BasicJsonType::array();
            for (uint8_t i = 0; i < numUV; ++i) arr.push_back(obj.uvLayerNames[i]);
            j["uvLayerNames"] = std::move(arr);
        }
    }

    if (uint8_t const numColors = static_cast<uint8_t>(NumColors(obj.vertexFormat)); numColors > 0) {
        bool anyNamed = false;
        for (uint8_t i = 0; i < numColors; ++i)
            if (!obj.colorLayerNames[i].empty()) { anyNamed = true; break; }
        if (anyNamed) {
            auto arr = BasicJsonType::array();
            for (uint8_t i = 0; i < numColors; ++i) arr.push_back(obj.colorLayerNames[i]);
            j["colorLayerNames"] = std::move(arr);
        }
    }

    if (!obj.properties.empty()) j["properties"] = obj.properties;
}

template <typename BasicJsonType>
void from_json(BasicJsonType const &j, Object &obj) {
    obj = Object{};

    if (j.contains("name"))   j.at("name").get_to(obj.name);
    if (j.contains("parent")) j.at("parent").get_to(obj.parent);

    obj.transform = j.contains("transform")
        ? j.at("transform").template get<Matrix4x4>()
        : MakeIdentityMatrix();

    obj.vertexFormat = j.contains("vertexFormat")
        ? VertexFormatFromString(j.at("vertexFormat").template get<std::string>())
        : 0;

    if (j.contains("vertices")) {
        BasicJsonType const &vertsJson = j.at("vertices");
        obj.vertices.resize(vertsJson.size());
        for (size_t i = 0; i < vertsJson.size(); ++i)
            VertexFromJson(vertsJson.at(i), obj.vertices[i], obj.vertexFormat);
    }

    if (j.contains("meshes"))    j.at("meshes").get_to(obj.meshes);
    if (j.contains("shapeKeys")) j.at("shapeKeys").get_to(obj.shapeKeys);

    if (j.contains("uvLayerNames")) {
        BasicJsonType const &arr = j.at("uvLayerNames");
        for (size_t i = 0; i < arr.size() && i < obj.uvLayerNames.size(); ++i)
            arr.at(i).get_to(obj.uvLayerNames[i]);
    }
    if (j.contains("colorLayerNames")) {
        BasicJsonType const &arr = j.at("colorLayerNames");
        for (size_t i = 0; i < arr.size() && i < obj.colorLayerNames.size(); ++i)
            arr.at(i).get_to(obj.colorLayerNames[i]);
    }

    if (j.contains("properties")) j.at("properties").get_to(obj.properties);
}

template <typename BasicJsonType>
void to_json(BasicJsonType &j, Bone const &bone) {
    j = BasicJsonType::object();
    if (!bone.name.empty())   j["name"] = bone.name;
    if (!bone.parent.empty()) j["parent"] = bone.parent;
    if (!IsIdentityMatrix(bone.matrix)) j["matrix"] = bone.matrix;
    if (!bone.properties.empty()) j["properties"] = bone.properties;
}
template <typename BasicJsonType>
void from_json(BasicJsonType const &j, Bone &bone) {
    bone = Bone{};
    if (j.contains("name"))   j.at("name").get_to(bone.name);
    if (j.contains("parent")) j.at("parent").get_to(bone.parent);
    bone.matrix = j.contains("matrix")
        ? j.at("matrix").template get<Matrix4x4>()
        : MakeIdentityMatrix();
    if (j.contains("properties")) j.at("properties").get_to(bone.properties);
}

template <typename BasicJsonType>
void to_json(BasicJsonType &j, Skeleton const &skel) {
    j = BasicJsonType::object();
    if (!skel.bones.empty())      j["bones"] = skel.bones;
    if (!skel.properties.empty()) j["properties"] = skel.properties;
}
template <typename BasicJsonType>
void from_json(BasicJsonType const &j, Skeleton &skel) {
    skel = Skeleton{};
    if (j.contains("bones"))      j.at("bones").get_to(skel.bones);
    if (j.contains("properties")) j.at("properties").get_to(skel.properties);
}

inline bool IsEmptySkeleton(Skeleton const &skel) {
    return skel.bones.empty() && skel.properties.empty();
}

template <typename BasicJsonType>
void to_json(BasicJsonType &j, Model const &model) {
    j = BasicJsonType::object();
    j["version"] = MODEL_LAYOUT_VERSION;
    if (!model.name.empty())      j["name"] = model.name;
    if (!model.objects.empty())   j["objects"] = model.objects;
    if (!model.materials.empty()) j["materials"] = model.materials;
    if (!model.textures.empty())  j["textures"] = model.textures;
    if (!IsEmptySkeleton(model.skeleton)) j["skeleton"] = model.skeleton;
    if (!model.properties.empty()) j["properties"] = model.properties;
}
template <typename BasicJsonType>
void from_json(BasicJsonType const &j, Model &model) {
    model = Model{};
    if (j.contains("name"))       j.at("name").get_to(model.name);
    if (j.contains("objects"))    j.at("objects").get_to(model.objects);
    if (j.contains("materials"))  j.at("materials").get_to(model.materials);
    if (j.contains("textures"))   j.at("textures").get_to(model.textures);
    if (j.contains("skeleton"))   j.at("skeleton").get_to(model.skeleton);
    if (j.contains("properties")) j.at("properties").get_to(model.properties);
}

void WriteModelJson(Model const &model, std::filesystem::path const &filename, int indent = 2) {
    nlohmann::ordered_json j = model;
    std::ofstream out(filename);
    if (!out) throw std::runtime_error("WriteModelJson: could not open " + filename.string());
    out << j.dump(indent);
}

Model ReadModelJson(std::filesystem::path const &filename) {
    std::ifstream in(filename);
    if (!in) throw std::runtime_error("ReadModelJson: could not open " + filename.string());
    nlohmann::ordered_json j;
    in >> j;
    return j.get<Model>();
}
