#include "Model.h"
#include <fstream>
#include "ModelFbxSdkHeader.h"
#include "ModelTypeConversion.h"
#include "Error.h"
#undef min
#undef max

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
    if (!options.AllowQuads) {
        geomConv.Triangulate(scene, true);
        if (!scene) {
            ios->Destroy();
            sdkManager->Destroy();
            throw std::runtime_error("unable to load complete scene");
        }
    }
    FbxNode *root = scene->GetRootNode();
    if (!root) {
        ios->Destroy();
        sdkManager->Destroy();
        throw std::runtime_error("unable to find scene root node");
    }

    name = filename.stem().string();
    std::map<std::string, size_t> texKeyToIndex;
    std::set<std::string> usedTexNames;
    auto MakeUniqueTexName = [&usedTexNames](const std::string &base) {
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
    auto isEmptyContainer = [&](FbxNode *n) -> bool {
        if (!n) return false;
        FbxNodeAttribute *a = n->GetNodeAttribute();
        return !a || a->GetAttributeType() == FbxNodeAttribute::eNull;
    };
    if (!explicitSkeletonNodes.empty()) {
        std::function<void(FbxNode *)> addDescendants = [&](FbxNode *n) {
            if (!n) return;
            if (bonesToInclude.insert(n).second == false) return;
            for (int i = 0; i < n->GetChildCount(); ++i) addDescendants(n->GetChild(i));
        };
        for (FbxNode *n : explicitSkeletonNodes) addDescendants(n);

        std::set<FbxNode *> topMost;
        for (FbxNode *n : explicitSkeletonNodes) {
            FbxNode *p = n->GetParent();
            if (!p || explicitSkeletonNodes.find(p) == explicitSkeletonNodes.end())
                topMost.insert(n);
        }
        for (FbxNode *n : topMost) {
            FbxNode *cur = n->GetParent();
            while (cur && cur != root) {
                if (bonesToInclude.count(cur))
                    break;
                if (isSkeletonNode(cur) || isEmptyContainer(cur) || cur->GetChildCount() > 1) {
                    bonesToInclude.insert(cur);
                    cur = cur->GetParent();
                    continue;
                }
                break;
            }
        }
        for (FbxNode *n : bonesToInclude) {
            FbxNode *p = n->GetParent();
            if (!p || bonesToInclude.find(p) == bonesToInclude.end())
                skeletonRoots.push_back(n);
        }
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
    int maxPolySize = options.AllowQuads ? 4 : 3;
    std::function<void(FbxNode *, std::string const &)> NodeCallback;
    NodeCallback = [&](FbxNode *node, std::string const &parentName) {
        if (!node) return;
        std::string nodeName = node->GetName() ? node->GetName() : std::string();
        std::string newParent = parentName;

        if (bonesToInclude.find(node) == bonesToInclude.end()) {
            FbxAMatrix localTransform = node->EvaluateLocalTransform();
            Object &nodeObj = CreateObject(nodeName, parentName, localTransform);
            newParent = nodeObj.name;
            Object *objPtr = &nodeObj;

            for (int attrIndex = 0; attrIndex < node->GetNodeAttributeCount(); ++attrIndex) {
                FbxNodeAttribute *attr = node->GetNodeAttributeByIndex(attrIndex);
                if (!attr) continue;
                if (attr->GetAttributeType() != FbxNodeAttribute::eMesh) continue;
                FbxMesh *fbxMesh = FbxCast<FbxMesh>(attr);
                if (!fbxMesh) continue;
                // If node has one mesh attribute, use the nodeObj. If multiple, create per-mesh child object.
                if (node->GetNodeAttributeCount() > 1) {
                    std::string meshName = fbxMesh->GetName() ? fbxMesh->GetName() : (newParent + "_mesh" + std::to_string(attrIndex));
                    objPtr = &CreateObject(meshName, newParent, FbxAMatrix()); // identity transform
                }
                uint32_t vertexFormat = 0;
                vertexFormat |= V_Position;
                bool hasNormals = fbxMesh->GetElementNormalCount() > 0;
                if (hasNormals) vertexFormat |= V_Normal;
                bool hasTangents = fbxMesh->GetElementTangentCount() > 0;
                bool hasBinormals = fbxMesh->GetElementBinormalCount() > 0;
                if (hasTangents) vertexFormat |= V_Tangent;
                if (hasBinormals) vertexFormat |= V_Binormal;

                auto isUsableUV = [](FbxLayerElementUV *e) {
                    return e && e->GetMappingMode() != FbxLayerElement::eNone && e->GetDirectArray().GetCount() > 0;
                };

                std::vector<FbxLayerElementUV *> uvElems;
                for (int u = 0; u < fbxMesh->GetElementUVCount(); ++u) {
                    FbxLayerElementUV *e = fbxMesh->GetElementUV(u);
                    if (isUsableUV(e)) uvElems.push_back(e);
                }
                if (uvElems.size() > 8) uvElems.resize(8);
                int nUV = (int)uvElems.size();
                SetNumTexCoords(vertexFormat, (uint8_t)uvElems.size());

                std::vector<FbxLayerElementVertexColor *> colElems;
                for (int c = 0; c < fbxMesh->GetElementVertexColorCount(); ++c) {
                    FbxLayerElementVertexColor *ce = fbxMesh->GetElementVertexColor(c);
                    if (!ce) continue;
                    if (ce->GetMappingMode() == FbxLayerElement::eNone) continue;
                    if (ce->GetDirectArray().GetCount() == 0) continue; // skip orphaned/empty set
                    colElems.push_back(ce);
                }
                int nCol = std::min((int)colElems.size(), 8);
                SetNumColors(vertexFormat, (uint8_t)nCol);

                // Build bone lists per control point
                std::vector<std::vector<std::pair<uint16_t, float>>> vertexBoneLists;
                int controlPointCount = fbxMesh->GetControlPointsCount();
                vertexBoneLists.resize(controlPointCount);
                // Needed to expand per-control-point blend shape deltas to the split vertex buffer.
                std::vector<std::vector<uint32_t>> cpToOutVerts(controlPointCount);

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

                // Build shared vertex buffer and per-material triangle groups
                std::vector<Vertex> outVertices;
                outVertices.reserve(fbxMesh->GetControlPointsCount() * 2); // heuristic

                // Key: FBX node-local material index (matches GetMaterial(idx) on the owning node).
                // Value: triangle index triples referencing outVertices.
                std::map<int, std::vector<std::array<uint32_t, 4>>> matPolys;

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
                    if (polySize != 3 && polySize != maxPolySize)
                        continue;
                    int basePV = getPolyVertexBaseIndex(p);

                    uint32_t polyIdx[4];

                    for (int pv = 0; pv < polySize; ++pv) {
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
                            polyIdx[pv] = it->second;
                        }
                        else {
                            uint32_t newIndex = (uint32_t)outVertices.size();
                            keyToIndex.emplace(key, newIndex);
                            Vertex &V = outVertices.emplace_back();
                            if (cp >= 0 && cp < (int)cpToOutVerts.size())
                                cpToOutVerts[cp].push_back(newIndex);
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
                            polyIdx[pv] = newIndex;
                        }
                    } // pv loop

                    // Determine which material slot this polygon belongs to, then bucket its poly.
                    int matIdx = 0;
                    if (matElem) {
                        if (matModeAllSame)
                            matIdx = allSameMatIdx;
                        else
                            matIdx = matElem->GetIndexArray().GetAt(p);
                    }
                    if (polySize == 3)
                        polyIdx[3] = polyIdx[2];
                    matPolys[matIdx].push_back({ polyIdx[0], polyIdx[1], polyIdx[2], polyIdx[3] });
                } // polygon loop

                // Shape keys / blend shapes
                for (int bsDef = 0; bsDef < fbxMesh->GetDeformerCount(FbxDeformer::eBlendShape); ++bsDef) {
                    FbxBlendShape *blendShape = static_cast<FbxBlendShape *>(fbxMesh->GetDeformer(bsDef, FbxDeformer::eBlendShape));
                    if (!blendShape) continue;
                    for (int chIdx = 0; chIdx < blendShape->GetBlendShapeChannelCount(); ++chIdx) {
                        FbxBlendShapeChannel *channel = blendShape->GetBlendShapeChannel(chIdx);
                        if (!channel || channel->GetTargetShapeCount() == 0) continue;
                        // Only the full-weight (100%) target is kept; in-between/progressive
                        // targets at partial weights are ignored (see note below).
                        FbxShape *shape = channel->GetTargetShape(channel->GetTargetShapeCount() - 1);
                        if (!shape) continue;

                        ShapeKey shapeKey;
                        shapeKey.name = channel->GetName() ? channel->GetName() : ("shape_" + std::to_string(chIdx));

                        int shapeCPCount = std::min(shape->GetControlPointsCount(), controlPointCount);
                        FbxVector4 *shapeCPs = shape->GetControlPoints();
                        FbxVector4 const *baseCPs = fbxMesh->GetControlPoints();

                        bool shapeHasNormals = shape->GetElementNormalCount() > 0;
                        FbxLayerElementNormal *shapeNormalElem = shapeHasNormals ? shape->GetElementNormal(0) : nullptr;
                        // Only trivially correct when both base and shape normals are eByControlPoint.
                        // If your source meshes use eByPolygonVertex normals (hard edges/smoothing groups),
                        // treat deltaNormal as approximate, or just leave it zero and re-derive normals
                        // after applying deltaPos at runtime.
                        bool normalsByCP = nElem && nElem->GetMappingMode() == FbxGeometryElement::eByControlPoint &&
                            shapeNormalElem && shapeNormalElem->GetMappingMode() == FbxGeometryElement::eByControlPoint;

                        for (int cp = 0; cp < shapeCPCount; ++cp) {
                            FbxVector4 base = baseCPs[cp];
                            Vector3 deltaPos((float)(shapeCPs[cp][0] - base[0]),
                                (float)(shapeCPs[cp][1] - base[1]),
                                (float)(shapeCPs[cp][2] - base[2]));
                            Vector3 deltaNormal(0, 0, 0);
                            if (normalsByCP && cp < nElem->GetDirectArray().GetCount() &&
                                cp < shapeNormalElem->GetDirectArray().GetCount()) {
                                FbxVector4 baseN = nElem->GetDirectArray().GetAt(cp);
                                FbxVector4 shapeN = shapeNormalElem->GetDirectArray().GetAt(cp);
                                deltaNormal = Vector3((float)(shapeN[0] - baseN[0]),
                                    (float)(shapeN[1] - baseN[1]),
                                    (float)(shapeN[2] - baseN[2]));
                            }
                            float d2 = deltaPos.x * deltaPos.x + deltaPos.y * deltaPos.y + deltaPos.z * deltaPos.z +
                                deltaNormal.x * deltaNormal.x + deltaNormal.y * deltaNormal.y + deltaNormal.z * deltaNormal.z;
                            if (d2 < 1e-12f) continue; // skip unaffected control points -> keeps ShapeKey sparse

                            for (uint32_t outIdx : cpToOutVerts[cp]) {
                                ShapeKeyVertex skv;
                                skv.vertexIndex = outIdx;
                                skv.deltaPos = deltaPos;
                                skv.deltaNormal = deltaNormal;
                                shapeKey.vertices.push_back(skv);
                            }
                        }
                        if (!shapeKey.vertices.empty())
                            objPtr->shapeKeys.push_back(std::move(shapeKey));
                    }
                }

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
                // The poly index lists share the single vertex buffer already stored on objPtr.
                for (auto &[idx, polys] : matPolys) {
                    Mesh m;
                    m.material = GetMaterialNameForMesh(fbxMesh, idx);
                    bool hasQuads = false;
                    if (options.AllowQuads) {
                        for (auto const &poly : polys) {
                            if (poly[2] != poly[3]) {
                                hasQuads = true;
                                break;
                            }
                        }
                    }
                    if (hasQuads)
                        m.quads = std::move(polys);
                    else {
                        m.triangles.resize(polys.size());
                        for (size_t pi = 0; pi < polys.size(); pi++)
                            m.triangles[pi] = { polys[pi][0], polys[pi][1], polys[pi][2] };
                    }
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
    ModelPostLoadProcess(*this, options);
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
            if (key.starts_with("internal:"))
                continue;
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
            FbxLayerElementNormal *leNormal = nullptr;
            if (obj.vertexFormat & V_Normal) {
                leNormal = fbxMesh->CreateElementNormal();
                leNormal->SetMappingMode(FbxLayerElement::eByPolygonVertex);
                leNormal->SetReferenceMode(FbxLayerElement::eIndexToDirect);
                for (const Vertex &v : obj.vertices)
                    leNormal->GetDirectArray().Add(FbxVector4((double)v.normal.x, (double)v.normal.y, (double)v.normal.z));
            }
            FbxLayerElementTangent *leTangent = nullptr;
            if (obj.vertexFormat & V_Tangent) {
                leTangent = fbxMesh->CreateElementTangent();
                leTangent->SetMappingMode(FbxLayerElement::eByPolygonVertex);
                leTangent->SetReferenceMode(FbxLayerElement::eIndexToDirect);
                for (const Vertex &v : obj.vertices)
                    leTangent->GetDirectArray().Add(FbxVector4((double)v.tangent.x, (double)v.tangent.y, (double)v.tangent.z));
            }
            FbxLayerElementBinormal *leBinormal = nullptr;
            if (obj.vertexFormat & V_Binormal) {
                leBinormal = fbxMesh->CreateElementBinormal();
                leBinormal->SetMappingMode(FbxLayerElement::eByPolygonVertex);
                leBinormal->SetReferenceMode(FbxLayerElement::eIndexToDirect);
                for (const Vertex &v : obj.vertices)
                    leBinormal->GetDirectArray().Add(FbxVector4((double)v.binormal.x, (double)v.binormal.y, (double)v.binormal.z));
            }
            std::vector<FbxLayerElementUV *> uvLayers;
            for (unsigned char uvSet = 0; uvSet < NumTexCoords(obj.vertexFormat); ++uvSet) {
                std::string uvName = (!obj.uvLayerNames[uvSet].empty()) ? obj.uvLayerNames[uvSet] : "UV" + std::to_string(uvSet);
                FbxLayerElementUV *leUV = fbxMesh->CreateElementUV(uvName.c_str());
                leUV->SetMappingMode(FbxLayerElement::eByPolygonVertex);
                leUV->SetReferenceMode(FbxLayerElement::eIndexToDirect);
                for (const Vertex &v : obj.vertices)
                    leUV->GetDirectArray().Add(FbxVector2((double)v.uv[uvSet].x, (double)v.uv[uvSet].y));
                uvLayers.push_back(leUV);
            }
            std::vector<FbxLayerElementVertexColor *> colorLayers;
            for (unsigned char c = 0; c < NumColors(obj.vertexFormat); ++c) {
                FbxLayer *layer = fbxMesh->GetLayer(c);
                while (!layer) {
                    fbxMesh->CreateLayer();
                    layer = fbxMesh->GetLayer(c);
                }
                std::string colName = !obj.colorLayerNames[c].empty() ? obj.colorLayerNames[c] : ("Col" + std::to_string(c));
                FbxLayerElementVertexColor *leVC = FbxLayerElementVertexColor::Create(fbxMesh, colName.c_str());
                leVC->SetMappingMode(FbxGeometryElement::eByPolygonVertex);
                leVC->SetReferenceMode(FbxGeometryElement::eIndexToDirect);
                for (const Vertex &v : obj.vertices)
                    leVC->GetDirectArray().Add(FbxColor((double)v.colors[c].r / 255.0, (double)v.colors[c].g / 255.0,
                        (double)v.colors[c].b / 255.0, (double)v.colors[c].a / 255.0));
                layer->SetVertexColors(leVC);
                colorLayers.push_back(leVC);
            }
            FbxLayerElementMaterial *leMat = fbxMesh->CreateElementMaterial();
            leMat->SetMappingMode(FbxLayerElement::eByPolygon);
            leMat->SetReferenceMode(FbxLayerElement::eIndexToDirect);
            std::map<int, int> globalToLocal;
            int nextLocalIdx = 0;
            for (auto const &mesh : obj.meshes) {
                // triangles
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
                        int vIdx = (int)cornerIdx[corner];
                        if (leNormal)
                            leNormal->GetIndexArray().Add(vIdx);
                        if (leTangent)
                            leTangent->GetIndexArray().Add(vIdx);
                        if (leBinormal)
                            leBinormal->GetIndexArray().Add(vIdx);
                        for (unsigned int uvSet = 0; uvSet < uvLayers.size(); ++uvSet)
                            uvLayers[uvSet]->GetIndexArray().Add(vIdx);
                        for (unsigned int cIdx = 0; cIdx < colorLayers.size(); ++cIdx)
                            colorLayers[cIdx]->GetIndexArray().Add(vIdx);
                    }
                }
                // quads
                for (size_t q = 0; q < mesh.quads.size(); ++q) {
                    uint32_t i0 = mesh.quads[q][0];
                    uint32_t i1 = mesh.quads[q][1];
                    uint32_t i2 = mesh.quads[q][2];
                    uint32_t i3 = mesh.quads[q][3];
                    int numCorners = i2 == i3 ? 3 : 4;
                    fbxMesh->BeginPolygon(-1, -1, false);
                    fbxMesh->AddPolygon((int)i0);
                    fbxMesh->AddPolygon((int)i1);
                    fbxMesh->AddPolygon((int)i2);
                    if (numCorners == 4)
                        fbxMesh->AddPolygon((int)i3);
                    fbxMesh->EndPolygon();
                    const unsigned int cornerIdx[4] = { i0, i1, i2, i3 };
                    for (int corner = 0; corner < numCorners; ++corner) {
                        int vIdx = (int)cornerIdx[corner];
                        if (leNormal)
                            leNormal->GetIndexArray().Add(vIdx);
                        if (leTangent)
                            leTangent->GetIndexArray().Add(vIdx);
                        if (leBinormal)
                            leBinormal->GetIndexArray().Add(vIdx);
                        for (unsigned int uvSet = 0; uvSet < uvLayers.size(); ++uvSet)
                            uvLayers[uvSet]->GetIndexArray().Add(vIdx);
                        for (unsigned int cIdx = 0; cIdx < colorLayers.size(); ++cIdx)
                            colorLayers[cIdx]->GetIndexArray().Add(vIdx);
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
                for (size_t p = 0; p < mesh.quads.size(); ++p)
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
            if (!obj.shapeKeys.empty()) {
                FbxBlendShape *fbxBlendShape = FbxBlendShape::Create(fbxScene, (obj.name + "_blendShape").c_str());
                fbxMesh->AddDeformer(fbxBlendShape);
                int cpCount = (int)obj.vertices.size();
                FbxVector4 const *baseCPs = fbxMesh->GetControlPoints();
                for (ShapeKey const &shapeKey : obj.shapeKeys) {
                    FbxBlendShapeChannel *fbxChannel = FbxBlendShapeChannel::Create(fbxScene, shapeKey.name.c_str());
                    fbxBlendShape->AddBlendShapeChannel(fbxChannel);
                    FbxShape *fbxShape = FbxShape::Create(fbxScene, shapeKey.name.c_str());
                    fbxShape->InitControlPoints(cpCount);
                    FbxVector4 *shapeCPs = fbxShape->GetControlPoints();
                    for (int i = 0; i < cpCount; ++i)
                        shapeCPs[i] = baseCPs[i]; // default: unmodified
                    bool anyNormalDelta = false;
                    for (ShapeKeyVertex const &skv : shapeKey.vertices) {
                        if (skv.vertexIndex >= (uint32_t)cpCount) continue;
                        FbxVector4 base = baseCPs[skv.vertexIndex];
                        shapeCPs[skv.vertexIndex] = FbxVector4(
                            base[0] + skv.deltaPos.x, base[1] + skv.deltaPos.y, base[2] + skv.deltaPos.z, 1.0);
                        if (skv.deltaNormal.x != 0.0f || skv.deltaNormal.y != 0.0f || skv.deltaNormal.z != 0.0f)
                            anyNormalDelta = true;
                    }
                    if (anyNormalDelta && (obj.vertexFormat & V_Normal)) {
                        FbxLayerElementNormal *leShapeNormal = fbxShape->CreateElementNormal();
                        leShapeNormal->SetMappingMode(FbxLayerElement::eByControlPoint);
                        leShapeNormal->SetReferenceMode(FbxLayerElement::eDirect);
                        for (int i = 0; i < cpCount; ++i)
                            leShapeNormal->GetDirectArray().Add(FbxVector4(
                                (double)obj.vertices[i].normal.x, (double)obj.vertices[i].normal.y, (double)obj.vertices[i].normal.z));
                        for (ShapeKeyVertex const &skv : shapeKey.vertices) {
                            if (skv.vertexIndex >= (uint32_t)cpCount) continue;
                            Vector3 base = obj.vertices[skv.vertexIndex].normal;
                            Vector3 sum(base.x + skv.deltaNormal.x, base.y + skv.deltaNormal.y, base.z + skv.deltaNormal.z);
                            leShapeNormal->GetDirectArray().SetAt((int)skv.vertexIndex,
                                FbxVector4((double)sum.x, (double)sum.y, (double)sum.z));
                        }
                    }
                    fbxChannel->AddTargetShape(fbxShape, 100.0);
                }
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
