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

/** @file  Urho3DImporter.h
 *  @brief Declaration of the Urho3D .mdl importer class.
 */
#pragma once
#ifndef AI_URHO3DIMPORTER_H_INC
#define AI_URHO3DIMPORTER_H_INC

#include <assimp/BaseImporter.h>
#include <assimp/LogAux.h>
#include <assimp/types.h>

#include <vector>
#include <string>
#include <cstdint>

namespace Assimp {

// -------------------------------------------------------------------------------------------
/// Loads the Urho3D .mdl binary model format (UMDL / UMD2 / UMD3).
// -------------------------------------------------------------------------------------------
class Urho3DImporter final : public BaseImporter, public LogFunctions<Urho3DImporter> {
public:
    Urho3DImporter() = default;
    ~Urho3DImporter() override = default;

    bool CanRead(const std::string& pFile, IOSystem* pIOHandler, bool checkSig) const override;

protected:
    const aiImporterDesc* GetInfo() const override;
    void SetupProperties(const Importer* pImp) override;
    void InternReadFile(const std::string& pFile, aiScene* pScene, IOSystem* pIOHandler) override;

private:
    // Urho3D vertex element types (matches GraphicsDefs.h)
    enum VEType : uint8_t { VE_INT=0, VE_FLOAT, VE_VECTOR2, VE_VECTOR3, VE_VECTOR4, VE_UBYTE4, VE_UBYTE4_NORM };
    enum VESemantic : uint8_t { VES_POSITION=0, VES_NORMAL, VES_BINORMAL, VES_TANGENT, VES_TEXCOORD,
                                VES_COLOR, VES_BLENDWEIGHTS, VES_BLENDINDICES, VES_OBJECTINDEX };

    struct VertexElement {
        VEType type;
        VESemantic semantic;
        unsigned char index;
        unsigned offset;
    };

    struct VertexBufferData {
        uint32_t vertexCount;
        uint32_t vertexSize;
        std::vector<VertexElement> elements;
        std::vector<uint8_t> data;
    };

    struct IndexBufferData {
        uint32_t indexCount;
        uint32_t indexSize;
        std::vector<uint8_t> data;
    };

    struct GeomLOD {
        float distance;
        uint32_t primitiveType;
        uint32_t vbRef, ibRef;
        uint32_t indexStart, indexCount;
    };

    struct GeometryData {
        std::vector<int32_t> boneMapping;
        std::vector<GeomLOD> lods;
    };

    struct BoneData {
        std::string name;
        int32_t parentIndex;
        aiVector3D position;
        aiQuaternion rotation;
        aiVector3D scale;
        float offsetMatrix[12]; // Matrix3x4 row-major
    };

    static unsigned ElementTypeSize(VEType t);
    const VertexElement* FindElement(const VertexBufferData& vb, VESemantic sem, unsigned char idx = 0) const;

    bool mReadWeights = true;
};

} // namespace Assimp

#endif // AI_URHO3DIMPORTER_H_INC
