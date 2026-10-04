// DO NOT EDIT. This file is generated

#include "../Precompiled.h"
#include "../AngelScript/APITemplates.h"

#include "../AngelScript/Generated_Includes.h"

namespace Urho3D
{

void ASRegisterGeneratedGlobalFunctions(asIScriptEngine* engine)
{
    // BigInt Abs(const BigInt& value) | File: ../Math/BigInt.h
    // Error: type "constBigInt&" can not automatically bind

    // template <class T> T Abs(T value) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Abs(float)", AS_FUNCTIONPR(Abs, (float), float), AS_CALL_CDECL);

    // template <class T> T Acos(T x) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Acos(float)", AS_FUNCTIONPR(Acos, (float), float), AS_CALL_CDECL);

    // String AddTrailingSlash(const String& pathName) | File: ../IO/FileSystem.h
    // Error: type "constString&" can not automatically bind

    // template <class T> T Asin(T x) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Asin(float)", AS_FUNCTIONPR(Asin, (float), float), AS_CALL_CDECL);

    // template <class T> T Atan(T x) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Atan(float)", AS_FUNCTIONPR(Atan, (float), float), AS_CALL_CDECL);

    // template <class T> T Atan2(T y, T x) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Atan2(float, float)", AS_FUNCTIONPR(Atan2, (float, float), float), AS_CALL_CDECL);

    // void BufferToString(String& dest, const void* data, unsigned size) | File: ../Core/StringUtils.h
    // Error: type "constvoid*" can not automatically bind

    // template <class T> T Ceil(T x) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Ceil(float)", AS_FUNCTIONPR(Ceil, (float), float), AS_CALL_CDECL);

    // template <class T> int CeilToInt(T x) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("int CeilToInt(float)", AS_FUNCTIONPR(CeilToInt, (float), int), AS_CALL_CDECL);

    // template <class T> T Clamp(T value, T min, T max) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Clamp(float, float, float)", AS_FUNCTIONPR(Clamp, (float, float, float), float), AS_CALL_CDECL);
    engine->RegisterGlobalFunction("int Clamp(int, int, int)", AS_FUNCTIONPR(Clamp, (int, int, int), int), AS_CALL_CDECL);

    // unsigned ClosestPowerOfTwo(unsigned value) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("uint ClosestPowerOfTwo(uint)", AS_FUNCTIONPR(ClosestPowerOfTwo, (unsigned), unsigned), AS_CALL_CDECL);

    // void CombineHash(hash32& result, hash32 hash) | File: ../Container/Hash.h
    engine->RegisterGlobalFunction("void CombineHash(hash32&, hash32)", AS_FUNCTIONPR(CombineHash, (hash32&, hash32), void), AS_CALL_CDECL);

    // bool CompareDrawables(Drawable* lhs, Drawable* rhs) | File: ../Graphics/Drawable.h
    engine->RegisterGlobalFunction("bool CompareDrawables(Drawable@+, Drawable@+)", AS_FUNCTIONPR(CompareDrawables, (Drawable*, Drawable*), bool), AS_CALL_CDECL);

    // bool CompareLights(Light* lhs, Light* rhs) | File: ../Graphics/Light.h
    engine->RegisterGlobalFunction("bool CompareLights(Light@+, Light@+)", AS_FUNCTIONPR(CompareLights, (Light*, Light*), bool), AS_CALL_CDECL);

    // unsigned CompressData(void* dest, const void* src, unsigned srcSize) | File: ../IO/Compression.h
    // Error: type "void*" can not automatically bind

    // bool CompressStream(Serializer& dest, Deserializer& src) | File: ../IO/Compression.h
    engine->RegisterGlobalFunction("bool CompressStream(Serializer&, Deserializer&)", AS_FUNCTIONPR(CompressStream, (Serializer&, Deserializer&), bool), AS_CALL_CDECL);

    // VectorBuffer CompressVectorBuffer(VectorBuffer& src) | File: ../IO/Compression.h
    engine->RegisterGlobalFunction("VectorBuffer CompressVectorBuffer(VectorBuffer&)", AS_FUNCTIONPR(CompressVectorBuffer, (VectorBuffer&), VectorBuffer), AS_CALL_CDECL);

    // template <class T> T Cos(T angle) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Cos(float)", AS_FUNCTIONPR(Cos, (float), float), AS_CALL_CDECL);

    // i32 CountSetBits(u32 value) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("int CountSetBits(uint)", AS_FUNCTIONPR(CountSetBits, (u32), i32), AS_CALL_CDECL);

    // Vector<unsigned char> DecodeBase64(const String& encodedString) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // unsigned DecompressData(void* dest, const void* src, unsigned destSize) | File: ../IO/Compression.h
    // Error: type "void*" can not automatically bind

    // void DecompressImageDXT(unsigned char* rgba, const void* blocks, int width, int height, int depth, CompressedFormat format) | File: ../Resource/Decompress.h
    // Error: type "unsignedchar*" can not automatically bind

    // void DecompressImageETC(unsigned char* dstImage, const void* blocks, int width, int height, bool hasAlpha) | File: ../Resource/Decompress.h
    // Error: type "unsignedchar*" can not automatically bind

    // void DecompressImagePVRTC(unsigned char* rgba, const void* blocks, int width, int height, CompressedFormat format) | File: ../Resource/Decompress.h
    // Error: type "unsignedchar*" can not automatically bind

    // bool DecompressStream(Serializer& dest, Deserializer& src) | File: ../IO/Compression.h
    engine->RegisterGlobalFunction("bool DecompressStream(Serializer&, Deserializer&)", AS_FUNCTIONPR(DecompressStream, (Serializer&, Deserializer&), bool), AS_CALL_CDECL);

    // VectorBuffer DecompressVectorBuffer(VectorBuffer& src) | File: ../IO/Compression.h
    engine->RegisterGlobalFunction("VectorBuffer DecompressVectorBuffer(VectorBuffer&)", AS_FUNCTIONPR(DecompressVectorBuffer, (VectorBuffer&), VectorBuffer), AS_CALL_CDECL);

    // template <class T> bool Equals(T lhs, T rhs) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("bool Equals(float, float)", AS_FUNCTIONPR(Equals, (float, float), bool), AS_CALL_CDECL);

    // void ErrorDialog(const String& title, const String& message) | File: ../Core/ProcessUtils.h
    // Error: type "constString&" can not automatically bind

    // void ErrorExit(const String& message = String::EMPTY, int exitCode = EXIT_FAILURE) | File: ../Core/ProcessUtils.h
    // Error: type "constString&" can not automatically bind

    // unsigned EstimateCompressBound(unsigned srcSize) | File: ../IO/Compression.h
    engine->RegisterGlobalFunction("uint EstimateCompressBound(uint)", AS_FUNCTIONPR(EstimateCompressBound, (unsigned), unsigned), AS_CALL_CDECL);

    // void FlipBlockHorizontal(unsigned char* dest, const unsigned char* src, CompressedFormat format) | File: ../Resource/Decompress.h
    // Error: type "unsignedchar*" can not automatically bind

    // void FlipBlockVertical(unsigned char* dest, const unsigned char* src, CompressedFormat format) | File: ../Resource/Decompress.h
    // Error: type "unsignedchar*" can not automatically bind

    // unsigned short FloatToHalf(float value) | File: ../Math/MathDefs.h
    // Error: type "unsignedshort" can not automatically bind

    // unsigned FloatToRawIntBits(float value) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("uint FloatToRawIntBits(float)", AS_FUNCTIONPR(FloatToRawIntBits, (float), unsigned), AS_CALL_CDECL);

    // template <class T> T Floor(T x) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Floor(float)", AS_FUNCTIONPR(Floor, (float), float), AS_CALL_CDECL);

    // template <class T> int FloorToInt(T x) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("int FloorToInt(float)", AS_FUNCTIONPR(FloorToInt, (float), int), AS_CALL_CDECL);

    // template <class T> T Fract(T value) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Fract(float)", AS_FUNCTIONPR(Fract, (float), float), AS_CALL_CDECL);

    // void GenerateTangents(void* vertexData, unsigned vertexSize, const void* indexData, unsigned indexSize, unsigned indexStart, unsigned indexCount, unsigned normalOffset, unsigned texCoordOffset, unsigned tangentOffset) | File: ../Graphics/Tangent.h
    // Error: type "void*" can not automatically bind

    // const Vector<String>& GetArguments() | File: ../Core/ProcessUtils.h
    // Error: type "constVector<String>&" can not automatically bind

    // const char* GetCompilerDefines() | File: ../LibraryInfo.h
    // Error: type "constchar*" can not automatically bind

    // String GetConsoleInput() | File: ../Core/ProcessUtils.h
    engine->RegisterGlobalFunction("String GetConsoleInput()", AS_FUNCTIONPR(GetConsoleInput, (), String), AS_CALL_CDECL);

    // StringHashRegister& GetEventNameRegister() | File: ../Core/Object.h
    engine->RegisterGlobalFunction("StringHashRegister& GetEventNameRegister()", AS_FUNCTIONPR(GetEventNameRegister, (), StringHashRegister&), AS_CALL_CDECL);

    // String GetExtension(const String& fullPath, bool lowercaseExtension = true) | File: ../IO/FileSystem.h
    // Error: type "constString&" can not automatically bind

    // String GetFileName(const String& fullPath) | File: ../IO/FileSystem.h
    // Error: type "constString&" can not automatically bind

    // String GetFileNameAndExtension(const String& fileName, bool lowercaseExtension = false) | File: ../IO/FileSystem.h
    // Error: type "constString&" can not automatically bind

    // String GetFileSizeString(unsigned long long memorySize) | File: ../Core/StringUtils.h
    // Error: type "unsignedlonglong" can not automatically bind

    // String GetHostName() | File: ../Core/ProcessUtils.h
    engine->RegisterGlobalFunction("String GetHostName()", AS_FUNCTIONPR(GetHostName, (), String), AS_CALL_CDECL);

    // String GetInternalPath(const String& pathName) | File: ../IO/FileSystem.h
    // Error: type "constString&" can not automatically bind

    // String GetLoginName() | File: ../Core/ProcessUtils.h
    engine->RegisterGlobalFunction("String GetLoginName()", AS_FUNCTIONPR(GetLoginName, (), String), AS_CALL_CDECL);

    // String GetMiniDumpDir() | File: ../Core/ProcessUtils.h
    engine->RegisterGlobalFunction("String GetMiniDumpDir()", AS_FUNCTIONPR(GetMiniDumpDir, (), String), AS_CALL_CDECL);

    // String GetNativePath(const String& pathName) | File: ../IO/FileSystem.h
    // Error: type "constString&" can not automatically bind

    // unsigned GetNumLogicalCPUs() | File: ../Core/ProcessUtils.h
    engine->RegisterGlobalFunction("uint GetNumLogicalCPUs()", AS_FUNCTIONPR(GetNumLogicalCPUs, (), unsigned), AS_CALL_CDECL);

    // unsigned GetNumPhysicalCPUs() | File: ../Core/ProcessUtils.h
    engine->RegisterGlobalFunction("uint GetNumPhysicalCPUs()", AS_FUNCTIONPR(GetNumPhysicalCPUs, (), unsigned), AS_CALL_CDECL);

    // String GetOSVersion() | File: ../Core/ProcessUtils.h
    engine->RegisterGlobalFunction("String GetOSVersion()", AS_FUNCTIONPR(GetOSVersion, (), String), AS_CALL_CDECL);

    // String GetParentPath(const String& path) | File: ../IO/FileSystem.h
    // Error: type "constString&" can not automatically bind

    // String GetPath(const String& fullPath) | File: ../IO/FileSystem.h
    // Error: type "constString&" can not automatically bind

    // String GetPlatform() | File: ../Core/ProcessUtils.h
    engine->RegisterGlobalFunction("String GetPlatform()", AS_FUNCTIONPR(GetPlatform, (), String), AS_CALL_CDECL);

    // unsigned GetRandomSeed() | File: ../Math/Random.h
    engine->RegisterGlobalFunction("uint GetRandomSeed()", AS_FUNCTIONPR(GetRandomSeed, (), unsigned), AS_CALL_CDECL);

    // const String& GetResourceName(Resource* resource) | File: ../Resource/Resource.h
    // Error: type "constString&" can not automatically bind

    // ResourceRef GetResourceRef(Resource* resource, StringHash defaultType) | File: ../Resource/Resource.h
    engine->RegisterGlobalFunction("ResourceRef GetResourceRef(Resource@+, StringHash)", AS_FUNCTIONPR(GetResourceRef, (Resource*, StringHash), ResourceRef), AS_CALL_CDECL);

    // StringHash GetResourceType(Resource* resource, StringHash defaultType) | File: ../Resource/Resource.h
    engine->RegisterGlobalFunction("StringHash GetResourceType(Resource@+, StringHash)", AS_FUNCTIONPR(GetResourceType, (Resource*, StringHash), StringHash), AS_CALL_CDECL);

    // const char* GetRevision() | File: ../LibraryInfo.h
    // Error: type "constchar*" can not automatically bind

    // i32 GetStringListIndex(const String& value, const String* strings, i32 defaultIndex, bool caseSensitive = false) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // i32 GetStringListIndex(const char* value, const String* strings, i32 defaultIndex, bool caseSensitive = false) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // i32 GetStringListIndex(const char* value, const char** strings, i32 defaultIndex, bool caseSensitive = false) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // unsigned long long GetTotalMemory() | File: ../Core/ProcessUtils.h
    // Error: type "unsignedlonglong" can not automatically bind

    // WString GetWideNativePath(const String& pathName) | File: ../IO/FileSystem.h
    // Error: type "constString&" can not automatically bind

    // float HalfToFloat(unsigned short value) | File: ../Math/MathDefs.h
    // Error: type "unsignedshort" can not automatically bind

    // void InitFPU() | File: ../Core/ProcessUtils.h
    engine->RegisterGlobalFunction("void InitFPU()", AS_FUNCTIONPR(InitFPU, (), void), AS_CALL_CDECL);

    // template <class T> T InverseLerp(T lhs, T rhs, T x) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float InverseLerp(float, float, float)", AS_FUNCTIONPR(InverseLerp, (float, float, float), float), AS_CALL_CDECL);

    // bool IsAbsolutePath(const String& pathName) | File: ../IO/FileSystem.h
    // Error: type "constString&" can not automatically bind

    // bool IsAlpha(unsigned ch) | File: ../Core/StringUtils.h
    engine->RegisterGlobalFunction("bool IsAlpha(uint)", AS_FUNCTIONPR(IsAlpha, (unsigned), bool), AS_CALL_CDECL);

    // bool IsDigit(unsigned ch) | File: ../Core/StringUtils.h
    engine->RegisterGlobalFunction("bool IsDigit(uint)", AS_FUNCTIONPR(IsDigit, (unsigned), bool), AS_CALL_CDECL);

    // template <class T> bool IsNaN(T value) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("bool IsNaN(double)", AS_FUNCTIONPR(IsNaN, (double), bool), AS_CALL_CDECL);
    engine->RegisterGlobalFunction("bool IsNaN(float)", AS_FUNCTIONPR(IsNaN, (float), bool), AS_CALL_CDECL);

    // bool IsPowerOfTwo(unsigned value) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("bool IsPowerOfTwo(uint)", AS_FUNCTIONPR(IsPowerOfTwo, (unsigned), bool), AS_CALL_CDECL);

    // template <class T, class U> T Lerp(T lhs, T rhs, U t) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Lerp(float, float, float)", AS_FUNCTIONPR(Lerp, (float, float, float), float), AS_CALL_CDECL);

    // template <class T> T Ln(T x) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Ln(float)", AS_FUNCTIONPR(Ln, (float), float), AS_CALL_CDECL);

    // unsigned LogBaseTwo(unsigned value) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("uint LogBaseTwo(uint)", AS_FUNCTIONPR(LogBaseTwo, (unsigned), unsigned), AS_CALL_CDECL);

    // template <class T, class U> T Max(T lhs, U rhs) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Max(float, float)", AS_FUNCTIONPR(Max, (float, float), float), AS_CALL_CDECL);
    engine->RegisterGlobalFunction("int Max(int, int)", AS_FUNCTIONPR(Max, (int, int), int), AS_CALL_CDECL);

    // template <class T, class U> T Min(T lhs, U rhs) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Min(float, float)", AS_FUNCTIONPR(Min, (float, float), float), AS_CALL_CDECL);
    engine->RegisterGlobalFunction("int Min(int, int)", AS_FUNCTIONPR(Min, (int, int), int), AS_CALL_CDECL);

    // unsigned NextPowerOfTwo(unsigned value) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("uint NextPowerOfTwo(uint)", AS_FUNCTIONPR(NextPowerOfTwo, (unsigned), unsigned), AS_CALL_CDECL);

    // void OpenConsoleWindow() | File: ../Core/ProcessUtils.h
    engine->RegisterGlobalFunction("void OpenConsoleWindow()", AS_FUNCTIONPR(OpenConsoleWindow, (), void), AS_CALL_CDECL);

    // const Vector<String>& ParseArguments(const String& cmdLine, bool skipFirstArgument = true) | File: ../Core/ProcessUtils.h
    // Error: type "constString&" can not automatically bind

    // const Vector<String>& ParseArguments(const WString& cmdLine) | File: ../Core/ProcessUtils.h
    // Error: type "constWString&" can not automatically bind

    // const Vector<String>& ParseArguments(const char* cmdLine) | File: ../Core/ProcessUtils.h
    // Error: type "constchar*" can not automatically bind

    // const Vector<String>& ParseArguments(const wchar_t* cmdLine) | File: ../Core/ProcessUtils.h
    // Error: type "constwchar_t*" can not automatically bind

    // const Vector<String>& ParseArguments(int argc, char** argv) | File: ../Core/ProcessUtils.h
    // Error: type "char**" can not automatically bind

    // template <class T> T Pow(T x, T y) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Pow(float, float)", AS_FUNCTIONPR(Pow, (float, float), float), AS_CALL_CDECL);

    // void PrintLine(const String& str, bool error = false) | File: ../Core/ProcessUtils.h
    // Error: type "constString&" can not automatically bind

    // void PrintLine(const char* str, bool error = false) | File: ../Core/ProcessUtils.h
    // Error: type "constchar*" can not automatically bind

    // void PrintUnicode(const String& str, bool error = false) | File: ../Core/ProcessUtils.h
    // Error: type "constString&" can not automatically bind

    // void PrintUnicodeLine(const String& str, bool error = false) | File: ../Core/ProcessUtils.h
    // Error: type "constString&" can not automatically bind

    // int Rand() | File: ../Math/Random.h
    engine->RegisterGlobalFunction("int Rand()", AS_FUNCTIONPR(Rand, (), int), AS_CALL_CDECL);
    engine->RegisterGlobalFunction("int RandomInt()", AS_FUNCTIONPR(Rand, (), int), AS_CALL_CDECL);

    // float RandStandardNormal() | File: ../Math/Random.h
    engine->RegisterGlobalFunction("float RandStandardNormal()", AS_FUNCTIONPR(RandStandardNormal, (), float), AS_CALL_CDECL);

    // float Random() | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Random()", AS_FUNCTIONPR(Random, (), float), AS_CALL_CDECL);

    // float Random(float min, float max) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Random(float, float)", AS_FUNCTIONPR(Random, (float, float), float), AS_CALL_CDECL);

    // float Random(float range) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Random(float)", AS_FUNCTIONPR(Random, (float), float), AS_CALL_CDECL);

    // int Random(int min, int max) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("int Random(int, int)", AS_FUNCTIONPR(Random, (int, int), int), AS_CALL_CDECL);
    engine->RegisterGlobalFunction("int RandomInt(int, int)", AS_FUNCTIONPR(Random, (int, int), int), AS_CALL_CDECL);

    // int Random(int range) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("int Random(int)", AS_FUNCTIONPR(Random, (int), int), AS_CALL_CDECL);
    engine->RegisterGlobalFunction("int RandomInt(int)", AS_FUNCTIONPR(Random, (int), int), AS_CALL_CDECL);

    // float RandomNormal(float meanValue, float variance) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float RandomNormal(float, float)", AS_FUNCTIONPR(RandomNormal, (float, float), float), AS_CALL_CDECL);

    // void RegisterAudioLibrary(Context* context) | File: ../Audio/Audio.h
    // Not registered because have @nobind mark

    // void RegisterGraphicsLibrary(Context* context) | File: ../Graphics/Graphics.h
    // Not registered because have @nobind mark

    // void RegisterResourceLibrary(Context* context) | File: ../Resource/ResourceCache.h
    // Not registered because have @nobind mark

    // void RegisterSceneLibrary(Context* context) | File: ../Scene/Scene.h
    // Not registered because have @nobind mark

    // void RegisterUILibrary(Context* context) | File: ../UI/UI.h
    // Not registered because have @nobind mark

    // String RemoveTrailingSlash(const String& pathName) | File: ../IO/FileSystem.h
    // Error: type "constString&" can not automatically bind

    // String ReplaceExtension(const String& fullPath, const String& newExtension) | File: ../IO/FileSystem.h
    // Error: type "constString&" can not automatically bind

    // template <class T> T Round(T x) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Round(float)", AS_FUNCTIONPR(Round, (float), float), AS_CALL_CDECL);

    // template <class T> int RoundToInt(T x) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("int RoundToInt(float)", AS_FUNCTIONPR(RoundToInt, (float), int), AS_CALL_CDECL);

    // constexpr hash32 SDBMHash(hash32 hash, byte b) | File: ../Math/MathDefs.h
    // Not registered because have @nobind mark

    // constexpr hash32 SDBMHash(hash32 hash, u8 c) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("hash32 SDBMHash(hash32, uint8)", AS_FUNCTIONPR(SDBMHash, (hash32, u8), hash32), AS_CALL_CDECL);

    // void SetMiniDumpDir(const String& pathName) | File: ../Core/ProcessUtils.h
    // Error: type "constString&" can not automatically bind

    // void SetRandomSeed(unsigned seed) | File: ../Math/Random.h
    engine->RegisterGlobalFunction("void SetRandomSeed(uint)", AS_FUNCTIONPR(SetRandomSeed, (unsigned), void), AS_CALL_CDECL);

    // template <class T> T Sign(T value) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Sign(float)", AS_FUNCTIONPR(Sign, (float), float), AS_CALL_CDECL);

    // template <class T> T Sin(T angle) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Sin(float)", AS_FUNCTIONPR(Sin, (float), float), AS_CALL_CDECL);

    // void SinCos(float angle, float& sin, float& cos) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("void SinCos(float, float&, float&)", AS_FUNCTIONPR(SinCos, (float, float&, float&), void), AS_CALL_CDECL);

    // template <class T> T SmoothStep(T lhs, T rhs, T t) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float SmoothStep(float, float, float)", AS_FUNCTIONPR(SmoothStep, (float, float, float), float), AS_CALL_CDECL);

    // void SplitPath(const String& fullPath, String& pathName, String& fileName, String& extension, bool lowercaseExtension = true) | File: ../IO/FileSystem.h
    // Error: type "constString&" can not automatically bind

    // template <class T> T Sqrt(T x) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Sqrt(float)", AS_FUNCTIONPR(Sqrt, (float), float), AS_CALL_CDECL);

    // float StableRandom(const Vector2& seed) | File: ../Math/Vector2.h
    // Error: type "constVector2&" can not automatically bind

    // float StableRandom(const Vector3& seed) | File: ../Math/Vector3.h
    // Error: type "constVector3&" can not automatically bind

    // float StableRandom(float seed) | File: ../Math/Vector2.h
    engine->RegisterGlobalFunction("float StableRandom(float)", AS_FUNCTIONPR(StableRandom, (float), float), AS_CALL_CDECL);

    // void StringToBuffer(Vector<byte>& dest, const String& source) | File: ../Core/StringUtils.h
    // Error: type "Vector<byte>&" can not automatically bind

    // void StringToBuffer(Vector<byte>& dest, const char* source) | File: ../Core/StringUtils.h
    // Error: type "Vector<byte>&" can not automatically bind

    // template <class T> T Tan(T angle) | File: ../Math/MathDefs.h
    engine->RegisterGlobalFunction("float Tan(float)", AS_FUNCTIONPR(Tan, (float), float), AS_CALL_CDECL);

    // bool ToBool(const String& source) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // bool ToBool(const char* source) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // Color ToColor(const String& source) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // Color ToColor(const char* source) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // double ToDouble(const String& source) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // double ToDouble(const char* source) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // float ToFloat(const String& source) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // float ToFloat(const char* source) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // i32 ToI32(const String& source, i32 base = 10) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // i32 ToI32(const char* source, i32 base = 10) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // i64 ToI64(const String& source, i32 base = 10) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // i64 ToI64(const char* source, i32 base = 10) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // IntRect ToIntRect(const String& source) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // IntRect ToIntRect(const char* source) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // IntVector2 ToIntVector2(const String& source) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // IntVector2 ToIntVector2(const char* source) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // IntVector3 ToIntVector3(const String& source) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // IntVector3 ToIntVector3(const char* source) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // unsigned ToLower(unsigned ch) | File: ../Core/StringUtils.h
    engine->RegisterGlobalFunction("uint ToLower(uint)", AS_FUNCTIONPR(ToLower, (unsigned), unsigned), AS_CALL_CDECL);

    // Matrix3 ToMatrix3(const String& source) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // Matrix3 ToMatrix3(const char* source) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // Matrix3x4 ToMatrix3x4(const String& source) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // Matrix3x4 ToMatrix3x4(const char* source) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // Matrix4 ToMatrix4(const String& source) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // Matrix4 ToMatrix4(const char* source) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // Quaternion ToQuaternion(const String& source) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // Quaternion ToQuaternion(const char* source) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // Rect ToRect(const String& source) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // Rect ToRect(const char* source) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // String ToString(const char* formatString,...) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // String ToString(void* value) | File: ../Core/StringUtils.h
    // Error: type "void*" can not automatically bind

    // String ToStringHex(unsigned value) | File: ../Core/StringUtils.h
    engine->RegisterGlobalFunction("String ToStringHex(uint)", AS_FUNCTIONPR(ToStringHex, (unsigned), String), AS_CALL_CDECL);

    // u32 ToU32(const String& source, i32 base = 10) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // u32 ToU32(const char* source, i32 base = 10) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // u64 ToU64(const String& source, i32 base = 10) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // u64 ToU64(const char* source, i32 base = 10) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // unsigned ToUpper(unsigned ch) | File: ../Core/StringUtils.h
    engine->RegisterGlobalFunction("uint ToUpper(uint)", AS_FUNCTIONPR(ToUpper, (unsigned), unsigned), AS_CALL_CDECL);

    // Vector2 ToVector2(const String& source) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // Vector2 ToVector2(const char* source) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // Vector3 ToVector3(const String& source) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // Vector3 ToVector3(const char* source) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // Vector4 ToVector4(const String& source, bool allowMissingCoords = false) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // Vector4 ToVector4(const char* source, bool allowMissingCoords = false) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // Variant ToVectorVariant(const String& source) | File: ../Core/StringUtils.h
    // Error: type "constString&" can not automatically bind

    // Variant ToVectorVariant(const char* source) | File: ../Core/StringUtils.h
    // Error: type "constchar*" can not automatically bind

    // IntVector2 VectorAbs(const IntVector2& vec) | File: ../Math/Vector2.h
    // Error: type "constIntVector2&" can not automatically bind

    // IntVector3 VectorAbs(const IntVector3& vec) | File: ../Math/Vector3.h
    // Error: type "constIntVector3&" can not automatically bind

    // Vector2 VectorAbs(const Vector2& vec) | File: ../Math/Vector2.h
    // Error: type "constVector2&" can not automatically bind

    // Vector3 VectorAbs(const Vector3& vec) | File: ../Math/Vector3.h
    // Error: type "constVector3&" can not automatically bind

    // Vector2 VectorCeil(const Vector2& vec) | File: ../Math/Vector2.h
    // Error: type "constVector2&" can not automatically bind

    // Vector3 VectorCeil(const Vector3& vec) | File: ../Math/Vector3.h
    // Error: type "constVector3&" can not automatically bind

    // Vector4 VectorCeil(const Vector4& vec) | File: ../Math/Vector4.h
    // Error: type "constVector4&" can not automatically bind

    // IntVector2 VectorCeilToInt(const Vector2& vec) | File: ../Math/Vector2.h
    // Error: type "constVector2&" can not automatically bind

    // IntVector3 VectorCeilToInt(const Vector3& vec) | File: ../Math/Vector3.h
    // Error: type "constVector3&" can not automatically bind

    // Vector2 VectorFloor(const Vector2& vec) | File: ../Math/Vector2.h
    // Error: type "constVector2&" can not automatically bind

    // Vector3 VectorFloor(const Vector3& vec) | File: ../Math/Vector3.h
    // Error: type "constVector3&" can not automatically bind

    // Vector4 VectorFloor(const Vector4& vec) | File: ../Math/Vector4.h
    // Error: type "constVector4&" can not automatically bind

    // IntVector2 VectorFloorToInt(const Vector2& vec) | File: ../Math/Vector2.h
    // Error: type "constVector2&" can not automatically bind

    // IntVector3 VectorFloorToInt(const Vector3& vec) | File: ../Math/Vector3.h
    // Error: type "constVector3&" can not automatically bind

    // Vector2 VectorLerp(const Vector2& lhs, const Vector2& rhs, const Vector2& t) | File: ../Math/Vector2.h
    // Error: type "constVector2&" can not automatically bind

    // Vector3 VectorLerp(const Vector3& lhs, const Vector3& rhs, const Vector3& t) | File: ../Math/Vector3.h
    // Error: type "constVector3&" can not automatically bind

    // Vector4 VectorLerp(const Vector4& lhs, const Vector4& rhs, const Vector4& t) | File: ../Math/Vector4.h
    // Error: type "constVector4&" can not automatically bind

    // IntVector2 VectorMax(const IntVector2& lhs, const IntVector2& rhs) | File: ../Math/Vector2.h
    // Error: type "constIntVector2&" can not automatically bind

    // IntVector3 VectorMax(const IntVector3& lhs, const IntVector3& rhs) | File: ../Math/Vector3.h
    // Error: type "constIntVector3&" can not automatically bind

    // Vector2 VectorMax(const Vector2& lhs, const Vector2& rhs) | File: ../Math/Vector2.h
    // Error: type "constVector2&" can not automatically bind

    // Vector3 VectorMax(const Vector3& lhs, const Vector3& rhs) | File: ../Math/Vector3.h
    // Error: type "constVector3&" can not automatically bind

    // Vector4 VectorMax(const Vector4& lhs, const Vector4& rhs) | File: ../Math/Vector4.h
    // Error: type "constVector4&" can not automatically bind

    // IntVector2 VectorMin(const IntVector2& lhs, const IntVector2& rhs) | File: ../Math/Vector2.h
    // Error: type "constIntVector2&" can not automatically bind

    // IntVector3 VectorMin(const IntVector3& lhs, const IntVector3& rhs) | File: ../Math/Vector3.h
    // Error: type "constIntVector3&" can not automatically bind

    // Vector2 VectorMin(const Vector2& lhs, const Vector2& rhs) | File: ../Math/Vector2.h
    // Error: type "constVector2&" can not automatically bind

    // Vector3 VectorMin(const Vector3& lhs, const Vector3& rhs) | File: ../Math/Vector3.h
    // Error: type "constVector3&" can not automatically bind

    // Vector4 VectorMin(const Vector4& lhs, const Vector4& rhs) | File: ../Math/Vector4.h
    // Error: type "constVector4&" can not automatically bind

    // Vector2 VectorRound(const Vector2& vec) | File: ../Math/Vector2.h
    // Error: type "constVector2&" can not automatically bind

    // Vector3 VectorRound(const Vector3& vec) | File: ../Math/Vector3.h
    // Error: type "constVector3&" can not automatically bind

    // Vector4 VectorRound(const Vector4& vec) | File: ../Math/Vector4.h
    // Error: type "constVector4&" can not automatically bind

    // IntVector2 VectorRoundToInt(const Vector2& vec) | File: ../Math/Vector2.h
    // Error: type "constVector2&" can not automatically bind

    // IntVector3 VectorRoundToInt(const Vector3& vec) | File: ../Math/Vector3.h
    // Error: type "constVector3&" can not automatically bind

    // bool WriteDrawablesToOBJ(const Vector<Drawable*>& drawables, File* outputFile, bool asZUp, bool asRightHanded, bool writeLightmapUV = false) | File: ../Graphics/Drawable.h
    // Error: type "constVector<Drawable*>&" can not automatically bind

#ifdef URHO3D_IK
    // void RegisterIKLibrary(Context* context) | File: ../IK/IK.h
    // Not registered because have @nobind mark
#endif

#ifdef URHO3D_NAVIGATION
    // void RegisterNavigationLibrary(Context* context) | File: ../Navigation/NavigationMesh.h
    // Not registered because have @nobind mark
#endif

#ifdef URHO3D_NETWORK
    // void RegisterNetworkLibrary(Context* context) | File: ../Network/Network.h
    // Not registered because have @nobind mark
#endif

#ifdef URHO3D_PHYSICS
    // void RegisterPhysicsLibrary(Context* context) | File: ../Physics/PhysicsWorld.h
    // Not registered because have @nobind mark
#endif

#ifdef URHO3D_PHYSICS2D
    // void RegisterPhysics2DLibrary(Context* context) | File: ../Physics2D/Physics2D.h
    // Not registered because have @nobind mark
#endif

#ifdef URHO3D_URHO2D
    // void RegisterUrho2DLibrary(Context* context) | File: ../Urho2D/Urho2D.h
    // Not registered because have @nobind mark
#endif
}

}
