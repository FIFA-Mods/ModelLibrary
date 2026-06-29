#pragma once
#include "ModelTypes.h"

using PropertyValue = std::variant<int, float, double, std::string, Vector2, Vector3, Vector4>;

enum VertexFormat {
    V_Position = 0,
    V_Normal = 1,
    V_Tangent = 2,
    V_Binormal = 4,
    V_1TexCoord = 8 * 1,
    V_2TexCoords = 8 * 2,
    V_3TexCoords = 8 * 3,
    V_4TexCoords = 8 * 4,
    V_5TexCoords = 8 * 5,
    V_6TexCoords = 8 * 6,
    V_7TexCoords = 8 * 7,
    V_8TexCoords = 8 * 8,
    V_1Color = 128 * 1,
    V_2Colors = 128 * 2,
    V_3Colors = 128 * 3,
    V_4Colors = 128 * 4,
    V_5Colors = 128 * 5,
    V_6Colors = 128 * 6,
    V_7Colors = 128 * 7,
    V_8Colors = 128 * 8,
    V_1Bone = 2048 * 1,
    V_2Bones = 2048 * 2,
    V_3Bones = 2048 * 3,
    V_4Bones = 2048 * 4,
    V_5Bones = 2048 * 5,
    V_6Bones = 2048 * 6,
    V_7Bones = 2048 * 7,
    V_8Bones = 2048 * 8
};

#define NumTexCoords(format) (((format) >> 3) & 0xF)
#define NumColors(format) (((format) >> 7) & 0xF)
#define NumBones(format) (((format) >> 11) & 0xF)

void SetNumTexCoords(uint32_t &format, uint8_t n);
void SetNumColors(uint32_t &format, uint8_t n);
void SetNumBones(uint32_t &format, uint8_t n);

struct Vertex {
    Vector3 pos, normal, tangent, binormal;
    Vector2 uv[8];
    RGBA colors[8];
    float boneWeights[8];
    uint16_t boneIndices[8];
};

struct Mesh {
    std::string material;
    std::vector<std::array<uint32_t, 3>> triangles;
    std::map<std::string, PropertyValue> properties;
};

struct Material {
    std::string name, texture, normalMap;
    RGBA color;
    std::map<std::string, PropertyValue> properties;

    Material();
    Material(std::string const &n);
    Material(std::string const &n, std::string const &tex);
    Material(std::string const &n, std::string const &tex, RGBA const &col);
};

struct Texture {
    std::string name;
    std::string filename;
    std::map<std::string, PropertyValue> properties;
};

struct Object {
    std::string name, parent;
    Matrix4x4 transform;
    uint32_t vertexFormat;
    std::vector<Vertex> vertices;
    std::vector<Mesh> meshes;
    std::array<std::string, 8> uvLayerNames;
    std::array<std::string, 8> colorLayerNames;
    std::map<std::string, PropertyValue> properties;

    Mesh &firstMesh();
};

struct Bone {
    std::string name, parent;
    Matrix4x4 matrix;
    std::map<std::string, PropertyValue> properties;
};

struct Skeleton {
    std::vector<Bone> bones;
    std::map<std::string, PropertyValue> properties;
};

struct ModelOptions {
    size_t VertexLimit = 0;
    float ScaleFactor = 1.0f;
    size_t BonesPerVertex = 0;
    bool PreTransformVertices = false;
    bool GenerateNormals = false;
    bool GenerateTangents = false;
    bool FlipWindingOrder = false;
    bool FlipUVs = false;
};

struct Model {
    std::string name;
    std::vector<Object> objects;
    std::vector<Material> materials;
    std::vector<Texture> textures;
    Skeleton skeleton;
    std::map<std::string, PropertyValue> properties;

    Model();
    Model(std::filesystem::path const &fbxFileName, ModelOptions const &options = ModelOptions());
    std::string GenerateObjectName() const;
    void Clear();
    void DumpSkeletonHierarchy(const Skeleton &skeleton);
    void ReadFbx(std::filesystem::path const &filename, ModelOptions const &options = ModelOptions());
    void WriteFbx(std::filesystem::path const &filename, bool ascii = false);
    void ReadObj(std::filesystem::path const &filename);
    void WriteObj(std::filesystem::path const &filename);

    Model &operator+=(Model const &other);
};

Model operator+(Model lhs, Model const &rhs);
