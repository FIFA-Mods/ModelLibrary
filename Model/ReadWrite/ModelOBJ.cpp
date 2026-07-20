#include "Model.h"
#include <fstream>
#include "ModelFbxSdkHeader.h"
#include "ModelTypeConversion.h"
#include "MeshOperations/MeshTriangulation.h"

namespace obj_helper::obj_utils {

void TrimInPlace(std::string &s) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
}

float ParseFloat(const std::string &s, size_t &pos) {
    while (pos < s.size() && std::isspace((unsigned char)s[pos])) ++pos;
    float v = 0.0f;
    auto result = std::from_chars(s.data() + pos, s.data() + s.size(), v);
    pos = result.ptr - s.data();
    return v;
}

int ParseInt(const char *s) {
    int v = 0;
    std::from_chars(s, s + std::strlen(s), v);
    return v;
}

int ResolveIndex(int raw, int total) {
    if (raw == 0) return -1;
    return (raw > 0) ? (raw - 1) : (total + raw);
}

struct ObjPVKey {
    int p = -1, t = -1, n = -1;
    bool operator==(const ObjPVKey &o) const noexcept {
        return p == o.p && t == o.t && n == o.n;
    }
};
struct ObjPVKeyHash {
    size_t operator()(const ObjPVKey &k) const noexcept {
        size_t h = std::hash<int>()(k.p);
        h ^= std::hash<int>()(k.t) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(k.n) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};

ObjPVKey SplitFaceToken(const std::string &tok,
    int totalPos, int totalUV, int totalNorm) {
    ObjPVKey k;
    const char *s = tok.c_str();
    const char *slash1 = std::strchr(s, '/');
    if (!slash1) {
        k.p = ResolveIndex(ParseInt(s), totalPos);
        return k;
    }
    // pos
    std::string posStr(s, slash1 - s);
    k.p = ResolveIndex(ParseInt(posStr.c_str()), totalPos);
    const char *slash2 = std::strchr(slash1 + 1, '/');
    if (!slash2) {
        // v/t
        k.t = ResolveIndex(ParseInt(slash1 + 1), totalUV);
    }
    else {
        // v/t/n  or  v//n
        if (slash2 != slash1 + 1) {
            std::string uvStr(slash1 + 1, slash2 - slash1 - 1);
            k.t = ResolveIndex(ParseInt(uvStr.c_str()), totalUV);
        }
        k.n = ResolveIndex(ParseInt(slash2 + 1), totalNorm);
    }
    return k;
}

std::string PathStem(const std::filesystem::path &p) {
    return p.stem().string();
}

std::filesystem::path WithExt(const std::filesystem::path &p, const char *ext) {
    return p.parent_path() / (p.stem().string() + ext);
}

Vector3 TransformPoint(const Matrix4x4 &m, const Vector3 &v) {
    FbxVector4 result = ToFbx(m).MultT(FbxVector4(v[0], v[1], v[2], 1.0));
    return { (float)result[0], (float)result[1], (float)result[2] };
}

Vector3 TransformNormal(const Matrix4x4 &m, const Vector3 &n) {
    FbxVector4 result = ToFbx(m).Inverse().Transpose().MultT(FbxVector4(n[0], n[1], n[2], 0.0));
    return { (float)result[0], (float)result[1], (float)result[2] };
}

}

void Model::ReadObj(std::filesystem::path const &filename, ModelOptions const &options) {
    using namespace obj_helper::obj_utils;
    Clear();
    std::ifstream file(filename);
    if (!file.is_open())
        throw std::runtime_error("ReadObj: cannot open file: " + filename.string());
    name = PathStem(filename);
    struct MtlDef {
        std::string texture;    // map_Kd
        std::string normalMap;  // map_Bump / map_Kn / bump
        RGBA color{ 255,255,255,255 };
    };
    std::map<std::string, MtlDef> mtlDefs;
    auto LoadMtl = [&](const std::filesystem::path &mtlPath) {
        std::ifstream mf(mtlPath);
        if (!mf.is_open()) return;
        std::string curMat;
        std::string line;
        while (std::getline(mf, line)) {
            TrimInPlace(line);
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line);
            std::string token;
            ss >> token;
            if (token == "newmtl") {
                std::string matName;
                std::getline(ss, matName);
                TrimInPlace(matName);
                curMat = matName;
                mtlDefs.emplace(curMat, MtlDef{});
            }
            else if (!curMat.empty()) {
                auto &def = mtlDefs[curMat];
                if (token == "map_Kd") {
                    std::string texFile;
                    std::getline(ss, texFile);
                    TrimInPlace(texFile);
                    def.texture = texFile;
                }
                else if (token == "map_Bump" || token == "map_bump" ||
                    token == "bump" || token == "map_Kn") {
                    std::string texFile;
                    std::getline(ss, texFile);
                    TrimInPlace(texFile);
                    def.normalMap = texFile;
                }
                else if (token == "Kd") {
                    size_t pos = (size_t)(line.find(' '));
                    float r = ParseFloat(line, pos);
                    float g = ParseFloat(line, pos);
                    float b = ParseFloat(line, pos);
                    def.color.r = (uint8_t)std::clamp((int)(r * 255.0f), 0, 255);
                    def.color.g = (uint8_t)std::clamp((int)(g * 255.0f), 0, 255);
                    def.color.b = (uint8_t)std::clamp((int)(b * 255.0f), 0, 255);
                }
                else if (token == "d" || token == "Tr") {
                    size_t pos = (size_t)(line.find(' '));
                    float alpha = ParseFloat(line, pos);
                    if (token == "Tr") alpha = 1.0f - alpha; // Tr is inverse
                    def.color.a = (uint8_t)std::clamp((int)(alpha * 255.0f), 0, 255);
                }
            }
        }
    };
    {
        std::string line;
        while (std::getline(file, line)) {
            TrimInPlace(line);
            if (line.rfind("mtllib", 0) == 0) {
                std::istringstream ss(line);
                std::string token, mtlFile;
                ss >> token;
                std::getline(ss, mtlFile);
                TrimInPlace(mtlFile);
                auto mtlPath = filename.parent_path() / mtlFile;
                LoadMtl(mtlPath);
            }
        }
        file.clear();
        file.seekg(0);
    }
    std::map<std::string, std::string> texFileToName;
    auto GetOrAddTexture = [&](const std::string &texFile) -> std::string {
        if (texFile.empty()) return {};
        auto it = texFileToName.find(texFile);
        if (it != texFileToName.end()) return it->second;
        Texture t;
        t.filename = texFile;
        t.name = std::filesystem::path(texFile).stem().string();
        std::string base = t.name;
        int suffix = 1;
        auto nameUsed = [&](const std::string &n) {
            for (auto &tx : textures) if (tx.name == n) return true;
            return false;
        };
        while (nameUsed(t.name))
            t.name = base + "." + std::to_string(suffix++);
        textures.push_back(t);
        texFileToName[texFile] = t.name;
        return t.name;
    };
    std::map<std::string, std::string> mtlNameToModelMatName;
    for (auto &[mtlName, def] : mtlDefs) {
        Material mat;
        mat.name = mtlName;
        {
            std::string base = mat.name;
            int suffix = 1;
            auto nameUsed = [&](const std::string &n) {
                for (auto &m : materials) if (m.name == n) return true;
                return false;
            };
            while (nameUsed(mat.name))
                mat.name = base + "." + std::to_string(suffix++);
        }
        mat.color = def.color;
        mat.texture = GetOrAddTexture(def.texture);
        mat.normalMap = GetOrAddTexture(def.normalMap);
        materials.push_back(mat);
        mtlNameToModelMatName[mtlName] = mat.name;
    }
    std::vector<Vector3> positions;
    std::vector<Vector2> uvs;
    std::vector<Vector3> normals;
    std::set<std::string> usedObjNames;
    auto MakeUniqueObjName = [&](const std::string &base) -> std::string {
        std::string n = base.empty() ? "object" : base;
        std::string unique = n;
        int i = 1;
        while (usedObjNames.count(unique))
            unique = n + "." + std::to_string(i++);
        usedObjNames.insert(unique);
        return unique;
    };
    struct FaceGroup {
        std::string material;
        std::vector<std::vector<uint32_t>> polygons;
    };
    struct ObjGroup {
        std::string name;
        std::unordered_map<ObjPVKey, uint32_t, ObjPVKeyHash> keyToIndex;
        std::vector<Vertex> vertices;
        std::vector<FaceGroup> groups;
        uint32_t vertexFormat = V_Position;
    };
    std::vector<ObjGroup> objGroups;
    ObjGroup *cur = nullptr;
    std::string curMaterial;
    auto FlushAndNewGroup = [&](const std::string &groupName) {
        objGroups.emplace_back();
        cur = &objGroups.back();
        cur->name = groupName;
        cur->vertexFormat = V_Position;
        curMaterial.clear();
    };
    FlushAndNewGroup(PathStem(filename));
    std::string line;
    while (std::getline(file, line)) {
        TrimInPlace(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string token;
        ss >> token;
        if (token == "v") {
            // Position
            size_t pos = 1;
            float x = ParseFloat(line, pos);
            float y = ParseFloat(line, pos);
            float z = ParseFloat(line, pos);
            positions.push_back({ x, y, z });
        }
        else if (token == "vt") {
            size_t pos = 2;
            float u = ParseFloat(line, pos);
            float v = ParseFloat(line, pos);
            uvs.push_back({ u, v });
        }
        else if (token == "vn") {
            size_t pos = 2;
            float x = ParseFloat(line, pos);
            float y = ParseFloat(line, pos);
            float z = ParseFloat(line, pos);
            normals.push_back({ x, y, z });
        }
        else if (token == "o") {
            std::string groupName;
            std::getline(ss, groupName);
            TrimInPlace(groupName);
            bool hasFaces = cur &&
                std::any_of(cur->groups.begin(), cur->groups.end(),
                    [](const FaceGroup &fg) { return !fg.polygons.empty(); });
            if (hasFaces) {
                FlushAndNewGroup(groupName);
            }
            else {
                cur->name = groupName;
            }
        }
        else if (token == "usemtl") {
            std::string matName;
            std::getline(ss, matName);
            TrimInPlace(matName);
            auto it = mtlNameToModelMatName.find(matName);
            curMaterial = (it != mtlNameToModelMatName.end()) ? it->second : matName;
        }
        else if (token == "f") {
            FaceGroup *fg = nullptr;
            for (auto &g : cur->groups) {
                if (g.material == curMaterial) { fg = &g; break; }
            }
            if (!fg) {
                cur->groups.emplace_back();
                fg = &cur->groups.back();
                fg->material = curMaterial;
            }
            std::vector<std::string> fvTokens;
            {
                std::string tok;
                while (ss >> tok) fvTokens.push_back(tok);
            }
            if (fvTokens.size() < 3) continue;
            int totP = (int)positions.size();
            int totT = (int)uvs.size();
            int totN = (int)normals.size();
            auto ResolveCorner = [&](const std::string &tok) -> uint32_t {
                ObjPVKey key = SplitFaceToken(tok, totP, totT, totN);
                auto it = cur->keyToIndex.find(key);
                if (it != cur->keyToIndex.end()) return it->second;
                uint32_t newIdx = (uint32_t)cur->vertices.size();
                cur->keyToIndex.emplace(key, newIdx);
                Vertex &V = cur->vertices.emplace_back();
                if (key.p >= 0 && key.p < (int)positions.size())
                    V.pos = positions[key.p];
                if (key.t >= 0 && key.t < (int)uvs.size()) {
                    V.uv[0] = uvs[key.t];
                    cur->vertexFormat |= V_1TexCoord;
                }
                if (key.n >= 0 && key.n < (int)normals.size()) {
                    V.normal = normals[key.n];
                    cur->vertexFormat |= V_Normal;
                }
                return newIdx;
            };
            auto &newPoly = fg->polygons.emplace_back();
            newPoly.resize(fvTokens.size());
            for (size_t vi = 0; vi < fvTokens.size(); ++vi)
                newPoly[vi] = ResolveCorner(fvTokens[vi]);
        }
    }
    for (auto &og : objGroups) {
        bool anyFaces = std::any_of(og.groups.begin(), og.groups.end(),
            [](const FaceGroup &fg) { return !fg.polygons.empty(); });
        if (og.vertices.empty() && !anyFaces) continue;
        Object obj;
        obj.name = MakeUniqueObjName(og.name);
        obj.parent.clear();
        obj.transform = Matrix4x4(); // identity
        obj.vertexFormat = og.vertexFormat;
        obj.vertices = std::move(og.vertices);
        for (auto &fg : og.groups) {
            if (fg.polygons.empty()) continue;
            Mesh m;
            m.material = fg.material;
            m.polygons = std::move(fg.polygons);
            obj.meshes.push_back(std::move(m));
        }
        objects.push_back(std::move(obj));
    }
    if (options.AlwaysTriangulate)
        Triangulate();
    ModelPostLoadProcess(*this, options);
}

void WriteOBJGeneratorVersion(std::ofstream &f, std::string const &userWriter) {
    if (!userWriter.empty())
        f << "# Generated by " << userWriter << "\n";
    f << "# Model Library version " MODEL_LIBRARY_VERSION "\n";
}

void Model::WriteObj(std::filesystem::path const &filename, ModelOptions const &options) {
    using namespace obj_helper::obj_utils;
    std::filesystem::path mtlPath;
    std::string mtlName;
    bool writeMaterials = !materials.empty();
    if (writeMaterials) {
        mtlPath = WithExt(filename, ".mtl");
        mtlName = mtlPath.filename().string();
        std::ofstream mf(mtlPath);
        if (!mf.is_open())
            throw std::runtime_error("WriteObj: cannot open MTL file: " + mtlPath.string());
        auto FindTexFile = [&](const std::string &texName) -> std::string {
            for (auto &t : textures)
                if (t.name == texName) return t.filename;
            return texName;
        };
        WriteOBJGeneratorVersion(mf, options.Writer);
        for (auto &mat : materials) {
            mf << "newmtl " << mat.name << "\n";
            float r = mat.color.r / 255.0f;
            float g = mat.color.g / 255.0f;
            float b = mat.color.b / 255.0f;
            float a = mat.color.a / 255.0f;
            mf << "Kd " << r << " " << g << " " << b << "\n";
            mf << "Ka 0 0 0\n";
            mf << "Ks 0 0 0\n";
            if (mat.color.a != 255)
                mf << "d " << a << "\n";
            if (!mat.texture.empty())
                mf << "map_Kd " << FindTexFile(mat.texture) << "\n";
            if (!mat.normalMap.empty())
                mf << "map_Bump " << FindTexFile(mat.normalMap) << "\n";
            mf << "\n";
        }
    }
    std::ofstream of(filename);
    if (!of.is_open())
        throw std::runtime_error("WriteObj: cannot open OBJ file: " + filename.string());
    WriteOBJGeneratorVersion(of, options.Writer);
    if (writeMaterials)
        of << "mtllib " << mtlName << "\n";
    of << "\n";
    uint32_t posBase = 1;
    uint32_t uvBase = 1;
    uint32_t normBase = 1;
    for (auto &obj : objects) {
        if (obj.meshes.empty()) continue;
        bool hasUV = (NumTexCoords(obj.vertexFormat) > 0);
        bool hasNormal = (obj.vertexFormat & V_Normal) != 0;
        of << "o " << obj.name << "\n";
        for (auto &v : obj.vertices) {
            Vector3 wp = TransformPoint(obj.transform, v.pos);
            of << "v " << wp.x << " " << wp.y << " " << wp.z << "\n";
        }
        if (hasUV) {
            for (auto &v : obj.vertices)
                of << "vt " << v.uv[0].x << " " << v.uv[0].y << "\n";
        }
        if (hasNormal) {
            for (auto &v : obj.vertices) {
                Vector3 wn = TransformNormal(obj.transform, v.normal);
                of << "vn " << wn.x << " " << wn.y << " " << wn.z << "\n";
            }
        }
        for (auto &mesh : obj.meshes) {
            if (mesh.polygons.empty()) continue;
            std::vector<std::vector<uint32_t>> triangulatedStorage;
            const std::vector<std::vector<uint32_t>> *polysToWrite = &mesh.polygons;
            if (options.AlwaysTriangulate && !mesh.IsTriangulated()) {
                triangulatedStorage = MeshTriangulation::Triangulate(mesh.polygons, obj.vertices);
                polysToWrite = &triangulatedStorage;
            }
            if (writeMaterials) {
                if (!mesh.material.empty())
                    of << "usemtl " << mesh.material << "\n";
                else
                    of << "usemtl (none)\n";
            }
            auto WriteCorner = [&](uint32_t vi) {
                uint32_t pi = posBase + vi;
                if (hasUV && hasNormal) {
                    uint32_t ti = uvBase + vi;
                    uint32_t ni = normBase + vi;
                    of << " " << pi << "/" << ti << "/" << ni;
                }
                else if (hasUV) {
                    uint32_t ti = uvBase + vi;
                    of << " " << pi << "/" << ti;
                }
                else if (hasNormal) {
                    uint32_t ni = normBase + vi;
                    of << " " << pi << "//" << ni;
                }
                else
                    of << " " << pi;
            };
            for (auto &poly : *polysToWrite) {
                of << "f";
                for (size_t c = 0; c < poly.size(); ++c)
                    WriteCorner(poly[c]);
                of << "\n";
            }
        }
        of << "\n";
        uint32_t nv = (uint32_t)obj.vertices.size();
        posBase += nv;
        if (hasUV)  uvBase += nv;
        if (hasNormal) normBase += nv;
    }
}
