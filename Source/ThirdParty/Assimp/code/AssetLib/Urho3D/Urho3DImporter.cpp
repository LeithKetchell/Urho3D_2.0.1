/*
Open Asset Import Library (assimp)
----------------------------------------------------------------------

Copyright (c) 2006-2026, assimp team

All rights reserved.

Redistribution and use of this software in source and binary forms,
with or without modification, are permitted provided that the
following conditions are met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the above
  copyright notice, this list of conditions and the
  following disclaimer in the documentation and/or other
  materials provided with the distribution.

* Neither the name of the assimp team, nor the names of its
  contributors may be used to endorse or promote products
  derived from this software without specific prior
  written permission of the assimp team.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

----------------------------------------------------------------------
*/

/** @file  Urho3DImporter.cpp
 *  @brief Implementation of the Urho3D .mdl importer.
 */

#ifndef ASSIMP_BUILD_NO_URHO3D_IMPORTER

#include "Urho3DImporter.h"

#include <assimp/IOSystem.hpp>
#include <assimp/IOStream.hpp>
#include <assimp/scene.h>
#include <assimp/mesh.h>
#include <assimp/material.h>
#include <assimp/importerdesc.h>
#include <assimp/Importer.hpp>

#include <cstring>
#include <memory>
#include <map>

namespace Assimp {

template <>
const char *LogFunctions<Urho3DImporter>::Prefix() {
    return "Urho3D: ";
}

} // namespace Assimp

using namespace Assimp;

namespace {

static constexpr aiImporterDesc desc = {
    "Urho3D Model Importer",
    "",
    "",
    "Phase 1: geometry + skeleton",
    aiImporterFlags_SupportBinaryFlavour,
    0, 0, 0, 0,
    "mdl"
};

// Little-endian readers from raw buffer
inline uint32_t ReadU32(const uint8_t*& p) {
    uint32_t v;
    memcpy(&v, p, 4); p += 4;
    return v;
}
inline int32_t ReadI32(const uint8_t*& p) {
    int32_t v;
    memcpy(&v, p, 4); p += 4;
    return v;
}
inline float ReadFloat(const uint8_t*& p) {
    float v;
    memcpy(&v, p, 4); p += 4;
    return v;
}
inline uint8_t ReadU8(const uint8_t*& p) {
    return *p++;
}
inline uint16_t ReadU16(const uint8_t*& p) {
    uint16_t v;
    memcpy(&v, p, 2); p += 2;
    return v;
}
inline aiVector3D ReadVec3(const uint8_t*& p) {
    float x = ReadFloat(p), y = ReadFloat(p), z = ReadFloat(p);
    return aiVector3D(x, y, z);
}
inline aiQuaternion ReadQuat(const uint8_t*& p) {
    float w = ReadFloat(p), x = ReadFloat(p), y = ReadFloat(p), z = ReadFloat(p);
    return aiQuaternion(w, x, y, z);
}
inline std::string ReadString(const uint8_t*& p) {
    std::string s;
    while (*p != 0) s += (char)*p++;
    ++p; // skip null terminator
    return s;
}

} // anonymous namespace

// ------------------------------------------------------------------------------------------------
void Urho3DImporter::SetupProperties(const Importer* pImp) {
    mReadWeights = pImp->GetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_WEIGHTS, true);
}

// ------------------------------------------------------------------------------------------------
unsigned Urho3DImporter::ElementTypeSize(VEType t) {
    static const unsigned sizes[] = { 4, 4, 8, 12, 16, 4, 4 };
    return (t < sizeof(sizes)/sizeof(sizes[0])) ? sizes[t] : 0;
}

const Urho3DImporter::VertexElement* Urho3DImporter::FindElement(
    const VertexBufferData& vb, VESemantic sem, unsigned char idx) const
{
    for (const auto& e : vb.elements)
        if (e.semantic == sem && e.index == idx) return &e;
    return nullptr;
}

bool Urho3DImporter::CanRead(const std::string& pFile, IOSystem* pIOHandler, bool checkSig) const {
    const std::string ext = GetExtension(pFile);
    if (ext == "mdl") return true;

    if (checkSig && pIOHandler) {
        std::unique_ptr<IOStream> stream(pIOHandler->Open(pFile, "rb"));
        if (stream && stream->FileSize() >= 4) {
            char magic[4];
            stream->Read(magic, 1, 4);
            return (memcmp(magic, "UMDL", 4) == 0 ||
                    memcmp(magic, "UMD2", 4) == 0 ||
                    memcmp(magic, "UMD3", 4) == 0);
        }
    }
    return false;
}

const aiImporterDesc* Urho3DImporter::GetInfo() const {
    return &desc;
}

void Urho3DImporter::InternReadFile(const std::string& pFile, aiScene* pScene, IOSystem* pIOHandler) {
    auto streamCloser = [&](IOStream* pStream) { pIOHandler->Close(pStream); };
    std::unique_ptr<IOStream, decltype(streamCloser)> stream(pIOHandler->Open(pFile, "rb"), streamCloser);
    if (!stream)
        ThrowException("Could not open file for reading");

    const size_t fileSize = stream->FileSize();
    std::vector<uint8_t> buf(fileSize);
    stream->Read(buf.data(), 1, fileSize);

    const uint8_t* ptr = buf.data();
    const uint8_t* end = ptr + fileSize;

    // --- Magic ---
    char magic[5] = {};
    memcpy(magic, ptr, 4); ptr += 4;
    bool hasVertexDecl = (memcmp(magic, "UMD2", 4) == 0 || memcmp(magic, "UMD3", 4) == 0);
    bool hasBBoxHeader = (memcmp(magic, "UMD3", 4) == 0);

    if (memcmp(magic, "UMDL", 4) != 0 && !hasVertexDecl)
        throw DeadlyImportError("Not a valid Urho3D model file");

    // --- BBox header (UMD3) ---
    if (hasBBoxHeader)
        ptr += 24; // skip BoundingBox (min Vec3 + max Vec3)

    // --- Vertex Buffers ---
    uint32_t numVB = ReadU32(ptr);
    std::vector<VertexBufferData> vertexBuffers(numVB);

    for (uint32_t i = 0; i < numVB; ++i) {
        VertexBufferData& vb = vertexBuffers[i];
        vb.vertexCount = ReadU32(ptr);

        if (!hasVertexDecl) {
            // Legacy element mask
            uint32_t mask = ReadU32(ptr);
            // Reconstruct elements from legacy bitmask
            // Bit 0=Position(Vec3), 1=Normal(Vec3), 2=Color(UByte4Norm),
            // 3=TexCoord1(Vec2), 4=TexCoord2(Vec2), 5=CubeTexCoord1(Vec3),
            // 6=CubeTexCoord2(Vec3), 7=Tangent(Vec4), 8=BlendWeights(Vec4),
            // 9=BlendIndices(UByte4), 10=InstanceMatrix1-3, 13=ObjectIndex
            struct LegacyElem { unsigned bit; VEType type; VESemantic sem; unsigned char idx; };
            static const LegacyElem legacy[] = {
                {0, VE_VECTOR3, VES_POSITION, 0},
                {1, VE_VECTOR3, VES_NORMAL, 0},
                {2, VE_UBYTE4_NORM, VES_COLOR, 0},
                {3, VE_VECTOR2, VES_TEXCOORD, 0},
                {4, VE_VECTOR2, VES_TEXCOORD, 1},
                {5, VE_VECTOR3, VES_TEXCOORD, 0},  // cube texcoord overwrites 2D
                {6, VE_VECTOR3, VES_TEXCOORD, 1},
                {7, VE_VECTOR4, VES_TANGENT, 0},
                {8, VE_VECTOR4, VES_BLENDWEIGHTS, 0},
                {9, VE_UBYTE4, VES_BLENDINDICES, 0},
            };
            unsigned offset = 0;
            for (const auto& le : legacy) {
                if (mask & (1u << le.bit)) {
                    VertexElement e;
                    e.type = le.type; e.semantic = le.sem; e.index = le.idx;
                    e.offset = offset;
                    offset += ElementTypeSize(le.type);
                    vb.elements.push_back(e);
                }
            }
            vb.vertexSize = offset;
        } else {
            uint32_t numElems = ReadU32(ptr);
            unsigned offset = 0;
            for (uint32_t j = 0; j < numElems; ++j) {
                uint32_t packed = ReadU32(ptr);
                VertexElement e;
                e.type = (VEType)(packed & 0xFF);
                e.semantic = (VESemantic)((packed >> 8) & 0xFF);
                e.index = (unsigned char)((packed >> 16) & 0xFF);
                e.offset = offset;
                offset += ElementTypeSize(e.type);
                vb.elements.push_back(e);
            }
            vb.vertexSize = offset;
        }

        ptr += 4; // morphRangeStart
        ptr += 4; // morphRangeCount

        size_t dataSize = (size_t)vb.vertexCount * vb.vertexSize;
        vb.data.resize(dataSize);
        memcpy(vb.data.data(), ptr, dataSize);
        ptr += dataSize;
    }

    // --- Index Buffers ---
    uint32_t numIB = ReadU32(ptr);
    std::vector<IndexBufferData> indexBuffers(numIB);

    for (uint32_t i = 0; i < numIB; ++i) {
        IndexBufferData& ib = indexBuffers[i];
        ib.indexCount = ReadU32(ptr);
        ib.indexSize = ReadU32(ptr);
        size_t dataSize = (size_t)ib.indexCount * ib.indexSize;
        ib.data.resize(dataSize);
        memcpy(ib.data.data(), ptr, dataSize);
        ptr += dataSize;
    }

    // --- Geometries ---
    uint32_t numGeoms = ReadU32(ptr);
    std::vector<GeometryData> geometries(numGeoms);

    for (uint32_t i = 0; i < numGeoms; ++i) {
        GeometryData& gd = geometries[i];
        uint32_t boneMappingCount = ReadU32(ptr);
        gd.boneMapping.resize(boneMappingCount);
        for (uint32_t j = 0; j < boneMappingCount; ++j)
            gd.boneMapping[j] = ReadI32(ptr);

        uint32_t numLods = ReadU32(ptr);
        gd.lods.resize(numLods);
        for (uint32_t j = 0; j < numLods; ++j) {
            GeomLOD& lod = gd.lods[j];
            lod.distance = ReadFloat(ptr);
            lod.primitiveType = ReadU32(ptr);
            lod.vbRef = ReadU32(ptr);
            lod.ibRef = ReadU32(ptr);
            lod.indexStart = ReadU32(ptr);
            lod.indexCount = ReadU32(ptr);
        }
    }

    // --- Morphs (skip Phase 1) ---
    if (ptr < end) {
        uint32_t numMorphs = ReadU32(ptr);
        for (uint32_t i = 0; i < numMorphs; ++i) {
            ReadString(ptr); // name
            uint32_t numBufs = ReadU32(ptr);
            for (uint32_t j = 0; j < numBufs; ++j) {
                ptr += 4; // bufferIndex
                uint32_t elemMask = ReadU32(ptr);
                uint32_t morphVertCount = ReadU32(ptr);
                unsigned morphVertSize = 4; // vertex index
                if (elemMask & 1) morphVertSize += 12; // position
                if (elemMask & 2) morphVertSize += 12; // normal
                if (elemMask & 128) morphVertSize += 12; // tangent (bit 7)
                ptr += (size_t)morphVertCount * morphVertSize;
            }
        }
        if (numMorphs > 0)
            LogWarn("Skipped ", numMorphs, " morph targets (Phase 1)");
    }

    // --- Skeleton ---
    std::vector<BoneData> bones;
    if (ptr < end) {
        int32_t numBones = ReadI32(ptr);
        bones.resize(numBones);
        for (int32_t i = 0; i < numBones; ++i) {
            BoneData& b = bones[i];
            b.name = ReadString(ptr);
            b.parentIndex = ReadI32(ptr);
            b.position = ReadVec3(ptr);
            b.rotation = ReadQuat(ptr);
            b.scale = ReadVec3(ptr);
            memcpy(b.offsetMatrix, ptr, 48); ptr += 48; // Matrix3x4

            uint8_t collisionMask = ReadU8(ptr);
            if (collisionMask & 1) ptr += 4; // sphere radius
            if (collisionMask & 2) ptr += 24; // bounding box
        }
    }

    // --- BBox (non-UMD3) ---
    // Skip — we don't need it for aiScene

    // =========================================================================
    // Build aiScene
    // =========================================================================

    // Count LOD0 geometries only
    unsigned meshCount = 0;
    for (const auto& gd : geometries)
        if (!gd.lods.empty()) ++meshCount;

    pScene->mNumMeshes = meshCount;
    pScene->mMeshes = new aiMesh*[meshCount];

    pScene->mNumMaterials = meshCount;
    pScene->mMaterials = new aiMaterial*[meshCount];

    // Build bone node hierarchy
    aiNode* rootNode = new aiNode("RootNode");
    pScene->mRootNode = rootNode;

    std::vector<aiNode*> boneNodes(bones.size(), nullptr);
    if (!bones.empty()) {
        // Create all bone nodes first
        for (size_t i = 0; i < bones.size(); ++i) {
            aiNode* bn = new aiNode(bones[i].name);

            // Local transform from initial pos/rot/scale
            aiMatrix4x4 trans, rot, scl;
            aiMatrix4x4::Translation(bones[i].position, trans);
            rot = aiMatrix4x4(bones[i].rotation.GetMatrix());
            aiMatrix4x4::Scaling(bones[i].scale, scl);
            bn->mTransformation = trans * rot * scl;

            boneNodes[i] = bn;
        }

        // Parent linkage
        std::vector<std::vector<unsigned>> childLists(bones.size());
        std::vector<unsigned> rootBones;
        for (size_t i = 0; i < bones.size(); ++i) {
            if (bones[i].parentIndex == (int32_t)i || bones[i].parentIndex < 0)
                rootBones.push_back((unsigned)i);
            else
                childLists[bones[i].parentIndex].push_back((unsigned)i);
        }

        for (size_t i = 0; i < bones.size(); ++i) {
            if (!childLists[i].empty()) {
                boneNodes[i]->mNumChildren = (unsigned)childLists[i].size();
                boneNodes[i]->mChildren = new aiNode*[childLists[i].size()];
                for (size_t c = 0; c < childLists[i].size(); ++c) {
                    boneNodes[i]->mChildren[c] = boneNodes[childLists[i][c]];
                    boneNodes[childLists[i][c]]->mParent = boneNodes[i];
                }
            }
        }

        // Attach root bones + mesh node to scene root
        rootNode->mNumChildren = (unsigned)rootBones.size();
        rootNode->mChildren = new aiNode*[rootBones.size()];
        for (size_t i = 0; i < rootBones.size(); ++i) {
            rootNode->mChildren[i] = boneNodes[rootBones[i]];
            boneNodes[rootBones[i]]->mParent = rootNode;
        }
    }

    // Mesh references on root node
    rootNode->mNumMeshes = meshCount;
    rootNode->mMeshes = new unsigned[meshCount];
    for (unsigned i = 0; i < meshCount; ++i)
        rootNode->mMeshes[i] = i;

    // Create meshes from LOD0
    unsigned meshIdx = 0;
    for (unsigned gi = 0; gi < numGeoms; ++gi) {
        const GeometryData& gd = geometries[gi];
        if (gd.lods.empty()) continue;

        const GeomLOD& lod = gd.lods[0]; // LOD0 only
        if (lod.vbRef >= vertexBuffers.size() || lod.ibRef >= indexBuffers.size())
            throw DeadlyImportError("Geometry references out-of-bounds buffer");

        const VertexBufferData& vb = vertexBuffers[lod.vbRef];
        const IndexBufferData& ib = indexBuffers[lod.ibRef];

        aiMesh* mesh = new aiMesh();
        pScene->mMeshes[meshIdx] = mesh;
        mesh->mMaterialIndex = meshIdx;
        mesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;

        // Gather unique vertex indices from this geometry's index range
        std::vector<uint32_t> indices(lod.indexCount);
        const uint8_t* ibData = ib.data.data() + (size_t)lod.indexStart * ib.indexSize;
        for (uint32_t i = 0; i < lod.indexCount; ++i) {
            if (ib.indexSize == 2)
                indices[i] = ((const uint16_t*)ibData)[i];
            else
                indices[i] = ((const uint32_t*)ibData)[i];
        }

        // Build unique vertex list and remap
        std::map<uint32_t, uint32_t> vertRemap;
        std::vector<uint32_t> uniqueVerts;
        for (uint32_t idx : indices) {
            if (vertRemap.find(idx) == vertRemap.end()) {
                vertRemap[idx] = (uint32_t)uniqueVerts.size();
                uniqueVerts.push_back(idx);
            }
        }

        uint32_t numVerts = (uint32_t)uniqueVerts.size();
        mesh->mNumVertices = numVerts;

        // Find element offsets
        const VertexElement* posElem = FindElement(vb, VES_POSITION);
        const VertexElement* normElem = FindElement(vb, VES_NORMAL);
        const VertexElement* uvElem = FindElement(vb, VES_TEXCOORD, 0);
        const VertexElement* tanElem = FindElement(vb, VES_TANGENT);
        const VertexElement* colorElem = FindElement(vb, VES_COLOR);
        const VertexElement* bwElem = FindElement(vb, VES_BLENDWEIGHTS);
        const VertexElement* biElem = FindElement(vb, VES_BLENDINDICES);

        if (posElem) {
            mesh->mVertices = new aiVector3D[numVerts];
            for (uint32_t i = 0; i < numVerts; ++i) {
                const uint8_t* v = vb.data.data() + (size_t)uniqueVerts[i] * vb.vertexSize + posElem->offset;
                float x, y, z;
                memcpy(&x, v, 4); memcpy(&y, v+4, 4); memcpy(&z, v+8, 4);
                mesh->mVertices[i] = aiVector3D(x, y, z);
            }
        }

        if (normElem) {
            mesh->mNormals = new aiVector3D[numVerts];
            for (uint32_t i = 0; i < numVerts; ++i) {
                const uint8_t* v = vb.data.data() + (size_t)uniqueVerts[i] * vb.vertexSize + normElem->offset;
                float x, y, z;
                memcpy(&x, v, 4); memcpy(&y, v+4, 4); memcpy(&z, v+8, 4);
                mesh->mNormals[i] = aiVector3D(x, y, z);
            }
        }

        if (uvElem) {
            mesh->mTextureCoords[0] = new aiVector3D[numVerts];
            mesh->mNumUVComponents[0] = 2;
            for (uint32_t i = 0; i < numVerts; ++i) {
                const uint8_t* v = vb.data.data() + (size_t)uniqueVerts[i] * vb.vertexSize + uvElem->offset;
                float u, vv;
                memcpy(&u, v, 4); memcpy(&vv, v+4, 4);
                mesh->mTextureCoords[0][i] = aiVector3D(u, vv, 0.0f);
            }
        }

        if (tanElem) {
            mesh->mTangents = new aiVector3D[numVerts];
            mesh->mBitangents = new aiVector3D[numVerts];
            for (uint32_t i = 0; i < numVerts; ++i) {
                const uint8_t* v = vb.data.data() + (size_t)uniqueVerts[i] * vb.vertexSize + tanElem->offset;
                float tx, ty, tz, tw;
                memcpy(&tx, v, 4); memcpy(&ty, v+4, 4); memcpy(&tz, v+8, 4); memcpy(&tw, v+12, 4);
                mesh->mTangents[i] = aiVector3D(tx, ty, tz);
                // Bitangent = cross(normal, tangent) * w
                if (normElem) {
                    aiVector3D n = mesh->mNormals[i];
                    aiVector3D t(tx, ty, tz);
                    mesh->mBitangents[i] = (n ^ t) * tw;
                }
            }
        }

        if (colorElem) {
            mesh->mColors[0] = new aiColor4D[numVerts];
            for (uint32_t i = 0; i < numVerts; ++i) {
                const uint8_t* v = vb.data.data() + (size_t)uniqueVerts[i] * vb.vertexSize + colorElem->offset;
                mesh->mColors[0][i] = aiColor4D(v[0]/255.f, v[1]/255.f, v[2]/255.f, v[3]/255.f);
            }
        }

        // Faces (triangles)
        mesh->mNumFaces = lod.indexCount / 3;
        mesh->mFaces = new aiFace[mesh->mNumFaces];
        for (uint32_t i = 0; i < mesh->mNumFaces; ++i) {
            aiFace& f = mesh->mFaces[i];
            f.mNumIndices = 3;
            f.mIndices = new unsigned[3];
            f.mIndices[0] = vertRemap[indices[i*3+0]];
            f.mIndices[1] = vertRemap[indices[i*3+1]];
            f.mIndices[2] = vertRemap[indices[i*3+2]];
        }

        // Bones (skinning) — respects AI_CONFIG_IMPORT_FBX_READ_WEIGHTS
        if (mReadWeights && bwElem && biElem && !bones.empty()) {
            // Collect per-bone weight lists
            std::map<uint32_t, std::vector<aiVertexWeight>> boneWeights;
            for (uint32_t i = 0; i < numVerts; ++i) {
                const uint8_t* wPtr = vb.data.data() + (size_t)uniqueVerts[i] * vb.vertexSize + bwElem->offset;
                const uint8_t* iPtr = vb.data.data() + (size_t)uniqueVerts[i] * vb.vertexSize + biElem->offset;
                float weights[4];
                memcpy(weights, wPtr, 16);
                uint8_t boneIdx[4];
                memcpy(boneIdx, iPtr, 4);

                for (int b = 0; b < 4; ++b) {
                    if (weights[b] <= 0.0f) continue;
                    // Map local bone index through geometry's bone mapping
                    uint32_t globalBone = boneIdx[b];
                    if (!gd.boneMapping.empty() && boneIdx[b] < gd.boneMapping.size())
                        globalBone = (uint32_t)gd.boneMapping[boneIdx[b]];
                    if (globalBone < bones.size())
                        boneWeights[globalBone].push_back(aiVertexWeight(i, weights[b]));
                }
            }

            mesh->mNumBones = (unsigned)boneWeights.size();
            mesh->mBones = new aiBone*[mesh->mNumBones];
            unsigned boneIdx2 = 0;
            for (auto& bw : boneWeights) {
                aiBone* aib = new aiBone();
                mesh->mBones[boneIdx2++] = aib;
                aib->mName = aiString(bones[bw.first].name);

                // Build offset matrix from stored Matrix3x4
                const float* m = bones[bw.first].offsetMatrix;
                aib->mOffsetMatrix = aiMatrix4x4(
                    m[0], m[1], m[2], m[3],
                    m[4], m[5], m[6], m[7],
                    m[8], m[9], m[10], m[11],
                    0.f, 0.f, 0.f, 1.f
                );

                aib->mNumWeights = (unsigned)bw.second.size();
                aib->mWeights = new aiVertexWeight[aib->mNumWeights];
                memcpy(aib->mWeights, bw.second.data(), sizeof(aiVertexWeight) * aib->mNumWeights);
            }
        }

        // Default material
        aiMaterial* mat = new aiMaterial();
        pScene->mMaterials[meshIdx] = mat;
        aiString matName("Urho3D_Material_" + std::to_string(gi));
        mat->AddProperty(&matName, AI_MATKEY_NAME);

        ++meshIdx;
    }

    LogInfo("Loaded ", meshCount, " meshes, ", bones.size(), " bones from ", pFile);
}

#endif // !ASSIMP_BUILD_NO_URHO3D_IMPORTER
