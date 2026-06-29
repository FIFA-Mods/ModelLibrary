#include "Model.h"
#include <iostream>
#include <fstream>
#include "ModelFbxHeader.h"
#include "ModelTypeConversion.h"

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
    ReadFbx(fbxFileName, options);
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

void Model::ReadFbx(std::filesystem::path const &filename, ModelOptions const &options) {
    FbxManager *sdkManager = FbxManager::Create();
    if (!sdkManager)
        throw std::runtime_error("unable to create sdk manager");
    FbxIOSettings *ios = FbxIOSettings::Create(sdkManager, IOSROOT);
    sdkManager->SetIOSettings(ios);
    FbxImporter *importer = FbxImporter::Create(sdkManager, "");
    bool importOk = importer->Initialize(filename.string().c_str(), -1, sdkManager->GetIOSettings());
    if (!importOk) {
        importer->Destroy();
        ios->Destroy();
        sdkManager->Destroy();
        throw std::runtime_error("unable to initialize importer");
    }
    FbxScene *scene = FbxScene::Create(sdkManager, "scene");
    if (!scene) {
        importer->Destroy();
        ios->Destroy();
        sdkManager->Destroy();
        throw std::runtime_error("unable to crfeate scene");
    }
    if (!importer->Import(scene)) {
        importer->Destroy();
        ios->Destroy();
        sdkManager->Destroy();
        throw std::runtime_error("unable to import scene");
    }
    importer->Destroy();
    FbxGeometryConverter geomConv(sdkManager);
    geomConv.Triangulate(scene, true);
    if (!scene) {
        ios->Destroy();
        sdkManager->Destroy();
        throw std::runtime_error("unable to load complete scene");
    }
    FbxNode *root = scene->GetRootNode();
    if (!root) {
        ios->Destroy();
        sdkManager->Destroy();
        throw std::runtime_error("unable to find scene root node");
    }
    name = filename.stem().string();
    std::map<std::string, size_t> texKeyToIndex;
    auto MakeUniqueTexName = [&](const std::string &base) {
        static std::set<std::string> usedTexNames;
        std::string n = base;
        if (n.empty()) n = "texture";
        std::string unique = n;
        size_t i = 1;
        while (usedTexNames.count(unique)) {
            unique = n + "." + std::to_string(i++);
        }
        usedTexNames.insert(unique);
        return unique;
    };
    auto AddTextureEntry = [&](const std::string &key, const std::string &filenameStr) -> size_t {
        auto it = texKeyToIndex.find(key);
        if (it != texKeyToIndex.end()) return it->second;
        Texture t;
        t.name = MakeUniqueTexName(std::filesystem::path(filenameStr).stem().string());
        t.filename = filenameStr;
        size_t idx = textures.size();
        textures.push_back(t);
        texKeyToIndex[key] = idx;
        return idx;
    };
    // Materials & textures
    std::map<FbxSurfaceMaterial *, std::string> materialToName;
    for (int mi = 0; mi < scene->GetMaterialCount(); ++mi) {
        FbxSurfaceMaterial *fmat = scene->GetMaterial(mi);
        Material mat;
        std::string matName = fmat->GetName() ? fmat->GetName() : ("mat_" + std::to_string(mi));
        mat.name = matName;
        FbxProperty diffuseProp = fmat->FindProperty(FbxSurfaceMaterial::sDiffuse);
        if (diffuseProp.IsValid()) {
            FbxDouble3 col = diffuseProp.Get<FbxDouble3>();
            mat.color.r = (uint8_t)std::clamp<int>(int(col[0] * 255.0), 0, 255);
            mat.color.g = (uint8_t)std::clamp<int>(int(col[1] * 255.0), 0, 255);
            mat.color.b = (uint8_t)std::clamp<int>(int(col[2] * 255.0), 0, 255);
            mat.color.a = 255;
        }
        auto TryGetTextureFromProperty = [&](FbxProperty &prop) -> std::string {
            if (!prop.IsValid()) return std::string();
            int layeredTexCount = prop.GetSrcObjectCount<FbxLayeredTexture>();
            if (layeredTexCount > 0) {
                FbxLayeredTexture *lTex = prop.GetSrcObject<FbxLayeredTexture>(0);
                if (lTex && lTex->GetSrcObjectCount<FbxFileTexture>() > 0) {
                    FbxFileTexture *ft = lTex->GetSrcObject<FbxFileTexture>(0);
                    if (ft) {
                        std::string fname = ft->GetFileName() ? ft->GetFileName() : ft->GetName();
                        size_t tidx = AddTextureEntry(fname.empty() ? ft->GetName() : fname, fname);
                        return textures[tidx].name;
                    }
                }
            }
            else {
                int fileTexCount = prop.GetSrcObjectCount<FbxFileTexture>();
                if (fileTexCount > 0) {
                    FbxFileTexture *ft = prop.GetSrcObject<FbxFileTexture>(0);
                    if (ft) {
                        std::string fname = ft->GetFileName() ? ft->GetFileName() : ft->GetName();
                        if (fname.empty()) {
                            std::string emb = std::string("embedded_") + (ft->GetName() ? ft->GetName() : "tex");
                            size_t tidx = AddTextureEntry(emb, emb);
                            return textures[tidx].name;
                        }
                        else {
                            size_t tidx = AddTextureEntry(fname, fname);
                            return textures[tidx].name;
                        }
                    }
                }
            }
            return std::string();
        };
        FbxProperty dprop = fmat->FindProperty(FbxSurfaceMaterial::sDiffuse);
        mat.texture = TryGetTextureFromProperty(dprop);
        FbxProperty nprop = fmat->FindProperty("NormalMap");
        if (!nprop.IsValid()) nprop = fmat->FindProperty(FbxSurfaceMaterial::sBump);
        mat.normalMap = TryGetTextureFromProperty(nprop);
        materials.push_back(mat);
        materialToName[fmat] = mat.name;
    }
    // Collect full skeleton (clusters + explicit skeleton nodes + controlled ancestors)
    std::set<FbxNode *> clusterLinks;
    std::set<FbxNode *> explicitSkeletonNodes;
    for (int geomIndex = 0; geomIndex < scene->GetGeometryCount(); ++geomIndex) {
        FbxGeometry *geom = scene->GetGeometry(geomIndex);
        FbxMesh *mesh = FbxCast<FbxMesh>(geom);
        if (!mesh)
            continue;
        for (int d = 0; d < mesh->GetDeformerCount(FbxDeformer::eSkin); ++d) {
            FbxSkin *skin = static_cast<FbxSkin *>(mesh->GetDeformer(d, FbxDeformer::eSkin));
            if (!skin)
                continue;
            for (int c = 0; c < skin->GetClusterCount(); ++c) {
                FbxCluster *cluster = skin->GetCluster(c);
                if (!cluster)
                    continue;
                FbxNode *link = cluster->GetLink();
                if (link)
                    clusterLinks.insert(link);
            }
        }
    }
    std::function<void(FbxNode *)> findSkeletonAttrs;
    findSkeletonAttrs = [&](FbxNode *node) {
        if (!node)
            return;
        FbxNodeAttribute *attr = node->GetNodeAttribute();
        if (attr && attr->GetAttributeType() == FbxNodeAttribute::eSkeleton)
            explicitSkeletonNodes.insert(node);
        for (int i = 0; i < node->GetChildCount(); ++i)
            findSkeletonAttrs(node->GetChild(i));
    };
    findSkeletonAttrs(root);
    auto isSkeletonNode = [&](FbxNode *n)->bool {
        if (!n) return false;
        FbxNodeAttribute *a = n->GetNodeAttribute();
        return (a && a->GetAttributeType() == FbxNodeAttribute::eSkeleton);
    };
    auto hasSingleChild = [&](FbxNode *n)->bool {
        if (!n) return false;
        return n->GetChildCount() == 1;
    };
    std::set<FbxNode *> bonesToInclude;
    std::vector<FbxNode *> skeletonRoots;
    if (!explicitSkeletonNodes.empty()) {
        for (FbxNode *n : explicitSkeletonNodes) {
            FbxNode *p = n->GetParent();
            if (!p || explicitSkeletonNodes.find(p) == explicitSkeletonNodes.end()) {
                skeletonRoots.push_back(n);
            }
        }
        std::function<void(FbxNode *)> addDescendants = [&](FbxNode *n) {
            if (!n) return;
            if (bonesToInclude.insert(n).second == false) return;
            for (int i = 0; i < n->GetChildCount(); ++i) addDescendants(n->GetChild(i));
        };
        for (FbxNode *rootJoint : skeletonRoots) addDescendants(rootJoint);
    }
    else if (!clusterLinks.empty()) {
        bonesToInclude.insert(clusterLinks.begin(), clusterLinks.end());
        for (FbxNode *ln : clusterLinks) {
            FbxNode *cur = ln->GetParent();
            while (cur) {
                if (bonesToInclude.count(cur))
                    break;
                if (isSkeletonNode(cur)) {
                    bonesToInclude.insert(cur);
                    cur = cur->GetParent();
                    continue;
                }
                if (cur->GetChildCount() > 1) {
                    bonesToInclude.insert(cur);
                    cur = cur->GetParent();
                    continue;
                }
                break;
            }
        }
        for (FbxNode *n : bonesToInclude) {
            FbxNode *p = n->GetParent();
            if (!p || bonesToInclude.find(p) == bonesToInclude.end()) skeletonRoots.push_back(n);
        }
    }
    skeleton.bones.clear();
    std::map<FbxNode *, std::string> nodeToBoneName;
    std::map<FbxNode *, uint16_t> nodeToIndex;
    std::set<std::string> usedBoneNames;
    auto MakeUniqueBoneName = [&](const std::string &base) {
        std::string nm = base.empty() ? "bone" : base;
        std::string unique = nm;
        int i = 1;
        while (usedBoneNames.count(unique)) unique = nm + "." + std::to_string(i++);
        usedBoneNames.insert(unique);
        return unique;
    };
    std::function<void(FbxNode *)> emitPreorder = [&](FbxNode *n) {
        if (!n) return;
        if (bonesToInclude.find(n) == bonesToInclude.end()) return;
        Bone b;
        std::string bname = n->GetName() ? n->GetName() : std::string();
        b.name = MakeUniqueBoneName(bname);
        b.matrix = FromFbx(n->EvaluateLocalTransform());
        uint16_t thisIndex = static_cast<uint16_t>(skeleton.bones.size());
        skeleton.bones.push_back(b);
        nodeToBoneName[n] = skeleton.bones[thisIndex].name;
        nodeToIndex[n] = thisIndex;
        for (int i = 0; i < n->GetChildCount(); ++i)
            emitPreorder(n->GetChild(i));
    };
    if (!skeletonRoots.empty()) {
        for (FbxNode *rootNode : skeletonRoots) emitPreorder(rootNode);
    }
    else {
        std::vector<FbxNode *> fallbackRoots;
        for (FbxNode *n : bonesToInclude) {
            FbxNode *p = n->GetParent();
            if (!p || bonesToInclude.find(p) == bonesToInclude.end()) fallbackRoots.push_back(n);
        }
        for (FbxNode *r : fallbackRoots) emitPreorder(r);
    }
    for (auto &pair : nodeToBoneName) {
        FbxNode *n = pair.first;
        uint16_t idx_ = nodeToIndex[n];
        if (n->GetParent() && nodeToBoneName.count(n->GetParent())) {
            skeleton.bones[idx_].parent = nodeToBoneName[n->GetParent()];
        }
        else {
            skeleton.bones[idx_].parent = "";
        }
    }
    std::map<std::string, uint16_t> globalBoneIndex;
    for (uint16_t i = 0; i < skeleton.bones.size(); ++i)
        globalBoneIndex[skeleton.bones[i].name] = i;
    // Node traversal and object creation
    std::set<std::string> usedNodeNames;
    auto MakeUniqueObjectName = [&](std::string const &objName) {
        std::string uniqueName = objName;
        if (objName.empty()) {
            size_t i = 1;
            while (usedNodeNames.count("noname_" + std::to_string(i))) ++i;
            uniqueName = "noname_" + std::to_string(i);
        }
        else if (usedNodeNames.count(objName)) {
            size_t i = 1;
            do {
                uniqueName = objName + "." + (i < 10 ? "00" : (i < 100 ? "0" : "")) + std::to_string(i);
                ++i;
            } while (usedNodeNames.count(uniqueName));
        }
        usedNodeNames.insert(uniqueName);
        return uniqueName;
    };
    auto CreateObject = [&](std::string const &objName, std::string const &parentName, const FbxAMatrix &transform) -> Object & {
        Object &obj = objects.emplace_back();
        obj.name = MakeUniqueObjectName(objName);
        obj.parent = parentName;
        obj.transform = FromFbx(transform);
        return obj;
    };
    auto GetMaterialNameForMesh = [&](FbxMesh *fbxMesh, int materialIndex) -> std::string {
        FbxNode *owningNode = fbxMesh->GetNode();
        if (!owningNode) return std::string();
        if (materialIndex < owningNode->GetMaterialCount()) {
            FbxSurfaceMaterial *mat = owningNode->GetMaterial(materialIndex);
            if (mat && materialToName.count(mat)) return materialToName[mat];
        }
        return std::string();
    };
    // PVKey for hashing polygon-vertex attribute set (use indices where possible)
    struct PVKey {
        int cp = -1;
        int normalIndex = -1;
        int tangentIndex = -1;
        int binormalIndex = -1;
        int uvIndex[8];
        int colIndex[8];
        bool operator==(PVKey const &o) const noexcept {
            if (cp != o.cp) return false;
            if (normalIndex != o.normalIndex) return false;
            if (tangentIndex != o.tangentIndex) return false;
            if (binormalIndex != o.binormalIndex) return false;
            for (int i = 0; i < 8; ++i) if (uvIndex[i] != o.uvIndex[i]) return false;
            for (int i = 0; i < 8; ++i) if (colIndex[i] != o.colIndex[i]) return false;
            return true;
        }
    };
    struct PVKeyHash {
        size_t operator()(PVKey const &k) const noexcept {
            size_t h = std::hash<int>()(k.cp + 0x9e3779b1);
            h ^= std::hash<int>()(k.normalIndex + 0x9e3779b1) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(k.tangentIndex + 0x9e3779b1) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(k.binormalIndex + 0x9e3779b1) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            for (int i = 0; i < 8; ++i) h ^= std::hash<int>()(k.uvIndex[i] + 1) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            for (int i = 0; i < 8; ++i) h ^= std::hash<int>()(k.colIndex[i] + 1) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            return h;
        }
    };
    std::function<void(FbxNode *, std::string const &)> NodeCallback;
    NodeCallback = [&](FbxNode *node, std::string const &parentName) {
        if (!node) return;
        std::string nodeName = node->GetName() ? node->GetName() : std::string();
        std::string newParent = parentName;

        if (bonesToInclude.find(node) == bonesToInclude.end()) {
            FbxAMatrix localTransform = node->EvaluateLocalTransform();
            Object &nodeObj = CreateObject(nodeName, parentName, localTransform);
            newParent = nodeObj.name;

            for (int attrIndex = 0; attrIndex < node->GetNodeAttributeCount(); ++attrIndex) {
                FbxNodeAttribute *attr = node->GetNodeAttributeByIndex(attrIndex);
                if (!attr) continue;
                if (attr->GetAttributeType() != FbxNodeAttribute::eMesh) continue;
                FbxMesh *fbxMesh = FbxCast<FbxMesh>(attr);
                if (!fbxMesh) continue;
                // If node has one mesh attribute, use the nodeObj. If multiple, create per-mesh child object.
                Object *objPtr = &nodeObj;
                if (node->GetNodeAttributeCount() > 1) {
                    std::string meshName = fbxMesh->GetName() ? fbxMesh->GetName() : (nodeObj.name + "_mesh" + std::to_string(attrIndex));
                    objPtr = &CreateObject(meshName, nodeObj.name, FbxAMatrix()); // identity transform
                }
                uint32_t vertexFormat = 0;
                vertexFormat |= V_Position;
                bool hasNormals = fbxMesh->GetElementNormalCount() > 0;
                if (hasNormals) vertexFormat |= V_Normal;
                bool hasTangents = fbxMesh->GetElementTangentCount() > 0;
                bool hasBinormals = fbxMesh->GetElementBinormalCount() > 0;
                if (hasTangents) vertexFormat |= V_Tangent;
                if (hasBinormals) vertexFormat |= V_Binormal;

                int numUVChannels = fbxMesh->GetElementUVCount();
                SetNumTexCoords(vertexFormat, (numUVChannels > 8) ? 8 : (uint8_t)numUVChannels);
                int numColorChannels = fbxMesh->GetElementVertexColorCount();
                SetNumColors(vertexFormat, (numColorChannels > 8) ? 8 : (uint8_t)numColorChannels);

                // Build bone lists per control point
                std::vector<std::vector<std::pair<uint16_t, float>>> vertexBoneLists;
                int controlPointCount = fbxMesh->GetControlPointsCount();
                vertexBoneLists.resize(controlPointCount);

                for (int defIndex = 0; defIndex < fbxMesh->GetDeformerCount(FbxDeformer::eSkin); ++defIndex) {
                    FbxSkin *skin = static_cast<FbxSkin *>(fbxMesh->GetDeformer(defIndex, FbxDeformer::eSkin));
                    if (!skin)
                        continue;
                    for (int c = 0; c < skin->GetClusterCount(); ++c) {
                        FbxCluster *cluster = skin->GetCluster(c);
                        if (!cluster)
                            continue;
                        FbxNode *link = cluster->GetLink();
                        if (!link)
                            continue;
                        std::string bname = link->GetName();
                        if (!globalBoneIndex.count(bname))
                            continue;
                        uint16_t gb = globalBoneIndex[bname];
                        const int *cpIndices = cluster->GetControlPointIndices();
                        const double *cpWeights = cluster->GetControlPointWeights();
                        int cpCount = cluster->GetControlPointIndicesCount();
                        for (int wi = 0; wi < cpCount; ++wi) {
                            int cpIndex = cpIndices[wi];
                            float weight = (float)cpWeights[wi];
                            if (cpIndex >= 0 && cpIndex < (int)vertexBoneLists.size()) {
                                vertexBoneLists[cpIndex].emplace_back(gb, weight);
                            }
                        }
                    }
                }
                // Prepare element pointers (may be null)
                FbxLayerElementNormal *nElem = (fbxMesh->GetElementNormalCount() > 0) ? fbxMesh->GetElementNormal(0) : nullptr;
                FbxLayerElementTangent *tElem = (fbxMesh->GetElementTangentCount() > 0) ? fbxMesh->GetElementTangent(0) : nullptr;
                FbxLayerElementBinormal *bElem = (fbxMesh->GetElementBinormalCount() > 0) ? fbxMesh->GetElementBinormal(0) : nullptr;
                int nUV = NumTexCoords(vertexFormat);
                std::vector<FbxLayerElementUV *> uvElems(nUV);
                for (int u = 0; u < nUV; ++u)
                    uvElems[u] = (u < fbxMesh->GetElementUVCount()) ? fbxMesh->GetElementUV(u) : nullptr;
                int nCol = NumColors(vertexFormat);
                std::vector<FbxLayerElementVertexColor *> colElems(nCol);
                for (int c = 0; c < nCol; ++c)
                    colElems[c] = (c < fbxMesh->GetElementVertexColorCount()) ? fbxMesh->GetElementVertexColor(c) : nullptr;

                // Build shared vertex buffer and per-material triangle groups
                std::vector<Vertex> outVertices;
                outVertices.reserve(fbxMesh->GetControlPointsCount() * 2); // heuristic

                // Key: FBX node-local material index (matches GetMaterial(idx) on the owning node).
                // Value: triangle index triples referencing outVertices.
                std::map<int, std::vector<std::array<uint32_t, 3>>> matTriangles;

                std::unordered_map<PVKey, uint32_t, PVKeyHash> keyToIndex;

                // Detect per-polygon material element once, outside the loop.
                FbxLayerElementMaterial *matElem = fbxMesh->GetElementMaterial();
                bool matModeAllSame = matElem && (matElem->GetMappingMode() == FbxGeometryElement::eAllSame);
                int allSameMatIdx = 0;
                if (matModeAllSame && matElem->GetIndexArray().GetCount() > 0)
                    allSameMatIdx = matElem->GetIndexArray().GetAt(0);

                auto getPolyVertexBaseIndex = [&](int p) -> int { return fbxMesh->GetPolygonVertexIndex(p); };

                unsigned int maxBonesUsed = 0;
                const int polyCount = fbxMesh->GetPolygonCount();
                for (int p = 0; p < polyCount; ++p) {
                    int polySize = fbxMesh->GetPolygonSize(p);
                    if (polySize != 3)
                        continue;
                    int basePV = getPolyVertexBaseIndex(p);

                    uint32_t triIdx[3];

                    for (int pv = 0; pv < 3; ++pv) {
                        int cp = fbxMesh->GetPolygonVertex(p, pv);
                        PVKey key;
                        key.cp = cp;
                        for (int i = 0; i < 8; ++i) key.uvIndex[i] = -1;
                        for (int i = 0; i < 8; ++i) key.colIndex[i] = -1;
                        key.normalIndex = -1;
                        key.tangentIndex = -1;
                        key.binormalIndex = -1;

                        if (nElem) {
                            if (nElem->GetMappingMode() == FbxGeometryElement::eByControlPoint)
                                key.normalIndex = cp;
                            else {
                                if (nElem->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
                                    key.normalIndex = nElem->GetIndexArray().GetAt(basePV + pv);
                                else
                                    key.normalIndex = basePV + pv;
                            }
                        }
                        if (tElem) {
                            if (tElem->GetMappingMode() == FbxGeometryElement::eByControlPoint)
                                key.tangentIndex = cp;
                            else
                                key.tangentIndex = (tElem->GetReferenceMode() == FbxLayerElement::eIndexToDirect) ? tElem->GetIndexArray().GetAt(basePV + pv) : basePV + pv;
                        }
                        if (bElem) {
                            if (bElem->GetMappingMode() == FbxGeometryElement::eByControlPoint)
                                key.binormalIndex = cp;
                            else
                                key.binormalIndex = (bElem->GetReferenceMode() == FbxLayerElement::eIndexToDirect) ? bElem->GetIndexArray().GetAt(basePV + pv) : basePV + pv;
                        }
                        for (int u = 0; u < nUV; ++u) {
                            FbxLayerElementUV *uvE = uvElems[u];
                            if (!uvE) { key.uvIndex[u] = -1; continue; }
                            if (uvE->GetMappingMode() == FbxGeometryElement::eByControlPoint)
                                key.uvIndex[u] = cp;
                            else {
                                if (uvE->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
                                    key.uvIndex[u] = uvE->GetIndexArray().GetAt(basePV + pv);
                                else
                                    key.uvIndex[u] = basePV + pv;
                            }
                        }
                        for (int c = 0; c < nCol; ++c) {
                            FbxLayerElementVertexColor *colE = colElems[c];
                            if (!colE) { key.colIndex[c] = -1; continue; }
                            if (colE->GetMappingMode() == FbxGeometryElement::eByControlPoint)
                                key.colIndex[c] = cp;
                            else {
                                if (colE->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
                                    key.colIndex[c] = colE->GetIndexArray().GetAt(basePV + pv);
                                else
                                    key.colIndex[c] = basePV + pv;
                            }
                        }
                        // lookup/create vertex
                        auto it = keyToIndex.find(key);
                        if (it != keyToIndex.end()) {
                            triIdx[pv] = it->second;
                        }
                        else {
                            uint32_t newIndex = (uint32_t)outVertices.size();
                            keyToIndex.emplace(key, newIndex);
                            Vertex &V = outVertices.emplace_back();
                            FbxVector4 cpPos = fbxMesh->GetControlPoints()[cp];
                            V.pos = Vector3((float)cpPos[0], (float)cpPos[1], (float)cpPos[2]);
                            if (nElem) {
                                int dIndex;
                                if (nElem->GetMappingMode() == FbxGeometryElement::eByControlPoint)
                                    dIndex = cp;
                                else {
                                    if (nElem->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
                                        dIndex = nElem->GetIndexArray().GetAt(basePV + pv);
                                    else
                                        dIndex = basePV + pv;
                                }
                                if (dIndex >= 0 && dIndex < nElem->GetDirectArray().GetCount()) {
                                    FbxVector4 nn = nElem->GetDirectArray().GetAt(dIndex);
                                    V.normal = Vector3((float)nn[0], (float)nn[1], (float)nn[2]);
                                }
                                else {
                                    FbxVector4 nn;
                                    if (fbxMesh->GetPolygonVertexNormal(p, pv, nn)) V.normal = Vector3((float)nn[0], (float)nn[1], (float)nn[2]);
                                    else V.normal = Vector3(0, 0, 0);
                                }
                            }
                            if (tElem) {
                                int dIndex = (tElem->GetMappingMode() == FbxGeometryElement::eByControlPoint) ? cp :
                                    (tElem->GetReferenceMode() == FbxLayerElement::eIndexToDirect ? tElem->GetIndexArray().GetAt(basePV + pv) : basePV + pv);
                                if (dIndex >= 0 && dIndex < tElem->GetDirectArray().GetCount()) {
                                    FbxVector4 tt = tElem->GetDirectArray().GetAt(dIndex);
                                    V.tangent = Vector3((float)tt[0], (float)tt[1], (float)tt[2]);
                                }
                            }
                            if (bElem) {
                                int dIndex = (bElem->GetMappingMode() == FbxGeometryElement::eByControlPoint) ? cp :
                                    (bElem->GetReferenceMode() == FbxLayerElement::eIndexToDirect ? bElem->GetIndexArray().GetAt(basePV + pv) : basePV + pv);
                                if (dIndex >= 0 && dIndex < bElem->GetDirectArray().GetCount()) {
                                    FbxVector4 bb = bElem->GetDirectArray().GetAt(dIndex);
                                    V.binormal = Vector3((float)bb[0], (float)bb[1], (float)bb[2]);
                                }
                            }
                            for (int u = 0; u < nUV; ++u) {
                                FbxLayerElementUV *uvE = uvElems[u];
                                if (!uvE) continue;
                                int dIndex;
                                if (uvE->GetMappingMode() == FbxGeometryElement::eByControlPoint)
                                    dIndex = cp;
                                else {
                                    int uvIdx = fbxMesh->GetTextureUVIndex(p, pv);
                                    if (uvE->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
                                        dIndex = uvE->GetIndexArray().GetAt(basePV + pv);
                                    else
                                        dIndex = uvIdx;
                                }
                                if (dIndex >= 0 && dIndex < uvE->GetDirectArray().GetCount()) {
                                    FbxVector2 uvt = uvE->GetDirectArray().GetAt(dIndex);
                                    V.uv[u] = Vector2((float)uvt[0], (float)uvt[1]);
                                }
                                else {
                                    FbxVector2 uvt; bool unmapped = false;
                                    if (fbxMesh->GetPolygonVertexUV(p, pv, uvE->GetName(), uvt, unmapped) && !unmapped)
                                        V.uv[u] = Vector2((float)uvt[0], (float)uvt[1]);
                                }
                            }
                            for (int c = 0; c < nCol; ++c) {
                                FbxLayerElementVertexColor *colE = colElems[c];
                                if (!colE) continue;
                                int dIndex;
                                if (colE->GetMappingMode() == FbxGeometryElement::eByControlPoint)
                                    dIndex = cp;
                                else {
                                    dIndex = (colE->GetReferenceMode() == FbxLayerElement::eIndexToDirect) ? colE->GetIndexArray().GetAt(basePV + pv) : basePV + pv;
                                }
                                if (dIndex >= 0 && dIndex < colE->GetDirectArray().GetCount()) {
                                    FbxColor fc = colE->GetDirectArray().GetAt(dIndex);
                                    V.colors[c].Set(fc.mRed, fc.mGreen, fc.mBlue, fc.mAlpha);
                                }
                            }
                            if (!vertexBoneLists.empty() && cp >= 0 && cp < (int)vertexBoneLists.size()) {
                                auto bl = vertexBoneLists[cp];
                                if (!bl.empty()) {
                                    std::sort(bl.begin(), bl.end(), [](auto &a, auto &b) { return a.second > b.second; });
                                    unsigned int take = std::min<unsigned int>(8, (unsigned int)bl.size());
                                    float sum = 0.0f;
                                    for (unsigned int bi = 0; bi < take; ++bi) {
                                        V.boneIndices[bi] = bl[bi].first;
                                        V.boneWeights[bi] = bl[bi].second;
                                        sum += V.boneWeights[bi];
                                    }
                                    if (sum > 0.0f) {
                                        for (unsigned int bi = 0; bi < take; ++bi)
                                            V.boneWeights[bi] /= sum;
                                    }
                                    for (unsigned int bi = take; bi < 8; ++bi) {
                                        V.boneIndices[bi] = 0;
                                        V.boneWeights[bi] = 0.0f;
                                    }
                                    if (take > maxBonesUsed)
                                        maxBonesUsed = take;
                                }
                                else {
                                    for (unsigned int bi = 0; bi < 8; ++bi) {
                                        V.boneIndices[bi] = 0;
                                        V.boneWeights[bi] = 0.0f;
                                    }
                                }
                            }
                            else {
                                for (unsigned int bi = 0; bi < 8; ++bi) {
                                    V.boneIndices[bi] = 0;
                                    V.boneWeights[bi] = 0.0f;
                                }
                            }
                            triIdx[pv] = newIndex;
                        }
                    } // pv loop

                    // Determine which material slot this polygon belongs to, then bucket its triangle.
                    int matIdx = 0;
                    if (matElem) {
                        if (matModeAllSame)
                            matIdx = allSameMatIdx;
                        else
                            matIdx = matElem->GetIndexArray().GetAt(p);
                    }
                    matTriangles[matIdx].push_back({ triIdx[0], triIdx[1], triIdx[2] });
                } // polygon loop

                // Commit to Object
                objPtr->vertices = std::move(outVertices);
                if (maxBonesUsed > 8)
                    maxBonesUsed = 8;
                SetNumBones(vertexFormat, maxBonesUsed);
                objPtr->vertexFormat = vertexFormat;
                for (int u = 0; u < nUV && u < 8; ++u)
                    if (uvElems[u]) objPtr->uvLayerNames[u] = uvElems[u]->GetName() ? uvElems[u]->GetName() : std::string();
                for (int c = 0; c < nCol && c < 8; ++c)
                    if (colElems[c]) objPtr->colorLayerNames[c] = colElems[c]->GetName() ? colElems[c]->GetName() : std::string();

                // Build one Mesh sub-object per unique material index used in this FBX mesh.
                // The triangle index lists share the single vertex buffer already stored on objPtr.
                for (auto &[idx, tris] : matTriangles) {
                    Mesh m;
                    m.material = GetMaterialNameForMesh(fbxMesh, idx);
                    m.triangles = std::move(tris);
                    objPtr->meshes.push_back(std::move(m));
                }
            } // attribute loop
        }
        for (int i = 0; i < node->GetChildCount(); ++i)
            NodeCallback(node->GetChild(i), newParent);
    };
    for (int c = 0; c < root->GetChildCount(); ++c)
        NodeCallback(root->GetChild(c), "");
    ios->Destroy();
    sdkManager->Destroy();
}

void Model::WriteFbx(std::filesystem::path const &filename, bool ascii) {
    auto getObjectIndex = [&](std::string const &name) {
        for (size_t i = 0; i < objects.size(); i++) {
            if (objects[i].name == name)
                return (int)i;
        }
        return -1;
    };
    auto getMaterialIndex = [&](std::string const &name) {
        for (size_t i = 0; i < materials.size(); i++) {
            if (materials[i].name == name)
                return (int)i;
        }
        return -1;
    };
    auto getBoneIndex = [&](std::string const &name) {
        for (size_t i = 0; i < skeleton.bones.size(); i++) {
            if (skeleton.bones[i].name == name)
                return (int)i;
        }
        return -1;
    };
    auto attachProperties = [](FbxObject *obj, std::map<std::string, PropertyValue> const &props) {
        for (auto &[key, value] : props) {
            std::visit([&](auto &&val) {
                using T = std::decay_t<decltype(val)>;
                FbxProperty prop;
                if constexpr (std::is_same_v<T, int>) {
                    prop = FbxProperty::Create(obj, FbxIntDT, key.c_str());
                    prop.ModifyFlag(FbxPropertyFlags::eUserDefined, true);
                    prop.Set(val);
                }
                else if constexpr (std::is_same_v<T, float>) {
                    prop = FbxProperty::Create(obj, FbxFloatDT, key.c_str());
                    prop.ModifyFlag(FbxPropertyFlags::eUserDefined, true);
                    prop.Set(val);
                }
                else if constexpr (std::is_same_v<T, double>) {
                    prop = FbxProperty::Create(obj, FbxDoubleDT, key.c_str());
                    prop.ModifyFlag(FbxPropertyFlags::eUserDefined, true);
                    prop.Set(val);
                }
                else if constexpr (std::is_same_v<T, std::string>) {
                    prop = FbxProperty::Create(obj, FbxStringDT, key.c_str());
                    prop.ModifyFlag(FbxPropertyFlags::eUserDefined, true);
                    prop.Set(FbxString(val.c_str()));
                }
                else if constexpr (std::is_same_v<T, Vector2>) {
                    prop = FbxProperty::Create(obj, FbxDouble2DT, key.c_str());
                    prop.ModifyFlag(FbxPropertyFlags::eUserDefined, true);
                    prop.Set(ToFbx(val));
                }
                else if constexpr (std::is_same_v<T, Vector3>) {
                    prop = FbxProperty::Create(obj, FbxDouble3DT, key.c_str());
                    prop.ModifyFlag(FbxPropertyFlags::eUserDefined, true);
                    prop.Set(ToFbx(val));
                }
                else if constexpr (std::is_same_v<T, Vector4>) {
                    prop = FbxProperty::Create(obj, FbxDouble4DT, key.c_str());
                    prop.ModifyFlag(FbxPropertyFlags::eUserDefined, true);
                    prop.Set(ToFbx(val));
                }
            }, value);
        }
    };
    FbxManager *fbxManager = FbxManager::Create();
    if (!fbxManager)
        throw std::runtime_error("unable to create fbx manager");
    FbxScene *fbxScene = FbxScene::Create(fbxManager, filename.stem().string().c_str());
    attachProperties(fbxScene, properties);
    // create nodes
    std::vector<FbxNode *> fbxNodes(objects.size());
    for (size_t nodeIdx = 0; nodeIdx < objects.size(); nodeIdx++) {
        Object const &obj = objects[nodeIdx];
        FbxNode *fbxNode = FbxNode::Create(fbxScene, obj.name.c_str());
        attachProperties(fbxNode, obj.properties);
        fbxNode->LclTranslation.Set(ToFbx(obj.transform.GetTranslation()));
        fbxNode->LclRotation.Set(ToFbx(obj.transform.GetRotation()));
        fbxNode->LclScaling.Set(ToFbx(obj.transform.GetScaling()));
        fbxNodes[nodeIdx] = fbxNode;
    }
    // setup nodes hierarchy
    FbxNode *root = fbxScene->GetRootNode();
    for (size_t nodeIdx = 0; nodeIdx < objects.size(); nodeIdx++) {
        int parentIndex = getObjectIndex(objects[nodeIdx].parent);
        if (parentIndex >= 0 && parentIndex < (int)objects.size())
            fbxNodes[parentIndex]->AddChild(fbxNodes[nodeIdx]);
        else
            root->AddChild(fbxNodes[nodeIdx]);
    }
    bool isSkinned = !skeleton.bones.empty();
    // create textures
    std::map<std::string, FbxFileTexture *> texByName;
    std::vector<FbxFileTexture *> fbxTextures(textures.size());
    for (size_t texIdx = 0; texIdx < textures.size(); texIdx++) {
        Texture const &tex = textures[texIdx];
        FbxFileTexture *fbxTex = FbxFileTexture::Create(fbxScene, tex.name.c_str());
        attachProperties(fbxTex, tex.properties);
        fbxTex->SetFileName(tex.filename.c_str());
        fbxTextures[texIdx] = fbxTex;
        texByName[tex.name] = fbxTex;
    }
    // create materials
    std::vector<FbxSurfaceMaterial *> fbxMaterials(materials.size());
    for (size_t matIdx = 0; matIdx < materials.size(); matIdx++) {
        const Material &mat = materials[matIdx];
        FbxSurfacePhong *fbxMat = FbxSurfacePhong::Create(fbxScene, mat.name.c_str());
        if (mat.color.r != 255 || mat.color.g != 255 || mat.color.b != 255)
            fbxMat->Diffuse.Set(FbxDouble3(double(mat.color.r) / 255.0, double(mat.color.g) / 255.0, double(mat.color.b) / 255.0));
        if (mat.color.a != 255)
            fbxMat->TransparencyFactor.Set(1.0 - (double(mat.color.a) / 255.0));
        if (!mat.texture.empty() && texByName.contains(mat.texture))
            fbxMat->Diffuse.ConnectSrcObject(texByName[mat.texture]);
        if (!mat.normalMap.empty() && texByName.contains(mat.normalMap))
            fbxMat->Bump.ConnectSrcObject(texByName[mat.normalMap]);
        attachProperties(fbxMat, mat.properties);
        fbxMaterials[matIdx] = fbxMat;
    }
    std::vector<FbxMesh *> fbxMeshes;
    // create meshes
    for (size_t nodeIdx = 0; nodeIdx < objects.size(); nodeIdx++) {
        Object const &obj = objects[nodeIdx];
        FbxMesh *fbxMesh = nullptr;
        if (!obj.meshes.empty()) {
            fbxMesh = FbxMesh::Create(fbxScene, obj.name.c_str());
            if (obj.vertices.empty())
                fbxMesh->InitControlPoints(0);
            else {
                int cpCount = (int)obj.vertices.size();
                fbxMesh->InitControlPoints(cpCount);
                FbxVector4 *controlPoints = fbxMesh->GetControlPoints();
                for (int i = 0; i < cpCount; ++i) {
                    const Vertex &v = obj.vertices[i];
                    controlPoints[i] = FbxVector4((double)v.pos.x, (double)v.pos.y, (double)v.pos.z, 1.0);
                }
            }
            //int numPolygonVertices = (int)mesh.triangles.size() * 3;
            FbxLayerElementNormal *leNormal = nullptr;
            if (obj.vertexFormat & V_Normal) {
                leNormal = fbxMesh->CreateElementNormal();
                leNormal->SetMappingMode(FbxLayerElement::eByPolygonVertex);
                leNormal->SetReferenceMode(FbxLayerElement::eDirect);
            }
            FbxLayerElementTangent *leTangent = nullptr;
            if (obj.vertexFormat & V_Tangent) {
                leTangent = fbxMesh->CreateElementTangent();
                leTangent->SetMappingMode(FbxLayerElement::eByPolygonVertex);
                leTangent->SetReferenceMode(FbxLayerElement::eDirect);
            }
            FbxLayerElementBinormal *leBinormal = nullptr;
            if (obj.vertexFormat & V_Binormal) {
                leBinormal = fbxMesh->CreateElementBinormal();
                leBinormal->SetMappingMode(FbxLayerElement::eByPolygonVertex);
                leBinormal->SetReferenceMode(FbxLayerElement::eDirect);
            }
            std::vector<FbxLayerElementUV *> uvLayers;
            for (unsigned char uvSet = 0; uvSet < NumTexCoords(obj.vertexFormat); ++uvSet) {
                std::string uvName = (!obj.uvLayerNames[uvSet].empty()) ? obj.uvLayerNames[uvSet] : "UV" + std::to_string(uvSet);
                FbxLayerElementUV *leUV = fbxMesh->CreateElementUV(uvName.c_str());
                leUV->SetMappingMode(FbxLayerElement::eByPolygonVertex);
                leUV->SetReferenceMode(FbxLayerElement::eDirect);
                uvLayers.push_back(leUV);
            }
            std::vector<FbxLayerElementVertexColor *> colorLayers;
            for (unsigned char c = 0; c < NumColors(obj.vertexFormat); ++c) {
                FbxLayerElementVertexColor *leVC = fbxMesh->CreateElementVertexColor();
                if (!obj.colorLayerNames[c].empty())
                    leVC->SetName(obj.colorLayerNames[c].c_str());
                leVC->SetMappingMode(FbxGeometryElement::eByPolygonVertex);
                leVC->SetReferenceMode(FbxGeometryElement::eDirect);
                colorLayers.push_back(leVC);
            }
            FbxLayerElementMaterial *leMat = fbxMesh->CreateElementMaterial();
            leMat->SetMappingMode(FbxLayerElement::eByPolygon);
            leMat->SetReferenceMode(FbxLayerElement::eIndexToDirect);
            std::map<int, int> globalToLocal;
            int nextLocalIdx = 0;
            for (auto const &mesh : obj.meshes) {
                for (size_t t = 0; t < mesh.triangles.size(); ++t) {
                    uint32_t i0 = mesh.triangles[t][0];
                    uint32_t i1 = mesh.triangles[t][1];
                    uint32_t i2 = mesh.triangles[t][2];
                    fbxMesh->BeginPolygon(-1, -1, false);
                    fbxMesh->AddPolygon((int)i0);
                    fbxMesh->AddPolygon((int)i1);
                    fbxMesh->AddPolygon((int)i2);
                    fbxMesh->EndPolygon();
                    const unsigned int cornerIdx[3] = { i0, i1, i2 };
                    for (int corner = 0; corner < 3; ++corner) {
                        const Vertex &v = obj.vertices[cornerIdx[corner]];
                        if (leNormal)
                            leNormal->GetDirectArray().Add(FbxVector4((double)v.normal.x, (double)v.normal.y, (double)v.normal.z));
                        if (leTangent)
                            leTangent->GetDirectArray().Add(FbxVector4((double)v.tangent.x, (double)v.tangent.y, (double)v.tangent.z));
                        if (leBinormal)
                            leBinormal->GetDirectArray().Add(FbxVector4((double)v.binormal.x, (double)v.binormal.y, (double)v.binormal.z));
                        for (unsigned int uvSet = 0; uvSet < uvLayers.size(); ++uvSet) {
                            const Vector2 &uv = v.uv[uvSet];
                            uvLayers[uvSet]->GetDirectArray().Add(FbxVector2((double)uv.x, (double)uv.y));
                        }
                        for (unsigned int cIdx = 0; cIdx < colorLayers.size(); ++cIdx) {
                            const RGBA &col = v.colors[cIdx];
                            colorLayers[cIdx]->GetDirectArray().Add(FbxColor((double)col.r / 255.0,
                                (double)col.g / 255.0, (double)col.b / 255.0, (double)col.a / 255.0));
                        }
                    }
                }
                int localIdx = 0;
                if (!mesh.material.empty()) {
                    int matIdx = getMaterialIndex(mesh.material);
                    if (matIdx != -1) {
                        auto [it, inserted] = globalToLocal.emplace(matIdx, nextLocalIdx);
                        if (inserted) {
                            fbxNodes[nodeIdx]->AddMaterial(fbxMaterials[matIdx]);
                            ++nextLocalIdx;
                        }
                        localIdx = it->second;
                    }
                }
                for (size_t p = 0; p < mesh.triangles.size(); ++p)
                    leMat->GetIndexArray().Add(localIdx);
            }
            if (!uvLayers.empty()) {
                FbxLayer *layer = fbxMesh->GetLayer(0);
                if (!layer) {
                    fbxMesh->CreateLayer();
                    layer = fbxMesh->GetLayer(0);
                }
                layer->SetUVs(uvLayers[0], FbxLayerElement::eTextureDiffuse);
            }
            fbxNodes[nodeIdx]->SetNodeAttribute(fbxMesh);
        }
        fbxMeshes.push_back(fbxMesh);
    }
    // skeleton
    if (isSkinned) {
        // create bones
        std::vector<FbxNode *> boneNodes;
        boneNodes.resize(skeleton.bones.size());
        for (size_t boneIdx = 0; boneIdx < skeleton.bones.size(); boneIdx++) {
            const Bone &bone = skeleton.bones[boneIdx];
            FbxSkeleton *fbxSkeleton = FbxSkeleton::Create(fbxScene, bone.name.c_str());
            attachProperties(fbxSkeleton, skeleton.properties);
            int parentIndex = getBoneIndex(bone.parent);
            fbxSkeleton->SetSkeletonType(parentIndex == -1 ? FbxSkeleton::EType::eRoot : FbxSkeleton::EType::eLimbNode);
            FbxNode *fbxNode = FbxNode::Create(fbxScene, bone.name.c_str());
            attachProperties(fbxNode, bone.properties);
            fbxNode->SetNodeAttribute(fbxSkeleton);
            fbxNode->LclTranslation.Set(ToFbx(bone.matrix.GetTranslation()));
            fbxNode->LclRotation.Set(ToFbx(bone.matrix.GetRotation()));
            fbxNode->LclScaling.Set(ToFbx(bone.matrix.GetScaling()));
            boneNodes[boneIdx] = fbxNode;
        }
        // setup bones hierarchy
        for (size_t boneIdx = 0; boneIdx < skeleton.bones.size(); boneIdx++) {
            int parentIndex = getBoneIndex(skeleton.bones[boneIdx].parent);
            if (parentIndex == -1)
                root->AddChild(boneNodes[boneIdx]);
            else
                boneNodes[parentIndex]->AddChild(boneNodes[boneIdx]);
        }
        // setup skinning
        for (size_t nodeIdx = 0; nodeIdx < objects.size(); nodeIdx++) {
            if (fbxMeshes[nodeIdx]) {
                Object const &obj = objects[nodeIdx];
                FbxNode *meshNode = fbxNodes[nodeIdx];
                FbxMesh *fbxMesh = fbxMeshes[nodeIdx];
                FbxSkin *fbxSkin = FbxSkin::Create(fbxScene, "Skin");
                std::vector<FbxCluster *> fbxClusters(skeleton.bones.size());
                for (size_t boneIdx = 0; boneIdx < skeleton.bones.size(); boneIdx++) {
                    FbxCluster *fbxCluster = FbxCluster::Create(fbxScene, ("Cluster_" + skeleton.bones[boneIdx].name).c_str());
                    fbxCluster->SetLink(boneNodes[boneIdx]);
                    fbxCluster->SetLinkMode(FbxCluster::eNormalize);
                    fbxClusters[boneIdx] = fbxCluster;
                }
                for (size_t boneIdx = 0; boneIdx < skeleton.bones.size(); boneIdx++) {
                    for (size_t v = 0; v < obj.vertices.size(); v++) {
                        for (size_t wi = 0; wi < NumBones(obj.vertexFormat); wi++) {
                            if (obj.vertices[v].boneIndices[wi] == boneIdx) {
                                float w = obj.vertices[v].boneWeights[wi];
                                if (w > 0.0f)
                                    fbxClusters[boneIdx]->AddControlPointIndex((int)v, (double)w);
                            }
                        }
                    }
                }
                for (size_t boneIdx = 0; boneIdx < skeleton.bones.size(); boneIdx++) {
                    fbxClusters[boneIdx]->SetTransformMatrix(meshNode->EvaluateGlobalTransform());
                    fbxClusters[boneIdx]->SetTransformLinkMatrix(boneNodes[boneIdx]->EvaluateGlobalTransform());
                    fbxSkin->AddCluster(fbxClusters[boneIdx]);
                }
                fbxMesh->AddDeformer(fbxSkin);
            }
        }
    }
    // export
    FbxExporter *fbxExporter = FbxExporter::Create(fbxManager, "");
    char *pFilenameUtf8 = nullptr;
    FbxWCToUTF8(filename.c_str(), pFilenameUtf8);
    int fileFormat = -1;
    if (ascii) {
        int numFormats = fbxManager->GetIOPluginRegistry()->GetWriterFormatCount();
        for (int i = 0; i < numFormats; ++i) {
            if (fbxManager->GetIOPluginRegistry()->WriterIsFBX(i)) {
                FbxString desc = fbxManager->GetIOPluginRegistry()->GetWriterFormatDescription(i);
                if (desc.Find("ascii") >= 0) {
                    fileFormat = i;
                    break;
                }
            }
        }
    }
    bool exportStatus = fbxExporter->Initialize(pFilenameUtf8, fileFormat, fbxManager->GetIOSettings());
    FbxFree(pFilenameUtf8);
    if (exportStatus)
        fbxExporter->Export(fbxScene);
    fbxExporter->Destroy();
    fbxManager->Destroy();
}

namespace ObjHelper {

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

void Model::ReadObj(std::filesystem::path const &filename) {
    using namespace ObjHelper;
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
        std::vector<std::array<uint32_t, 3>> triangles;
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
        else if (token == "o" || token == "g") {
            std::string groupName;
            std::getline(ss, groupName);
            TrimInPlace(groupName);
            bool hasFaces = cur &&
                std::any_of(cur->groups.begin(), cur->groups.end(),
                    [](const FaceGroup &fg) { return !fg.triangles.empty(); });
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
            uint32_t i0 = ResolveCorner(fvTokens[0]);
            for (size_t vi = 1; vi + 1 < fvTokens.size(); ++vi) {
                uint32_t i1 = ResolveCorner(fvTokens[vi]);
                uint32_t i2 = ResolveCorner(fvTokens[vi + 1]);
                fg->triangles.push_back({ i0, i1, i2 });
            }
        }
    }
    for (auto &og : objGroups) {
        bool anyFaces = std::any_of(og.groups.begin(), og.groups.end(),
            [](const FaceGroup &fg) { return !fg.triangles.empty(); });
        if (og.vertices.empty() && !anyFaces) continue;
        Object obj;
        obj.name = MakeUniqueObjName(og.name);
        obj.parent.clear();
        obj.transform = Matrix4x4(); // identity
        obj.vertexFormat = og.vertexFormat;
        obj.vertices = std::move(og.vertices);
        for (auto &fg : og.groups) {
            if (fg.triangles.empty()) continue;
            Mesh m;
            m.material = fg.material;
            m.triangles = std::move(fg.triangles);
            obj.meshes.push_back(std::move(m));
        }
        objects.push_back(std::move(obj));
    }
}

void Model::WriteObj(std::filesystem::path const &filename) {
    using namespace ObjHelper;
    auto mtlPath = WithExt(filename, ".mtl");
    std::string mtlName = mtlPath.filename().string();
    {
        std::ofstream mf(mtlPath);
        if (!mf.is_open())
            throw std::runtime_error("WriteObj: cannot open MTL file: " + mtlPath.string());
        auto FindTexFile = [&](const std::string &texName) -> std::string {
            for (auto &t : textures)
                if (t.name == texName) return t.filename;
            return texName;
        };
        mf << "# Generated by Model::WriteObj\n\n";
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
    of << "# Generated by Model::WriteObj\n";
    of << "mtllib " << mtlName << "\n\n";
    uint32_t posBase = 1;
    uint32_t uvBase = 1;
    uint32_t normBase = 1;
    for (auto &obj : objects) {
        if (obj.meshes.empty()) continue;
        bool hasUV = (NumTexCoords(obj.vertexFormat) > 0);
        bool hasNormal = (obj.vertexFormat & V_Normal) != 0;
        of << "# object " << obj.name << "\n";
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
        of << "g " << obj.name << "\n";
        for (auto &mesh : obj.meshes) {
            if (mesh.triangles.empty()) continue;
            if (!mesh.material.empty())
                of << "usemtl " << mesh.material << "\n";
            else
                of << "usemtl (none)\n";
            for (auto &tri : mesh.triangles) {
                of << "f";
                for (int c = 0; c < 3; ++c) {
                    uint32_t vi = tri[c];
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
                }
                of << "\n";
            }
        }
        of << "\n";
        uint32_t nv = (uint32_t)obj.vertices.size();
        posBase += nv;
        if (hasUV)     uvBase += nv;
        if (hasNormal) normBase += nv;
    }
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

Mesh &Object::firstMesh() {
    if (meshes.empty())
        meshes.push_back(Mesh());
    return meshes[0];
}
