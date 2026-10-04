// DO NOT EDIT. This file is generated

// We need register all enums before registration of any functions because functions can use any enums

#include "../Precompiled.h"
#include "../AngelScript/APITemplates.h"

#include "../AngelScript/Generated_Includes.h"

namespace Urho3D
{

// enum class AnimationChannels : u8 | File: ../Graphics/Animation.h
static const u8 AnimationChannels_None = static_cast<u8>(AnimationChannels::None);
static const u8 AnimationChannels_Position = static_cast<u8>(AnimationChannels::Position);
static const u8 AnimationChannels_Rotation = static_cast<u8>(AnimationChannels::Rotation);
static const u8 AnimationChannels_Scale = static_cast<u8>(AnimationChannels::Scale);

// enum ClearTarget : u32 | File: ../GraphicsAPI/GraphicsDefs.h
static const u32 ClearTarget_CLEAR_COLOR = CLEAR_COLOR;
static const u32 ClearTarget_CLEAR_DEPTH = CLEAR_DEPTH;
static const u32 ClearTarget_CLEAR_STENCIL = CLEAR_STENCIL;

// enum class DebugHudElements | File: ../Engine/DebugHud.h
static const int DebugHudElements_None = static_cast<int>(DebugHudElements::None);
static const int DebugHudElements_Stats = static_cast<int>(DebugHudElements::Stats);
static const int DebugHudElements_Mode = static_cast<int>(DebugHudElements::Mode);
static const int DebugHudElements_Profiler = static_cast<int>(DebugHudElements::Profiler);
static const int DebugHudElements_Memory = static_cast<int>(DebugHudElements::Memory);
static const int DebugHudElements_EventProfiler = static_cast<int>(DebugHudElements::EventProfiler);
static const int DebugHudElements_All = static_cast<int>(DebugHudElements::All);

// enum class DrawableTypes : u8 | File: ../Graphics/Drawable.h
static const u8 DrawableTypes_Undefined = static_cast<u8>(DrawableTypes::Undefined);
static const u8 DrawableTypes_Geometry = static_cast<u8>(DrawableTypes::Geometry);
static const u8 DrawableTypes_Light = static_cast<u8>(DrawableTypes::Light);
static const u8 DrawableTypes_Zone = static_cast<u8>(DrawableTypes::Zone);
static const u8 DrawableTypes_Geometry2D = static_cast<u8>(DrawableTypes::Geometry2D);
static const u8 DrawableTypes_Any = static_cast<u8>(DrawableTypes::Any);

// enum class LogicComponentEvents | File: ../Scene/LogicComponent.h
static const int LogicComponentEvents_None = static_cast<int>(LogicComponentEvents::None);
static const int LogicComponentEvents_Update = static_cast<int>(LogicComponentEvents::Update);
static const int LogicComponentEvents_PostUpdate = static_cast<int>(LogicComponentEvents::PostUpdate);
static const int LogicComponentEvents_FixedUpdate = static_cast<int>(LogicComponentEvents::FixedUpdate);
static const int LogicComponentEvents_FixedPostUpdate = static_cast<int>(LogicComponentEvents::FixedPostUpdate);
static const int LogicComponentEvents_All = static_cast<int>(LogicComponentEvents::All);

// enum MaterialQuality : u32 | File: ../GraphicsAPI/GraphicsDefs.h
static const u32 MaterialQuality_QUALITY_LOW = QUALITY_LOW;
static const u32 MaterialQuality_QUALITY_MEDIUM = QUALITY_MEDIUM;
static const u32 MaterialQuality_QUALITY_HIGH = QUALITY_HIGH;
static const u32 MaterialQuality_QUALITY_MAX = QUALITY_MAX;

// enum class TransformSpace | File: ../Scene/Node.h
static const int TransformSpace_Local = static_cast<int>(TransformSpace::Local);
static const int TransformSpace_Parent = static_cast<int>(TransformSpace::Parent);
static const int TransformSpace_World = static_cast<int>(TransformSpace::World);

// enum class VertexElements : u32 | File: ../GraphicsAPI/GraphicsDefs.h
static const u32 VertexElements_None = static_cast<u32>(VertexElements::None);
static const u32 VertexElements_Position = static_cast<u32>(VertexElements::Position);
static const u32 VertexElements_Normal = static_cast<u32>(VertexElements::Normal);
static const u32 VertexElements_Color = static_cast<u32>(VertexElements::Color);
static const u32 VertexElements_TexCoord1 = static_cast<u32>(VertexElements::TexCoord1);
static const u32 VertexElements_TexCoord2 = static_cast<u32>(VertexElements::TexCoord2);
static const u32 VertexElements_CubeTexCoord1 = static_cast<u32>(VertexElements::CubeTexCoord1);
static const u32 VertexElements_CubeTexCoord2 = static_cast<u32>(VertexElements::CubeTexCoord2);
static const u32 VertexElements_Tangent = static_cast<u32>(VertexElements::Tangent);
static const u32 VertexElements_BlendWeights = static_cast<u32>(VertexElements::BlendWeights);
static const u32 VertexElements_BlendIndices = static_cast<u32>(VertexElements::BlendIndices);
static const u32 VertexElements_InstanceMatrix1 = static_cast<u32>(VertexElements::InstanceMatrix1);
static const u32 VertexElements_InstanceMatrix2 = static_cast<u32>(VertexElements::InstanceMatrix2);
static const u32 VertexElements_InstanceMatrix3 = static_cast<u32>(VertexElements::InstanceMatrix3);
static const u32 VertexElements_ObjectIndex = static_cast<u32>(VertexElements::ObjectIndex);

void ASRegisterGeneratedEnums(asIScriptEngine* engine)
{
    // enum AnimationBlendMode | File: ../Graphics/AnimationState.h
    engine->RegisterEnum("AnimationBlendMode");
    engine->RegisterEnumValue("AnimationBlendMode", "ABM_LERP", ABM_LERP);
    engine->RegisterEnumValue("AnimationBlendMode", "ABM_ADDITIVE", ABM_ADDITIVE);

    // enum class AnimationChannels : u8 | File: ../Graphics/Animation.h
    engine->RegisterTypedef("AnimationChannels", "uint8");
    engine->SetDefaultNamespace("AnimationChannels");
    engine->RegisterGlobalProperty("const uint8 None", (void*)&AnimationChannels_None);
    engine->RegisterGlobalProperty("const uint8 Position", (void*)&AnimationChannels_Position);
    engine->RegisterGlobalProperty("const uint8 Rotation", (void*)&AnimationChannels_Rotation);
    engine->RegisterGlobalProperty("const uint8 Scale", (void*)&AnimationChannels_Scale);
    engine->SetDefaultNamespace("");

    // enum AsyncLoadState | File: ../Resource/Resource.h
    engine->RegisterEnum("AsyncLoadState");
    engine->RegisterEnumValue("AsyncLoadState", "ASYNC_DONE", ASYNC_DONE);
    engine->RegisterEnumValue("AsyncLoadState", "ASYNC_QUEUED", ASYNC_QUEUED);
    engine->RegisterEnumValue("AsyncLoadState", "ASYNC_LOADING", ASYNC_LOADING);
    engine->RegisterEnumValue("AsyncLoadState", "ASYNC_SUCCESS", ASYNC_SUCCESS);
    engine->RegisterEnumValue("AsyncLoadState", "ASYNC_FAIL", ASYNC_FAIL);

    // enum AttributeMode | File: ../Core/Attribute.h
    engine->RegisterEnum("AttributeMode");
    engine->RegisterEnumValue("AttributeMode", "AM_EDIT", AM_EDIT);
    engine->RegisterEnumValue("AttributeMode", "AM_FILE", AM_FILE);
    engine->RegisterEnumValue("AttributeMode", "AM_NET", AM_NET);
    engine->RegisterEnumValue("AttributeMode", "AM_DEFAULT", AM_DEFAULT);
    engine->RegisterEnumValue("AttributeMode", "AM_LATESTDATA", AM_LATESTDATA);
    engine->RegisterEnumValue("AttributeMode", "AM_NOEDIT", AM_NOEDIT);
    engine->RegisterEnumValue("AttributeMode", "AM_NODEID", AM_NODEID);
    engine->RegisterEnumValue("AttributeMode", "AM_COMPONENTID", AM_COMPONENTID);
    engine->RegisterEnumValue("AttributeMode", "AM_NODEIDVECTOR", AM_NODEIDVECTOR);
    engine->RegisterEnumValue("AttributeMode", "AM_FILEREADONLY", AM_FILEREADONLY);

    // URHO3D_FLAGSET(AttributeMode, AttributeModeFlags) | File: ../Core/Attribute.h
    engine->RegisterTypedef("AttributeModeFlags", "int");

    // enum AutoRemoveMode | File: ../Scene/Component.h
    engine->RegisterEnum("AutoRemoveMode");
    engine->RegisterEnumValue("AutoRemoveMode", "REMOVE_DISABLED", REMOVE_DISABLED);
    engine->RegisterEnumValue("AutoRemoveMode", "REMOVE_COMPONENT", REMOVE_COMPONENT);
    engine->RegisterEnumValue("AutoRemoveMode", "REMOVE_NODE", REMOVE_NODE);

    // enum BlendMode | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("BlendMode");
    engine->RegisterEnumValue("BlendMode", "BLEND_REPLACE", BLEND_REPLACE);
    engine->RegisterEnumValue("BlendMode", "BLEND_ADD", BLEND_ADD);
    engine->RegisterEnumValue("BlendMode", "BLEND_MULTIPLY", BLEND_MULTIPLY);
    engine->RegisterEnumValue("BlendMode", "BLEND_ALPHA", BLEND_ALPHA);
    engine->RegisterEnumValue("BlendMode", "BLEND_ADDALPHA", BLEND_ADDALPHA);
    engine->RegisterEnumValue("BlendMode", "BLEND_PREMULALPHA", BLEND_PREMULALPHA);
    engine->RegisterEnumValue("BlendMode", "BLEND_INVDESTALPHA", BLEND_INVDESTALPHA);
    engine->RegisterEnumValue("BlendMode", "BLEND_SUBTRACT", BLEND_SUBTRACT);
    engine->RegisterEnumValue("BlendMode", "BLEND_SUBTRACTALPHA", BLEND_SUBTRACTALPHA);
    engine->RegisterEnumValue("BlendMode", "MAX_BLENDMODES", MAX_BLENDMODES);

    // enum BoneCollisionShape | File: ../Graphics/Skeleton.h
    engine->RegisterEnum("BoneCollisionShape");
    engine->RegisterEnumValue("BoneCollisionShape", "BONECOLLISION_NONE", BONECOLLISION_NONE);
    engine->RegisterEnumValue("BoneCollisionShape", "BONECOLLISION_SPHERE", BONECOLLISION_SPHERE);
    engine->RegisterEnumValue("BoneCollisionShape", "BONECOLLISION_BOX", BONECOLLISION_BOX);

    // URHO3D_FLAGSET(BoneCollisionShape, BoneCollisionShapeFlags) | File: ../Graphics/Skeleton.h
    engine->RegisterTypedef("BoneCollisionShapeFlags", "int");

    // enum ClearTarget : u32 | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterTypedef("ClearTarget", "uint");
    engine->RegisterGlobalProperty("const uint CLEAR_COLOR", (void*)&ClearTarget_CLEAR_COLOR);
    engine->RegisterGlobalProperty("const uint CLEAR_DEPTH", (void*)&ClearTarget_CLEAR_DEPTH);
    engine->RegisterGlobalProperty("const uint CLEAR_STENCIL", (void*)&ClearTarget_CLEAR_STENCIL);

    // URHO3D_FLAGSET(ClearTarget, ClearTargetFlags) | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterTypedef("ClearTargetFlags", "uint");

    // enum CompareMode | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("CompareMode");
    engine->RegisterEnumValue("CompareMode", "CMP_ALWAYS", CMP_ALWAYS);
    engine->RegisterEnumValue("CompareMode", "CMP_EQUAL", CMP_EQUAL);
    engine->RegisterEnumValue("CompareMode", "CMP_NOTEQUAL", CMP_NOTEQUAL);
    engine->RegisterEnumValue("CompareMode", "CMP_LESS", CMP_LESS);
    engine->RegisterEnumValue("CompareMode", "CMP_LESSEQUAL", CMP_LESSEQUAL);
    engine->RegisterEnumValue("CompareMode", "CMP_GREATER", CMP_GREATER);
    engine->RegisterEnumValue("CompareMode", "CMP_GREATEREQUAL", CMP_GREATEREQUAL);
    engine->RegisterEnumValue("CompareMode", "MAX_COMPAREMODES", MAX_COMPAREMODES);

    // enum CompressedFormat | File: ../Resource/Image.h
    engine->RegisterEnum("CompressedFormat");
    engine->RegisterEnumValue("CompressedFormat", "CF_NONE", CF_NONE);
    engine->RegisterEnumValue("CompressedFormat", "CF_RGBA", CF_RGBA);
    engine->RegisterEnumValue("CompressedFormat", "CF_DXT1", CF_DXT1);
    engine->RegisterEnumValue("CompressedFormat", "CF_DXT3", CF_DXT3);
    engine->RegisterEnumValue("CompressedFormat", "CF_DXT5", CF_DXT5);
    engine->RegisterEnumValue("CompressedFormat", "CF_ETC1", CF_ETC1);
    engine->RegisterEnumValue("CompressedFormat", "CF_ETC2_RGB", CF_ETC2_RGB);
    engine->RegisterEnumValue("CompressedFormat", "CF_ETC2_RGBA", CF_ETC2_RGBA);
    engine->RegisterEnumValue("CompressedFormat", "CF_PVRTC_RGB_2BPP", CF_PVRTC_RGB_2BPP);
    engine->RegisterEnumValue("CompressedFormat", "CF_PVRTC_RGBA_2BPP", CF_PVRTC_RGBA_2BPP);
    engine->RegisterEnumValue("CompressedFormat", "CF_PVRTC_RGB_4BPP", CF_PVRTC_RGB_4BPP);
    engine->RegisterEnumValue("CompressedFormat", "CF_PVRTC_RGBA_4BPP", CF_PVRTC_RGBA_4BPP);

    // enum ControllerAxis | File: ../Input/InputConstants.h
    engine->RegisterEnum("ControllerAxis");
    engine->RegisterEnumValue("ControllerAxis", "CONTROLLER_AXIS_LEFTX", CONTROLLER_AXIS_LEFTX);
    engine->RegisterEnumValue("ControllerAxis", "CONTROLLER_AXIS_LEFTY", CONTROLLER_AXIS_LEFTY);
    engine->RegisterEnumValue("ControllerAxis", "CONTROLLER_AXIS_RIGHTX", CONTROLLER_AXIS_RIGHTX);
    engine->RegisterEnumValue("ControllerAxis", "CONTROLLER_AXIS_RIGHTY", CONTROLLER_AXIS_RIGHTY);
    engine->RegisterEnumValue("ControllerAxis", "CONTROLLER_AXIS_TRIGGERLEFT", CONTROLLER_AXIS_TRIGGERLEFT);
    engine->RegisterEnumValue("ControllerAxis", "CONTROLLER_AXIS_TRIGGERRIGHT", CONTROLLER_AXIS_TRIGGERRIGHT);

    // enum ControllerButton | File: ../Input/InputConstants.h
    engine->RegisterEnum("ControllerButton");
    engine->RegisterEnumValue("ControllerButton", "CONTROLLER_BUTTON_A", CONTROLLER_BUTTON_A);
    engine->RegisterEnumValue("ControllerButton", "CONTROLLER_BUTTON_B", CONTROLLER_BUTTON_B);
    engine->RegisterEnumValue("ControllerButton", "CONTROLLER_BUTTON_X", CONTROLLER_BUTTON_X);
    engine->RegisterEnumValue("ControllerButton", "CONTROLLER_BUTTON_Y", CONTROLLER_BUTTON_Y);
    engine->RegisterEnumValue("ControllerButton", "CONTROLLER_BUTTON_BACK", CONTROLLER_BUTTON_BACK);
    engine->RegisterEnumValue("ControllerButton", "CONTROLLER_BUTTON_GUIDE", CONTROLLER_BUTTON_GUIDE);
    engine->RegisterEnumValue("ControllerButton", "CONTROLLER_BUTTON_START", CONTROLLER_BUTTON_START);
    engine->RegisterEnumValue("ControllerButton", "CONTROLLER_BUTTON_LEFTSTICK", CONTROLLER_BUTTON_LEFTSTICK);
    engine->RegisterEnumValue("ControllerButton", "CONTROLLER_BUTTON_RIGHTSTICK", CONTROLLER_BUTTON_RIGHTSTICK);
    engine->RegisterEnumValue("ControllerButton", "CONTROLLER_BUTTON_LEFTSHOULDER", CONTROLLER_BUTTON_LEFTSHOULDER);
    engine->RegisterEnumValue("ControllerButton", "CONTROLLER_BUTTON_RIGHTSHOULDER", CONTROLLER_BUTTON_RIGHTSHOULDER);
    engine->RegisterEnumValue("ControllerButton", "CONTROLLER_BUTTON_DPAD_UP", CONTROLLER_BUTTON_DPAD_UP);
    engine->RegisterEnumValue("ControllerButton", "CONTROLLER_BUTTON_DPAD_DOWN", CONTROLLER_BUTTON_DPAD_DOWN);
    engine->RegisterEnumValue("ControllerButton", "CONTROLLER_BUTTON_DPAD_LEFT", CONTROLLER_BUTTON_DPAD_LEFT);
    engine->RegisterEnumValue("ControllerButton", "CONTROLLER_BUTTON_DPAD_RIGHT", CONTROLLER_BUTTON_DPAD_RIGHT);

    // enum Corner | File: ../UI/UIElement.h
    engine->RegisterEnum("Corner");
    engine->RegisterEnumValue("Corner", "C_TOPLEFT", C_TOPLEFT);
    engine->RegisterEnumValue("Corner", "C_TOPRIGHT", C_TOPRIGHT);
    engine->RegisterEnumValue("Corner", "C_BOTTOMLEFT", C_BOTTOMLEFT);
    engine->RegisterEnumValue("Corner", "C_BOTTOMRIGHT", C_BOTTOMRIGHT);
    engine->RegisterEnumValue("Corner", "MAX_UIELEMENT_CORNERS", MAX_UIELEMENT_CORNERS);

    // enum CreateMode | File: ../Scene/Node.h
    engine->RegisterEnum("CreateMode");
    engine->RegisterEnumValue("CreateMode", "REPLICATED", REPLICATED);
    engine->RegisterEnumValue("CreateMode", "LOCAL", LOCAL);

    // enum CubeMapFace | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("CubeMapFace");
    engine->RegisterEnumValue("CubeMapFace", "FACE_POSITIVE_X", FACE_POSITIVE_X);
    engine->RegisterEnumValue("CubeMapFace", "FACE_NEGATIVE_X", FACE_NEGATIVE_X);
    engine->RegisterEnumValue("CubeMapFace", "FACE_POSITIVE_Y", FACE_POSITIVE_Y);
    engine->RegisterEnumValue("CubeMapFace", "FACE_NEGATIVE_Y", FACE_NEGATIVE_Y);
    engine->RegisterEnumValue("CubeMapFace", "FACE_POSITIVE_Z", FACE_POSITIVE_Z);
    engine->RegisterEnumValue("CubeMapFace", "FACE_NEGATIVE_Z", FACE_NEGATIVE_Z);
    engine->RegisterEnumValue("CubeMapFace", "MAX_CUBEMAP_FACES", MAX_CUBEMAP_FACES);

    // enum CubeMapLayout | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("CubeMapLayout");
    engine->RegisterEnumValue("CubeMapLayout", "CML_HORIZONTAL", CML_HORIZONTAL);
    engine->RegisterEnumValue("CubeMapLayout", "CML_HORIZONTALNVIDIA", CML_HORIZONTALNVIDIA);
    engine->RegisterEnumValue("CubeMapLayout", "CML_HORIZONTALCROSS", CML_HORIZONTALCROSS);
    engine->RegisterEnumValue("CubeMapLayout", "CML_VERTICALCROSS", CML_VERTICALCROSS);
    engine->RegisterEnumValue("CubeMapLayout", "CML_BLENDER", CML_BLENDER);

    // enum CullMode | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("CullMode");
    engine->RegisterEnumValue("CullMode", "CULL_NONE", CULL_NONE);
    engine->RegisterEnumValue("CullMode", "CULL_CCW", CULL_CCW);
    engine->RegisterEnumValue("CullMode", "CULL_CW", CULL_CW);
    engine->RegisterEnumValue("CullMode", "MAX_CULLMODES", MAX_CULLMODES);

    // enum CursorShape | File: ../UI/Cursor.h
    engine->RegisterEnum("CursorShape");
    engine->RegisterEnumValue("CursorShape", "CS_NORMAL", CS_NORMAL);
    engine->RegisterEnumValue("CursorShape", "CS_IBEAM", CS_IBEAM);
    engine->RegisterEnumValue("CursorShape", "CS_CROSS", CS_CROSS);
    engine->RegisterEnumValue("CursorShape", "CS_RESIZEVERTICAL", CS_RESIZEVERTICAL);
    engine->RegisterEnumValue("CursorShape", "CS_RESIZEDIAGONAL_TOPRIGHT", CS_RESIZEDIAGONAL_TOPRIGHT);
    engine->RegisterEnumValue("CursorShape", "CS_RESIZEHORIZONTAL", CS_RESIZEHORIZONTAL);
    engine->RegisterEnumValue("CursorShape", "CS_RESIZEDIAGONAL_TOPLEFT", CS_RESIZEDIAGONAL_TOPLEFT);
    engine->RegisterEnumValue("CursorShape", "CS_RESIZE_ALL", CS_RESIZE_ALL);
    engine->RegisterEnumValue("CursorShape", "CS_ACCEPTDROP", CS_ACCEPTDROP);
    engine->RegisterEnumValue("CursorShape", "CS_REJECTDROP", CS_REJECTDROP);
    engine->RegisterEnumValue("CursorShape", "CS_BUSY", CS_BUSY);
    engine->RegisterEnumValue("CursorShape", "CS_BUSY_ARROW", CS_BUSY_ARROW);
    engine->RegisterEnumValue("CursorShape", "CS_MAX_SHAPES", CS_MAX_SHAPES);

    // enum class DebugHudElements | File: ../Engine/DebugHud.h
    engine->RegisterTypedef("DebugHudElements", "int");
    engine->SetDefaultNamespace("DebugHudElements");
    engine->RegisterGlobalProperty("const int None", (void*)&DebugHudElements_None);
    engine->RegisterGlobalProperty("const int Stats", (void*)&DebugHudElements_Stats);
    engine->RegisterGlobalProperty("const int Mode", (void*)&DebugHudElements_Mode);
    engine->RegisterGlobalProperty("const int Profiler", (void*)&DebugHudElements_Profiler);
    engine->RegisterGlobalProperty("const int Memory", (void*)&DebugHudElements_Memory);
    engine->RegisterGlobalProperty("const int EventProfiler", (void*)&DebugHudElements_EventProfiler);
    engine->RegisterGlobalProperty("const int All", (void*)&DebugHudElements_All);
    engine->SetDefaultNamespace("");

    // enum DeferredLightPSVariation | File: ../Graphics/Renderer.h
    engine->RegisterEnum("DeferredLightPSVariation");
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_NONE", DLPS_NONE);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_SPOT", DLPS_SPOT);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_POINT", DLPS_POINT);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_POINTMASK", DLPS_POINTMASK);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_SPEC", DLPS_SPEC);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_SPOTSPEC", DLPS_SPOTSPEC);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_POINTSPEC", DLPS_POINTSPEC);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_POINTMASKSPEC", DLPS_POINTMASKSPEC);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_SHADOW", DLPS_SHADOW);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_SPOTSHADOW", DLPS_SPOTSHADOW);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_POINTSHADOW", DLPS_POINTSHADOW);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_POINTMASKSHADOW", DLPS_POINTMASKSHADOW);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_SHADOWSPEC", DLPS_SHADOWSPEC);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_SPOTSHADOWSPEC", DLPS_SPOTSHADOWSPEC);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_POINTSHADOWSPEC", DLPS_POINTSHADOWSPEC);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_POINTMASKSHADOWSPEC", DLPS_POINTMASKSHADOWSPEC);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_SHADOWNORMALOFFSET", DLPS_SHADOWNORMALOFFSET);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_SPOTSHADOWNORMALOFFSET", DLPS_SPOTSHADOWNORMALOFFSET);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_POINTSHADOWNORMALOFFSET", DLPS_POINTSHADOWNORMALOFFSET);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_POINTMASKSHADOWNORMALOFFSET", DLPS_POINTMASKSHADOWNORMALOFFSET);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_SHADOWSPECNORMALOFFSET", DLPS_SHADOWSPECNORMALOFFSET);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_SPOTSHADOWSPECNORMALOFFSET", DLPS_SPOTSHADOWSPECNORMALOFFSET);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_POINTSHADOWSPECNORMALOFFSET", DLPS_POINTSHADOWSPECNORMALOFFSET);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_POINTMASKSHADOWSPECNORMALOFFSET", DLPS_POINTMASKSHADOWSPECNORMALOFFSET);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHO", DLPS_ORTHO);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOSPOT", DLPS_ORTHOSPOT);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOPOINT", DLPS_ORTHOPOINT);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOPOINTMASK", DLPS_ORTHOPOINTMASK);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOSPEC", DLPS_ORTHOSPEC);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOSPOTSPEC", DLPS_ORTHOSPOTSPEC);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOPOINTSPEC", DLPS_ORTHOPOINTSPEC);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOPOINTMASKSPEC", DLPS_ORTHOPOINTMASKSPEC);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOSHADOW", DLPS_ORTHOSHADOW);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOSPOTSHADOW", DLPS_ORTHOSPOTSHADOW);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOPOINTSHADOW", DLPS_ORTHOPOINTSHADOW);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOPOINTMASKSHADOW", DLPS_ORTHOPOINTMASKSHADOW);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOSHADOWSPEC", DLPS_ORTHOSHADOWSPEC);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOSPOTSHADOWSPEC", DLPS_ORTHOSPOTSHADOWSPEC);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOPOINTSHADOWSPEC", DLPS_ORTHOPOINTSHADOWSPEC);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOPOINTMASKSHADOWSPEC", DLPS_ORTHOPOINTMASKSHADOWSPEC);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOSHADOWNORMALOFFSET", DLPS_ORTHOSHADOWNORMALOFFSET);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOSPOTSHADOWNORMALOFFSET", DLPS_ORTHOSPOTSHADOWNORMALOFFSET);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOPOINTSHADOWNORMALOFFSET", DLPS_ORTHOPOINTSHADOWNORMALOFFSET);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOPOINTMASKSHADOWNORMALOFFSET", DLPS_ORTHOPOINTMASKSHADOWNORMALOFFSET);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOSHADOWSPECNORMALOFFSET", DLPS_ORTHOSHADOWSPECNORMALOFFSET);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOSPOTSHADOWSPECNORMALOFFSET", DLPS_ORTHOSPOTSHADOWSPECNORMALOFFSET);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOPOINTSHADOWSPECNORMALOFFSET", DLPS_ORTHOPOINTSHADOWSPECNORMALOFFSET);
    engine->RegisterEnumValue("DeferredLightPSVariation", "DLPS_ORTHOPOINTMASKSHADOWSPECNORMALOFFSET", DLPS_ORTHOPOINTMASKSHADOWSPECNORMALOFFSET);
    engine->RegisterEnumValue("DeferredLightPSVariation", "MAX_DEFERRED_LIGHT_PS_VARIATIONS", MAX_DEFERRED_LIGHT_PS_VARIATIONS);

    // enum DeferredLightVSVariation | File: ../Graphics/Renderer.h
    engine->RegisterEnum("DeferredLightVSVariation");
    engine->RegisterEnumValue("DeferredLightVSVariation", "DLVS_NONE", DLVS_NONE);
    engine->RegisterEnumValue("DeferredLightVSVariation", "DLVS_DIR", DLVS_DIR);
    engine->RegisterEnumValue("DeferredLightVSVariation", "DLVS_ORTHO", DLVS_ORTHO);
    engine->RegisterEnumValue("DeferredLightVSVariation", "DLVS_ORTHODIR", DLVS_ORTHODIR);
    engine->RegisterEnumValue("DeferredLightVSVariation", "MAX_DEFERRED_LIGHT_VS_VARIATIONS", MAX_DEFERRED_LIGHT_VS_VARIATIONS);

    // enum DragAndDropMode | File: ../UI/UIElement.h
    engine->RegisterEnum("DragAndDropMode");
    engine->RegisterEnumValue("DragAndDropMode", "DD_DISABLED", DD_DISABLED);
    engine->RegisterEnumValue("DragAndDropMode", "DD_SOURCE", DD_SOURCE);
    engine->RegisterEnumValue("DragAndDropMode", "DD_TARGET", DD_TARGET);
    engine->RegisterEnumValue("DragAndDropMode", "DD_SOURCE_AND_TARGET", DD_SOURCE_AND_TARGET);

    // URHO3D_FLAGSET(DragAndDropMode, DragAndDropModeFlags) | File: ../UI/UIElement.h
    engine->RegisterTypedef("DragAndDropModeFlags", "int");

    // enum class DrawableTypes : u8 | File: ../Graphics/Drawable.h
    engine->RegisterTypedef("DrawableTypes", "uint8");
    engine->SetDefaultNamespace("DrawableTypes");
    engine->RegisterGlobalProperty("const uint8 Undefined", (void*)&DrawableTypes_Undefined);
    engine->RegisterGlobalProperty("const uint8 Geometry", (void*)&DrawableTypes_Geometry);
    engine->RegisterGlobalProperty("const uint8 Light", (void*)&DrawableTypes_Light);
    engine->RegisterGlobalProperty("const uint8 Zone", (void*)&DrawableTypes_Zone);
    engine->RegisterGlobalProperty("const uint8 Geometry2D", (void*)&DrawableTypes_Geometry2D);
    engine->RegisterGlobalProperty("const uint8 Any", (void*)&DrawableTypes_Any);
    engine->SetDefaultNamespace("");

    // enum EmitterType | File: ../Graphics/ParticleEffect.h
    engine->RegisterEnum("EmitterType");
    engine->RegisterEnumValue("EmitterType", "EMITTER_SPHERE", EMITTER_SPHERE);
    engine->RegisterEnumValue("EmitterType", "EMITTER_BOX", EMITTER_BOX);
    engine->RegisterEnumValue("EmitterType", "EMITTER_SPHEREVOLUME", EMITTER_SPHEREVOLUME);
    engine->RegisterEnumValue("EmitterType", "EMITTER_CYLINDER", EMITTER_CYLINDER);
    engine->RegisterEnumValue("EmitterType", "EMITTER_RING", EMITTER_RING);

    // enum FaceCameraMode | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("FaceCameraMode");
    engine->RegisterEnumValue("FaceCameraMode", "FC_NONE", FC_NONE);
    engine->RegisterEnumValue("FaceCameraMode", "FC_ROTATE_XYZ", FC_ROTATE_XYZ);
    engine->RegisterEnumValue("FaceCameraMode", "FC_ROTATE_Y", FC_ROTATE_Y);
    engine->RegisterEnumValue("FaceCameraMode", "FC_LOOKAT_XYZ", FC_LOOKAT_XYZ);
    engine->RegisterEnumValue("FaceCameraMode", "FC_LOOKAT_Y", FC_LOOKAT_Y);
    engine->RegisterEnumValue("FaceCameraMode", "FC_LOOKAT_MIXED", FC_LOOKAT_MIXED);
    engine->RegisterEnumValue("FaceCameraMode", "FC_DIRECTION", FC_DIRECTION);

    // enum FileMode | File: ../IO/File.h
    engine->RegisterEnum("FileMode");
    engine->RegisterEnumValue("FileMode", "FILE_READ", FILE_READ);
    engine->RegisterEnumValue("FileMode", "FILE_WRITE", FILE_WRITE);
    engine->RegisterEnumValue("FileMode", "FILE_READWRITE", FILE_READWRITE);

    // enum FillMode | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("FillMode");
    engine->RegisterEnumValue("FillMode", "FILL_SOLID", FILL_SOLID);
    engine->RegisterEnumValue("FillMode", "FILL_WIREFRAME", FILL_WIREFRAME);
    engine->RegisterEnumValue("FillMode", "FILL_POINT", FILL_POINT);

    // enum FocusMode | File: ../UI/UIElement.h
    engine->RegisterEnum("FocusMode");
    engine->RegisterEnumValue("FocusMode", "FM_NOTFOCUSABLE", FM_NOTFOCUSABLE);
    engine->RegisterEnumValue("FocusMode", "FM_RESETFOCUS", FM_RESETFOCUS);
    engine->RegisterEnumValue("FocusMode", "FM_FOCUSABLE", FM_FOCUSABLE);
    engine->RegisterEnumValue("FocusMode", "FM_FOCUSABLE_DEFOCUSABLE", FM_FOCUSABLE_DEFOCUSABLE);

    // enum FontHintLevel | File: ../UI/UI.h
    engine->RegisterEnum("FontHintLevel");
    engine->RegisterEnumValue("FontHintLevel", "FONT_HINT_LEVEL_NONE", FONT_HINT_LEVEL_NONE);
    engine->RegisterEnumValue("FontHintLevel", "FONT_HINT_LEVEL_LIGHT", FONT_HINT_LEVEL_LIGHT);
    engine->RegisterEnumValue("FontHintLevel", "FONT_HINT_LEVEL_NORMAL", FONT_HINT_LEVEL_NORMAL);

    // enum FontType | File: ../UI/Font.h
    engine->RegisterEnum("FontType");
    engine->RegisterEnumValue("FontType", "FONT_NONE", FONT_NONE);
    engine->RegisterEnumValue("FontType", "FONT_FREETYPE", FONT_FREETYPE);
    engine->RegisterEnumValue("FontType", "FONT_BITMAP", FONT_BITMAP);
    engine->RegisterEnumValue("FontType", "MAX_FONT_TYPES", MAX_FONT_TYPES);

    // enum FrustumPlane | File: ../Math/Frustum.h
    engine->RegisterEnum("FrustumPlane");
    engine->RegisterEnumValue("FrustumPlane", "PLANE_NEAR", PLANE_NEAR);
    engine->RegisterEnumValue("FrustumPlane", "PLANE_LEFT", PLANE_LEFT);
    engine->RegisterEnumValue("FrustumPlane", "PLANE_RIGHT", PLANE_RIGHT);
    engine->RegisterEnumValue("FrustumPlane", "PLANE_UP", PLANE_UP);
    engine->RegisterEnumValue("FrustumPlane", "PLANE_DOWN", PLANE_DOWN);
    engine->RegisterEnumValue("FrustumPlane", "PLANE_FAR", PLANE_FAR);

    // enum GAPI | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("GAPI");
    engine->RegisterEnumValue("GAPI", "GAPI_NONE", GAPI_NONE);
    engine->RegisterEnumValue("GAPI", "GAPI_OPENGL", GAPI_OPENGL);
    engine->RegisterEnumValue("GAPI", "GAPI_D3D11", GAPI_D3D11);
    engine->RegisterEnumValue("GAPI", "GAPI_VULKAN", GAPI_VULKAN);

    // enum GeometryType | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("GeometryType");
    engine->RegisterEnumValue("GeometryType", "GEOM_STATIC", GEOM_STATIC);
    engine->RegisterEnumValue("GeometryType", "GEOM_SKINNED", GEOM_SKINNED);
    engine->RegisterEnumValue("GeometryType", "GEOM_INSTANCED", GEOM_INSTANCED);
    engine->RegisterEnumValue("GeometryType", "GEOM_BILLBOARD", GEOM_BILLBOARD);
    engine->RegisterEnumValue("GeometryType", "GEOM_DIRBILLBOARD", GEOM_DIRBILLBOARD);
    engine->RegisterEnumValue("GeometryType", "GEOM_TRAIL_FACE_CAMERA", GEOM_TRAIL_FACE_CAMERA);
    engine->RegisterEnumValue("GeometryType", "GEOM_TRAIL_BONE", GEOM_TRAIL_BONE);
    engine->RegisterEnumValue("GeometryType", "MAX_GEOMETRYTYPES", MAX_GEOMETRYTYPES);
    engine->RegisterEnumValue("GeometryType", "GEOM_STATIC_NOINSTANCING", GEOM_STATIC_NOINSTANCING);

    // enum HatPosition | File: ../Input/InputConstants.h
    engine->RegisterEnum("HatPosition");
    engine->RegisterEnumValue("HatPosition", "HAT_CENTER", HAT_CENTER);
    engine->RegisterEnumValue("HatPosition", "HAT_UP", HAT_UP);
    engine->RegisterEnumValue("HatPosition", "HAT_RIGHT", HAT_RIGHT);
    engine->RegisterEnumValue("HatPosition", "HAT_DOWN", HAT_DOWN);
    engine->RegisterEnumValue("HatPosition", "HAT_LEFT", HAT_LEFT);

    // enum HighlightMode | File: ../UI/ListView.h
    engine->RegisterEnum("HighlightMode");
    engine->RegisterEnumValue("HighlightMode", "HM_NEVER", HM_NEVER);
    engine->RegisterEnumValue("HighlightMode", "HM_FOCUS", HM_FOCUS);
    engine->RegisterEnumValue("HighlightMode", "HM_ALWAYS", HM_ALWAYS);

    // enum HorizontalAlignment | File: ../UI/UIElement.h
    engine->RegisterEnum("HorizontalAlignment");
    engine->RegisterEnumValue("HorizontalAlignment", "HA_LEFT", HA_LEFT);
    engine->RegisterEnumValue("HorizontalAlignment", "HA_CENTER", HA_CENTER);
    engine->RegisterEnumValue("HorizontalAlignment", "HA_RIGHT", HA_RIGHT);
    engine->RegisterEnumValue("HorizontalAlignment", "HA_CUSTOM", HA_CUSTOM);

    // enum InterpMethod | File: ../Scene/ValueAnimation.h
    engine->RegisterEnum("InterpMethod");
    engine->RegisterEnumValue("InterpMethod", "IM_NONE", IM_NONE);
    engine->RegisterEnumValue("InterpMethod", "IM_LINEAR", IM_LINEAR);
    engine->RegisterEnumValue("InterpMethod", "IM_SPLINE", IM_SPLINE);

    // enum InterpolationMode | File: ../Core/Spline.h
    engine->RegisterEnum("InterpolationMode");
    engine->RegisterEnumValue("InterpolationMode", "BEZIER_CURVE", BEZIER_CURVE);
    engine->RegisterEnumValue("InterpolationMode", "CATMULL_ROM_CURVE", CATMULL_ROM_CURVE);
    engine->RegisterEnumValue("InterpolationMode", "LINEAR_CURVE", LINEAR_CURVE);
    engine->RegisterEnumValue("InterpolationMode", "CATMULL_ROM_FULL_CURVE", CATMULL_ROM_FULL_CURVE);

    // enum Intersection | File: ../Math/MathDefs.h
    engine->RegisterEnum("Intersection");
    engine->RegisterEnumValue("Intersection", "OUTSIDE", OUTSIDE);
    engine->RegisterEnumValue("Intersection", "INTERSECTS", INTERSECTS);
    engine->RegisterEnumValue("Intersection", "INSIDE", INSIDE);

    // enum JSONNumberType | File: ../Resource/JSONValue.h
    engine->RegisterEnum("JSONNumberType");
    engine->RegisterEnumValue("JSONNumberType", "JSONNT_NAN", JSONNT_NAN);
    engine->RegisterEnumValue("JSONNumberType", "JSONNT_INT", JSONNT_INT);
    engine->RegisterEnumValue("JSONNumberType", "JSONNT_UINT", JSONNT_UINT);
    engine->RegisterEnumValue("JSONNumberType", "JSONNT_FLOAT_DOUBLE", JSONNT_FLOAT_DOUBLE);

    // enum JSONValueType | File: ../Resource/JSONValue.h
    engine->RegisterEnum("JSONValueType");
    engine->RegisterEnumValue("JSONValueType", "JSON_NULL", JSON_NULL);
    engine->RegisterEnumValue("JSONValueType", "JSON_BOOL", JSON_BOOL);
    engine->RegisterEnumValue("JSONValueType", "JSON_NUMBER", JSON_NUMBER);
    engine->RegisterEnumValue("JSONValueType", "JSON_STRING", JSON_STRING);
    engine->RegisterEnumValue("JSONValueType", "JSON_ARRAY", JSON_ARRAY);
    engine->RegisterEnumValue("JSONValueType", "JSON_OBJECT", JSON_OBJECT);

    // enum Key | File: ../Input/InputConstants.h
    engine->RegisterEnum("Key");
    engine->RegisterEnumValue("Key", "KEY_UNKNOWN", KEY_UNKNOWN);
    engine->RegisterEnumValue("Key", "KEY_A", KEY_A);
    engine->RegisterEnumValue("Key", "KEY_B", KEY_B);
    engine->RegisterEnumValue("Key", "KEY_C", KEY_C);
    engine->RegisterEnumValue("Key", "KEY_D", KEY_D);
    engine->RegisterEnumValue("Key", "KEY_E", KEY_E);
    engine->RegisterEnumValue("Key", "KEY_F", KEY_F);
    engine->RegisterEnumValue("Key", "KEY_G", KEY_G);
    engine->RegisterEnumValue("Key", "KEY_H", KEY_H);
    engine->RegisterEnumValue("Key", "KEY_I", KEY_I);
    engine->RegisterEnumValue("Key", "KEY_J", KEY_J);
    engine->RegisterEnumValue("Key", "KEY_K", KEY_K);
    engine->RegisterEnumValue("Key", "KEY_L", KEY_L);
    engine->RegisterEnumValue("Key", "KEY_M", KEY_M);
    engine->RegisterEnumValue("Key", "KEY_N", KEY_N);
    engine->RegisterEnumValue("Key", "KEY_O", KEY_O);
    engine->RegisterEnumValue("Key", "KEY_P", KEY_P);
    engine->RegisterEnumValue("Key", "KEY_Q", KEY_Q);
    engine->RegisterEnumValue("Key", "KEY_R", KEY_R);
    engine->RegisterEnumValue("Key", "KEY_S", KEY_S);
    engine->RegisterEnumValue("Key", "KEY_T", KEY_T);
    engine->RegisterEnumValue("Key", "KEY_U", KEY_U);
    engine->RegisterEnumValue("Key", "KEY_V", KEY_V);
    engine->RegisterEnumValue("Key", "KEY_W", KEY_W);
    engine->RegisterEnumValue("Key", "KEY_X", KEY_X);
    engine->RegisterEnumValue("Key", "KEY_Y", KEY_Y);
    engine->RegisterEnumValue("Key", "KEY_Z", KEY_Z);
    engine->RegisterEnumValue("Key", "KEY_0", KEY_0);
    engine->RegisterEnumValue("Key", "KEY_1", KEY_1);
    engine->RegisterEnumValue("Key", "KEY_2", KEY_2);
    engine->RegisterEnumValue("Key", "KEY_3", KEY_3);
    engine->RegisterEnumValue("Key", "KEY_4", KEY_4);
    engine->RegisterEnumValue("Key", "KEY_5", KEY_5);
    engine->RegisterEnumValue("Key", "KEY_6", KEY_6);
    engine->RegisterEnumValue("Key", "KEY_7", KEY_7);
    engine->RegisterEnumValue("Key", "KEY_8", KEY_8);
    engine->RegisterEnumValue("Key", "KEY_9", KEY_9);
    engine->RegisterEnumValue("Key", "KEY_BACKSPACE", KEY_BACKSPACE);
    engine->RegisterEnumValue("Key", "KEY_TAB", KEY_TAB);
    engine->RegisterEnumValue("Key", "KEY_RETURN", KEY_RETURN);
    engine->RegisterEnumValue("Key", "KEY_RETURN2", KEY_RETURN2);
    engine->RegisterEnumValue("Key", "KEY_KP_ENTER", KEY_KP_ENTER);
    engine->RegisterEnumValue("Key", "KEY_SHIFT", KEY_SHIFT);
    engine->RegisterEnumValue("Key", "KEY_CTRL", KEY_CTRL);
    engine->RegisterEnumValue("Key", "KEY_ALT", KEY_ALT);
    engine->RegisterEnumValue("Key", "KEY_GUI", KEY_GUI);
    engine->RegisterEnumValue("Key", "KEY_PAUSE", KEY_PAUSE);
    engine->RegisterEnumValue("Key", "KEY_CAPSLOCK", KEY_CAPSLOCK);
    engine->RegisterEnumValue("Key", "KEY_ESCAPE", KEY_ESCAPE);
    engine->RegisterEnumValue("Key", "KEY_SPACE", KEY_SPACE);
    engine->RegisterEnumValue("Key", "KEY_PAGEUP", KEY_PAGEUP);
    engine->RegisterEnumValue("Key", "KEY_PAGEDOWN", KEY_PAGEDOWN);
    engine->RegisterEnumValue("Key", "KEY_END", KEY_END);
    engine->RegisterEnumValue("Key", "KEY_HOME", KEY_HOME);
    engine->RegisterEnumValue("Key", "KEY_LEFT", KEY_LEFT);
    engine->RegisterEnumValue("Key", "KEY_UP", KEY_UP);
    engine->RegisterEnumValue("Key", "KEY_RIGHT", KEY_RIGHT);
    engine->RegisterEnumValue("Key", "KEY_DOWN", KEY_DOWN);
    engine->RegisterEnumValue("Key", "KEY_SELECT", KEY_SELECT);
    engine->RegisterEnumValue("Key", "KEY_PRINTSCREEN", KEY_PRINTSCREEN);
    engine->RegisterEnumValue("Key", "KEY_INSERT", KEY_INSERT);
    engine->RegisterEnumValue("Key", "KEY_DELETE", KEY_DELETE);
    engine->RegisterEnumValue("Key", "KEY_LGUI", KEY_LGUI);
    engine->RegisterEnumValue("Key", "KEY_RGUI", KEY_RGUI);
    engine->RegisterEnumValue("Key", "KEY_APPLICATION", KEY_APPLICATION);
    engine->RegisterEnumValue("Key", "KEY_KP_0", KEY_KP_0);
    engine->RegisterEnumValue("Key", "KEY_KP_1", KEY_KP_1);
    engine->RegisterEnumValue("Key", "KEY_KP_2", KEY_KP_2);
    engine->RegisterEnumValue("Key", "KEY_KP_3", KEY_KP_3);
    engine->RegisterEnumValue("Key", "KEY_KP_4", KEY_KP_4);
    engine->RegisterEnumValue("Key", "KEY_KP_5", KEY_KP_5);
    engine->RegisterEnumValue("Key", "KEY_KP_6", KEY_KP_6);
    engine->RegisterEnumValue("Key", "KEY_KP_7", KEY_KP_7);
    engine->RegisterEnumValue("Key", "KEY_KP_8", KEY_KP_8);
    engine->RegisterEnumValue("Key", "KEY_KP_9", KEY_KP_9);
    engine->RegisterEnumValue("Key", "KEY_KP_MULTIPLY", KEY_KP_MULTIPLY);
    engine->RegisterEnumValue("Key", "KEY_KP_PLUS", KEY_KP_PLUS);
    engine->RegisterEnumValue("Key", "KEY_KP_MINUS", KEY_KP_MINUS);
    engine->RegisterEnumValue("Key", "KEY_KP_PERIOD", KEY_KP_PERIOD);
    engine->RegisterEnumValue("Key", "KEY_KP_DIVIDE", KEY_KP_DIVIDE);
    engine->RegisterEnumValue("Key", "KEY_F1", KEY_F1);
    engine->RegisterEnumValue("Key", "KEY_F2", KEY_F2);
    engine->RegisterEnumValue("Key", "KEY_F3", KEY_F3);
    engine->RegisterEnumValue("Key", "KEY_F4", KEY_F4);
    engine->RegisterEnumValue("Key", "KEY_F5", KEY_F5);
    engine->RegisterEnumValue("Key", "KEY_F6", KEY_F6);
    engine->RegisterEnumValue("Key", "KEY_F7", KEY_F7);
    engine->RegisterEnumValue("Key", "KEY_F8", KEY_F8);
    engine->RegisterEnumValue("Key", "KEY_F9", KEY_F9);
    engine->RegisterEnumValue("Key", "KEY_F10", KEY_F10);
    engine->RegisterEnumValue("Key", "KEY_F11", KEY_F11);
    engine->RegisterEnumValue("Key", "KEY_F12", KEY_F12);
    engine->RegisterEnumValue("Key", "KEY_F13", KEY_F13);
    engine->RegisterEnumValue("Key", "KEY_F14", KEY_F14);
    engine->RegisterEnumValue("Key", "KEY_F15", KEY_F15);
    engine->RegisterEnumValue("Key", "KEY_F16", KEY_F16);
    engine->RegisterEnumValue("Key", "KEY_F17", KEY_F17);
    engine->RegisterEnumValue("Key", "KEY_F18", KEY_F18);
    engine->RegisterEnumValue("Key", "KEY_F19", KEY_F19);
    engine->RegisterEnumValue("Key", "KEY_F20", KEY_F20);
    engine->RegisterEnumValue("Key", "KEY_F21", KEY_F21);
    engine->RegisterEnumValue("Key", "KEY_F22", KEY_F22);
    engine->RegisterEnumValue("Key", "KEY_F23", KEY_F23);
    engine->RegisterEnumValue("Key", "KEY_F24", KEY_F24);
    engine->RegisterEnumValue("Key", "KEY_NUMLOCKCLEAR", KEY_NUMLOCKCLEAR);
    engine->RegisterEnumValue("Key", "KEY_SCROLLLOCK", KEY_SCROLLLOCK);
    engine->RegisterEnumValue("Key", "KEY_LSHIFT", KEY_LSHIFT);
    engine->RegisterEnumValue("Key", "KEY_RSHIFT", KEY_RSHIFT);
    engine->RegisterEnumValue("Key", "KEY_LCTRL", KEY_LCTRL);
    engine->RegisterEnumValue("Key", "KEY_RCTRL", KEY_RCTRL);
    engine->RegisterEnumValue("Key", "KEY_LALT", KEY_LALT);
    engine->RegisterEnumValue("Key", "KEY_RALT", KEY_RALT);
    engine->RegisterEnumValue("Key", "KEY_AC_BACK", KEY_AC_BACK);
    engine->RegisterEnumValue("Key", "KEY_AC_BOOKMARKS", KEY_AC_BOOKMARKS);
    engine->RegisterEnumValue("Key", "KEY_AC_FORWARD", KEY_AC_FORWARD);
    engine->RegisterEnumValue("Key", "KEY_AC_HOME", KEY_AC_HOME);
    engine->RegisterEnumValue("Key", "KEY_AC_REFRESH", KEY_AC_REFRESH);
    engine->RegisterEnumValue("Key", "KEY_AC_SEARCH", KEY_AC_SEARCH);
    engine->RegisterEnumValue("Key", "KEY_AC_STOP", KEY_AC_STOP);
    engine->RegisterEnumValue("Key", "KEY_AGAIN", KEY_AGAIN);
    engine->RegisterEnumValue("Key", "KEY_ALTERASE", KEY_ALTERASE);
    engine->RegisterEnumValue("Key", "KEY_AMPERSAND", KEY_AMPERSAND);
    engine->RegisterEnumValue("Key", "KEY_ASTERISK", KEY_ASTERISK);
    engine->RegisterEnumValue("Key", "KEY_AT", KEY_AT);
    engine->RegisterEnumValue("Key", "KEY_AUDIOMUTE", KEY_AUDIOMUTE);
    engine->RegisterEnumValue("Key", "KEY_AUDIONEXT", KEY_AUDIONEXT);
    engine->RegisterEnumValue("Key", "KEY_AUDIOPLAY", KEY_AUDIOPLAY);
    engine->RegisterEnumValue("Key", "KEY_AUDIOPREV", KEY_AUDIOPREV);
    engine->RegisterEnumValue("Key", "KEY_AUDIOSTOP", KEY_AUDIOSTOP);
    engine->RegisterEnumValue("Key", "KEY_BACKQUOTE", KEY_BACKQUOTE);
    engine->RegisterEnumValue("Key", "KEY_BACKSLASH", KEY_BACKSLASH);
    engine->RegisterEnumValue("Key", "KEY_BRIGHTNESSDOWN", KEY_BRIGHTNESSDOWN);
    engine->RegisterEnumValue("Key", "KEY_BRIGHTNESSUP", KEY_BRIGHTNESSUP);
    engine->RegisterEnumValue("Key", "KEY_CALCULATOR", KEY_CALCULATOR);
    engine->RegisterEnumValue("Key", "KEY_CANCEL", KEY_CANCEL);
    engine->RegisterEnumValue("Key", "KEY_CARET", KEY_CARET);
    engine->RegisterEnumValue("Key", "KEY_CLEAR", KEY_CLEAR);
    engine->RegisterEnumValue("Key", "KEY_CLEARAGAIN", KEY_CLEARAGAIN);
    engine->RegisterEnumValue("Key", "KEY_COLON", KEY_COLON);
    engine->RegisterEnumValue("Key", "KEY_COMMA", KEY_COMMA);
    engine->RegisterEnumValue("Key", "KEY_COMPUTER", KEY_COMPUTER);
    engine->RegisterEnumValue("Key", "KEY_COPY", KEY_COPY);
    engine->RegisterEnumValue("Key", "KEY_CRSEL", KEY_CRSEL);
    engine->RegisterEnumValue("Key", "KEY_CURRENCYSUBUNIT", KEY_CURRENCYSUBUNIT);
    engine->RegisterEnumValue("Key", "KEY_CURRENCYUNIT", KEY_CURRENCYUNIT);
    engine->RegisterEnumValue("Key", "KEY_CUT", KEY_CUT);
    engine->RegisterEnumValue("Key", "KEY_DECIMALSEPARATOR", KEY_DECIMALSEPARATOR);
    engine->RegisterEnumValue("Key", "KEY_DISPLAYSWITCH", KEY_DISPLAYSWITCH);
    engine->RegisterEnumValue("Key", "KEY_DOLLAR", KEY_DOLLAR);
    engine->RegisterEnumValue("Key", "KEY_EJECT", KEY_EJECT);
    engine->RegisterEnumValue("Key", "KEY_EQUALS", KEY_EQUALS);
    engine->RegisterEnumValue("Key", "KEY_EXCLAIM", KEY_EXCLAIM);
    engine->RegisterEnumValue("Key", "KEY_EXSEL", KEY_EXSEL);
    engine->RegisterEnumValue("Key", "KEY_FIND", KEY_FIND);
    engine->RegisterEnumValue("Key", "KEY_GREATER", KEY_GREATER);
    engine->RegisterEnumValue("Key", "KEY_HASH", KEY_HASH);
    engine->RegisterEnumValue("Key", "KEY_HELP", KEY_HELP);
    engine->RegisterEnumValue("Key", "KEY_KBDILLUMDOWN", KEY_KBDILLUMDOWN);
    engine->RegisterEnumValue("Key", "KEY_KBDILLUMTOGGLE", KEY_KBDILLUMTOGGLE);
    engine->RegisterEnumValue("Key", "KEY_KBDILLUMUP", KEY_KBDILLUMUP);
    engine->RegisterEnumValue("Key", "KEY_KP_00", KEY_KP_00);
    engine->RegisterEnumValue("Key", "KEY_KP_000", KEY_KP_000);
    engine->RegisterEnumValue("Key", "KEY_KP_A", KEY_KP_A);
    engine->RegisterEnumValue("Key", "KEY_KP_AMPERSAND", KEY_KP_AMPERSAND);
    engine->RegisterEnumValue("Key", "KEY_KP_AT", KEY_KP_AT);
    engine->RegisterEnumValue("Key", "KEY_KP_B", KEY_KP_B);
    engine->RegisterEnumValue("Key", "KEY_KP_BACKSPACE", KEY_KP_BACKSPACE);
    engine->RegisterEnumValue("Key", "KEY_KP_BINARY", KEY_KP_BINARY);
    engine->RegisterEnumValue("Key", "KEY_KP_C", KEY_KP_C);
    engine->RegisterEnumValue("Key", "KEY_KP_CLEAR", KEY_KP_CLEAR);
    engine->RegisterEnumValue("Key", "KEY_KP_CLEARENTRY", KEY_KP_CLEARENTRY);
    engine->RegisterEnumValue("Key", "KEY_KP_COLON", KEY_KP_COLON);
    engine->RegisterEnumValue("Key", "KEY_KP_COMMA", KEY_KP_COMMA);
    engine->RegisterEnumValue("Key", "KEY_KP_D", KEY_KP_D);
    engine->RegisterEnumValue("Key", "KEY_KP_DBLAMPERSAND", KEY_KP_DBLAMPERSAND);
    engine->RegisterEnumValue("Key", "KEY_KP_DBLVERTICALBAR", KEY_KP_DBLVERTICALBAR);
    engine->RegisterEnumValue("Key", "KEY_KP_DECIMAL", KEY_KP_DECIMAL);
    engine->RegisterEnumValue("Key", "KEY_KP_E", KEY_KP_E);
    engine->RegisterEnumValue("Key", "KEY_KP_EQUALS", KEY_KP_EQUALS);
    engine->RegisterEnumValue("Key", "KEY_KP_EQUALSAS400", KEY_KP_EQUALSAS400);
    engine->RegisterEnumValue("Key", "KEY_KP_EXCLAM", KEY_KP_EXCLAM);
    engine->RegisterEnumValue("Key", "KEY_KP_F", KEY_KP_F);
    engine->RegisterEnumValue("Key", "KEY_KP_GREATER", KEY_KP_GREATER);
    engine->RegisterEnumValue("Key", "KEY_KP_HASH", KEY_KP_HASH);
    engine->RegisterEnumValue("Key", "KEY_KP_HEXADECIMAL", KEY_KP_HEXADECIMAL);
    engine->RegisterEnumValue("Key", "KEY_KP_LEFTBRACE", KEY_KP_LEFTBRACE);
    engine->RegisterEnumValue("Key", "KEY_KP_LEFTPAREN", KEY_KP_LEFTPAREN);
    engine->RegisterEnumValue("Key", "KEY_KP_LESS", KEY_KP_LESS);
    engine->RegisterEnumValue("Key", "KEY_KP_MEMADD", KEY_KP_MEMADD);
    engine->RegisterEnumValue("Key", "KEY_KP_MEMCLEAR", KEY_KP_MEMCLEAR);
    engine->RegisterEnumValue("Key", "KEY_KP_MEMDIVIDE", KEY_KP_MEMDIVIDE);
    engine->RegisterEnumValue("Key", "KEY_KP_MEMMULTIPLY", KEY_KP_MEMMULTIPLY);
    engine->RegisterEnumValue("Key", "KEY_KP_MEMRECALL", KEY_KP_MEMRECALL);
    engine->RegisterEnumValue("Key", "KEY_KP_MEMSTORE", KEY_KP_MEMSTORE);
    engine->RegisterEnumValue("Key", "KEY_KP_MEMSUBTRACT", KEY_KP_MEMSUBTRACT);
    engine->RegisterEnumValue("Key", "KEY_KP_OCTAL", KEY_KP_OCTAL);
    engine->RegisterEnumValue("Key", "KEY_KP_PERCENT", KEY_KP_PERCENT);
    engine->RegisterEnumValue("Key", "KEY_KP_PLUSMINUS", KEY_KP_PLUSMINUS);
    engine->RegisterEnumValue("Key", "KEY_KP_POWER", KEY_KP_POWER);
    engine->RegisterEnumValue("Key", "KEY_KP_RIGHTBRACE", KEY_KP_RIGHTBRACE);
    engine->RegisterEnumValue("Key", "KEY_KP_RIGHTPAREN", KEY_KP_RIGHTPAREN);
    engine->RegisterEnumValue("Key", "KEY_KP_SPACE", KEY_KP_SPACE);
    engine->RegisterEnumValue("Key", "KEY_KP_TAB", KEY_KP_TAB);
    engine->RegisterEnumValue("Key", "KEY_KP_VERTICALBAR", KEY_KP_VERTICALBAR);
    engine->RegisterEnumValue("Key", "KEY_KP_XOR", KEY_KP_XOR);
    engine->RegisterEnumValue("Key", "KEY_LEFTBRACKET", KEY_LEFTBRACKET);
    engine->RegisterEnumValue("Key", "KEY_LEFTPAREN", KEY_LEFTPAREN);
    engine->RegisterEnumValue("Key", "KEY_LESS", KEY_LESS);
    engine->RegisterEnumValue("Key", "KEY_MAIL", KEY_MAIL);
    engine->RegisterEnumValue("Key", "KEY_MEDIASELECT", KEY_MEDIASELECT);
    engine->RegisterEnumValue("Key", "KEY_MENU", KEY_MENU);
    engine->RegisterEnumValue("Key", "KEY_MINUS", KEY_MINUS);
    engine->RegisterEnumValue("Key", "KEY_MODE", KEY_MODE);
    engine->RegisterEnumValue("Key", "KEY_MUTE", KEY_MUTE);
    engine->RegisterEnumValue("Key", "KEY_OPER", KEY_OPER);
    engine->RegisterEnumValue("Key", "KEY_OUT", KEY_OUT);
    engine->RegisterEnumValue("Key", "KEY_PASTE", KEY_PASTE);
    engine->RegisterEnumValue("Key", "KEY_PERCENT", KEY_PERCENT);
    engine->RegisterEnumValue("Key", "KEY_PERIOD", KEY_PERIOD);
    engine->RegisterEnumValue("Key", "KEY_PLUS", KEY_PLUS);
    engine->RegisterEnumValue("Key", "KEY_POWER", KEY_POWER);
    engine->RegisterEnumValue("Key", "KEY_PRIOR", KEY_PRIOR);
    engine->RegisterEnumValue("Key", "KEY_QUESTION", KEY_QUESTION);
    engine->RegisterEnumValue("Key", "KEY_QUOTE", KEY_QUOTE);
    engine->RegisterEnumValue("Key", "KEY_QUOTEDBL", KEY_QUOTEDBL);
    engine->RegisterEnumValue("Key", "KEY_RIGHTBRACKET", KEY_RIGHTBRACKET);
    engine->RegisterEnumValue("Key", "KEY_RIGHTPAREN", KEY_RIGHTPAREN);
    engine->RegisterEnumValue("Key", "KEY_SEMICOLON", KEY_SEMICOLON);
    engine->RegisterEnumValue("Key", "KEY_SEPARATOR", KEY_SEPARATOR);
    engine->RegisterEnumValue("Key", "KEY_SLASH", KEY_SLASH);
    engine->RegisterEnumValue("Key", "KEY_SLEEP", KEY_SLEEP);
    engine->RegisterEnumValue("Key", "KEY_STOP", KEY_STOP);
    engine->RegisterEnumValue("Key", "KEY_SYSREQ", KEY_SYSREQ);
    engine->RegisterEnumValue("Key", "KEY_THOUSANDSSEPARATOR", KEY_THOUSANDSSEPARATOR);
    engine->RegisterEnumValue("Key", "KEY_UNDERSCORE", KEY_UNDERSCORE);
    engine->RegisterEnumValue("Key", "KEY_UNDO", KEY_UNDO);
    engine->RegisterEnumValue("Key", "KEY_VOLUMEDOWN", KEY_VOLUMEDOWN);
    engine->RegisterEnumValue("Key", "KEY_VOLUMEUP", KEY_VOLUMEUP);
    engine->RegisterEnumValue("Key", "KEY_WWW", KEY_WWW);

    // enum LayoutMode | File: ../UI/UIElement.h
    engine->RegisterEnum("LayoutMode");
    engine->RegisterEnumValue("LayoutMode", "LM_FREE", LM_FREE);
    engine->RegisterEnumValue("LayoutMode", "LM_HORIZONTAL", LM_HORIZONTAL);
    engine->RegisterEnumValue("LayoutMode", "LM_VERTICAL", LM_VERTICAL);

    // enum LegacyVertexElement | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("LegacyVertexElement");
    engine->RegisterEnumValue("LegacyVertexElement", "ELEMENT_POSITION", ELEMENT_POSITION);
    engine->RegisterEnumValue("LegacyVertexElement", "ELEMENT_NORMAL", ELEMENT_NORMAL);
    engine->RegisterEnumValue("LegacyVertexElement", "ELEMENT_COLOR", ELEMENT_COLOR);
    engine->RegisterEnumValue("LegacyVertexElement", "ELEMENT_TEXCOORD1", ELEMENT_TEXCOORD1);
    engine->RegisterEnumValue("LegacyVertexElement", "ELEMENT_TEXCOORD2", ELEMENT_TEXCOORD2);
    engine->RegisterEnumValue("LegacyVertexElement", "ELEMENT_CUBETEXCOORD1", ELEMENT_CUBETEXCOORD1);
    engine->RegisterEnumValue("LegacyVertexElement", "ELEMENT_CUBETEXCOORD2", ELEMENT_CUBETEXCOORD2);
    engine->RegisterEnumValue("LegacyVertexElement", "ELEMENT_TANGENT", ELEMENT_TANGENT);
    engine->RegisterEnumValue("LegacyVertexElement", "ELEMENT_BLENDWEIGHTS", ELEMENT_BLENDWEIGHTS);
    engine->RegisterEnumValue("LegacyVertexElement", "ELEMENT_BLENDINDICES", ELEMENT_BLENDINDICES);
    engine->RegisterEnumValue("LegacyVertexElement", "ELEMENT_INSTANCEMATRIX1", ELEMENT_INSTANCEMATRIX1);
    engine->RegisterEnumValue("LegacyVertexElement", "ELEMENT_INSTANCEMATRIX2", ELEMENT_INSTANCEMATRIX2);
    engine->RegisterEnumValue("LegacyVertexElement", "ELEMENT_INSTANCEMATRIX3", ELEMENT_INSTANCEMATRIX3);
    engine->RegisterEnumValue("LegacyVertexElement", "ELEMENT_OBJECTINDEX", ELEMENT_OBJECTINDEX);
    engine->RegisterEnumValue("LegacyVertexElement", "MAX_LEGACY_VERTEX_ELEMENTS", MAX_LEGACY_VERTEX_ELEMENTS);

    // enum LightPSVariation | File: ../Graphics/Renderer.h
    engine->RegisterEnum("LightPSVariation");
    engine->RegisterEnumValue("LightPSVariation", "LPS_NONE", LPS_NONE);
    engine->RegisterEnumValue("LightPSVariation", "LPS_SPOT", LPS_SPOT);
    engine->RegisterEnumValue("LightPSVariation", "LPS_POINT", LPS_POINT);
    engine->RegisterEnumValue("LightPSVariation", "LPS_POINTMASK", LPS_POINTMASK);
    engine->RegisterEnumValue("LightPSVariation", "LPS_SPEC", LPS_SPEC);
    engine->RegisterEnumValue("LightPSVariation", "LPS_SPOTSPEC", LPS_SPOTSPEC);
    engine->RegisterEnumValue("LightPSVariation", "LPS_POINTSPEC", LPS_POINTSPEC);
    engine->RegisterEnumValue("LightPSVariation", "LPS_POINTMASKSPEC", LPS_POINTMASKSPEC);
    engine->RegisterEnumValue("LightPSVariation", "LPS_SHADOW", LPS_SHADOW);
    engine->RegisterEnumValue("LightPSVariation", "LPS_SPOTSHADOW", LPS_SPOTSHADOW);
    engine->RegisterEnumValue("LightPSVariation", "LPS_POINTSHADOW", LPS_POINTSHADOW);
    engine->RegisterEnumValue("LightPSVariation", "LPS_POINTMASKSHADOW", LPS_POINTMASKSHADOW);
    engine->RegisterEnumValue("LightPSVariation", "LPS_SHADOWSPEC", LPS_SHADOWSPEC);
    engine->RegisterEnumValue("LightPSVariation", "LPS_SPOTSHADOWSPEC", LPS_SPOTSHADOWSPEC);
    engine->RegisterEnumValue("LightPSVariation", "LPS_POINTSHADOWSPEC", LPS_POINTSHADOWSPEC);
    engine->RegisterEnumValue("LightPSVariation", "LPS_POINTMASKSHADOWSPEC", LPS_POINTMASKSHADOWSPEC);
    engine->RegisterEnumValue("LightPSVariation", "MAX_LIGHT_PS_VARIATIONS", MAX_LIGHT_PS_VARIATIONS);

    // enum LightType | File: ../Graphics/Light.h
    engine->RegisterEnum("LightType");
    engine->RegisterEnumValue("LightType", "LIGHT_DIRECTIONAL", LIGHT_DIRECTIONAL);
    engine->RegisterEnumValue("LightType", "LIGHT_SPOT", LIGHT_SPOT);
    engine->RegisterEnumValue("LightType", "LIGHT_POINT", LIGHT_POINT);

    // enum LightVSVariation | File: ../Graphics/Renderer.h
    engine->RegisterEnum("LightVSVariation");
    engine->RegisterEnumValue("LightVSVariation", "LVS_DIR", LVS_DIR);
    engine->RegisterEnumValue("LightVSVariation", "LVS_SPOT", LVS_SPOT);
    engine->RegisterEnumValue("LightVSVariation", "LVS_POINT", LVS_POINT);
    engine->RegisterEnumValue("LightVSVariation", "LVS_SHADOW", LVS_SHADOW);
    engine->RegisterEnumValue("LightVSVariation", "LVS_SPOTSHADOW", LVS_SPOTSHADOW);
    engine->RegisterEnumValue("LightVSVariation", "LVS_POINTSHADOW", LVS_POINTSHADOW);
    engine->RegisterEnumValue("LightVSVariation", "LVS_SHADOWNORMALOFFSET", LVS_SHADOWNORMALOFFSET);
    engine->RegisterEnumValue("LightVSVariation", "LVS_SPOTSHADOWNORMALOFFSET", LVS_SPOTSHADOWNORMALOFFSET);
    engine->RegisterEnumValue("LightVSVariation", "LVS_POINTSHADOWNORMALOFFSET", LVS_POINTSHADOWNORMALOFFSET);
    engine->RegisterEnumValue("LightVSVariation", "MAX_LIGHT_VS_VARIATIONS", MAX_LIGHT_VS_VARIATIONS);

    // enum LoadMode | File: ../Scene/Scene.h
    engine->RegisterEnum("LoadMode");
    engine->RegisterEnumValue("LoadMode", "LOAD_RESOURCES_ONLY", LOAD_RESOURCES_ONLY);
    engine->RegisterEnumValue("LoadMode", "LOAD_SCENE", LOAD_SCENE);
    engine->RegisterEnumValue("LoadMode", "LOAD_SCENE_AND_RESOURCES", LOAD_SCENE_AND_RESOURCES);

    // enum LockState | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("LockState");
    engine->RegisterEnumValue("LockState", "LOCK_NONE", LOCK_NONE);
    engine->RegisterEnumValue("LockState", "LOCK_HARDWARE", LOCK_HARDWARE);
    engine->RegisterEnumValue("LockState", "LOCK_SHADOW", LOCK_SHADOW);
    engine->RegisterEnumValue("LockState", "LOCK_SCRATCH", LOCK_SCRATCH);

    // enum class LogicComponentEvents | File: ../Scene/LogicComponent.h
    engine->RegisterTypedef("LogicComponentEvents", "int");
    engine->SetDefaultNamespace("LogicComponentEvents");
    engine->RegisterGlobalProperty("const int None", (void*)&LogicComponentEvents_None);
    engine->RegisterGlobalProperty("const int Update", (void*)&LogicComponentEvents_Update);
    engine->RegisterGlobalProperty("const int PostUpdate", (void*)&LogicComponentEvents_PostUpdate);
    engine->RegisterGlobalProperty("const int FixedUpdate", (void*)&LogicComponentEvents_FixedUpdate);
    engine->RegisterGlobalProperty("const int FixedPostUpdate", (void*)&LogicComponentEvents_FixedPostUpdate);
    engine->RegisterGlobalProperty("const int All", (void*)&LogicComponentEvents_All);
    engine->SetDefaultNamespace("");

    // enum MaterialQuality : u32 | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterTypedef("MaterialQuality", "uint");
    engine->RegisterGlobalProperty("const uint QUALITY_LOW", (void*)&MaterialQuality_QUALITY_LOW);
    engine->RegisterGlobalProperty("const uint QUALITY_MEDIUM", (void*)&MaterialQuality_QUALITY_MEDIUM);
    engine->RegisterGlobalProperty("const uint QUALITY_HIGH", (void*)&MaterialQuality_QUALITY_HIGH);
    engine->RegisterGlobalProperty("const uint QUALITY_MAX", (void*)&MaterialQuality_QUALITY_MAX);

    // enum MouseButton | File: ../Input/InputConstants.h
    engine->RegisterEnum("MouseButton");
    engine->RegisterEnumValue("MouseButton", "MOUSEB_NONE", MOUSEB_NONE);
    engine->RegisterEnumValue("MouseButton", "MOUSEB_LEFT", MOUSEB_LEFT);
    engine->RegisterEnumValue("MouseButton", "MOUSEB_MIDDLE", MOUSEB_MIDDLE);
    engine->RegisterEnumValue("MouseButton", "MOUSEB_RIGHT", MOUSEB_RIGHT);
    engine->RegisterEnumValue("MouseButton", "MOUSEB_X1", MOUSEB_X1);
    engine->RegisterEnumValue("MouseButton", "MOUSEB_X2", MOUSEB_X2);
    engine->RegisterEnumValue("MouseButton", "MOUSEB_ANY", MOUSEB_ANY);

    // URHO3D_FLAGSET(MouseButton, MouseButtonFlags) | File: ../Input/InputConstants.h
    engine->RegisterTypedef("MouseButtonFlags", "int");

    // enum MouseMode | File: ../Input/Input.h
    engine->RegisterEnum("MouseMode");
    engine->RegisterEnumValue("MouseMode", "MM_ABSOLUTE", MM_ABSOLUTE);
    engine->RegisterEnumValue("MouseMode", "MM_RELATIVE", MM_RELATIVE);
    engine->RegisterEnumValue("MouseMode", "MM_WRAP", MM_WRAP);
    engine->RegisterEnumValue("MouseMode", "MM_FREE", MM_FREE);
    engine->RegisterEnumValue("MouseMode", "MM_INVALID", MM_INVALID);

    // enum Orientation | File: ../UI/UIElement.h
    engine->RegisterEnum("Orientation");
    engine->RegisterEnumValue("Orientation", "O_HORIZONTAL", O_HORIZONTAL);
    engine->RegisterEnumValue("Orientation", "O_VERTICAL", O_VERTICAL);

    // enum PassLightingMode | File: ../Graphics/Technique.h
    engine->RegisterEnum("PassLightingMode");
    engine->RegisterEnumValue("PassLightingMode", "LIGHTING_UNLIT", LIGHTING_UNLIT);
    engine->RegisterEnumValue("PassLightingMode", "LIGHTING_PERVERTEX", LIGHTING_PERVERTEX);
    engine->RegisterEnumValue("PassLightingMode", "LIGHTING_PERPIXEL", LIGHTING_PERPIXEL);

    // enum PrimitiveType | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("PrimitiveType");
    engine->RegisterEnumValue("PrimitiveType", "TRIANGLE_LIST", TRIANGLE_LIST);
    engine->RegisterEnumValue("PrimitiveType", "LINE_LIST", LINE_LIST);
    engine->RegisterEnumValue("PrimitiveType", "POINT_LIST", POINT_LIST);
    engine->RegisterEnumValue("PrimitiveType", "TRIANGLE_STRIP", TRIANGLE_STRIP);
    engine->RegisterEnumValue("PrimitiveType", "LINE_STRIP", LINE_STRIP);
    engine->RegisterEnumValue("PrimitiveType", "TRIANGLE_FAN", TRIANGLE_FAN);

    // enum Qualifier | File: ../Input/InputConstants.h
    engine->RegisterEnum("Qualifier");
    engine->RegisterEnumValue("Qualifier", "QUAL_NONE", QUAL_NONE);
    engine->RegisterEnumValue("Qualifier", "QUAL_SHIFT", QUAL_SHIFT);
    engine->RegisterEnumValue("Qualifier", "QUAL_CTRL", QUAL_CTRL);
    engine->RegisterEnumValue("Qualifier", "QUAL_ALT", QUAL_ALT);
    engine->RegisterEnumValue("Qualifier", "QUAL_ANY", QUAL_ANY);

    // URHO3D_FLAGSET(Qualifier, QualifierFlags) | File: ../Input/InputConstants.h
    engine->RegisterTypedef("QualifierFlags", "int");

    // enum RayQueryLevel | File: ../Graphics/OctreeQuery.h
    engine->RegisterEnum("RayQueryLevel");
    engine->RegisterEnumValue("RayQueryLevel", "RAY_AABB", RAY_AABB);
    engine->RegisterEnumValue("RayQueryLevel", "RAY_OBB", RAY_OBB);
    engine->RegisterEnumValue("RayQueryLevel", "RAY_TRIANGLE", RAY_TRIANGLE);
    engine->RegisterEnumValue("RayQueryLevel", "RAY_TRIANGLE_UV", RAY_TRIANGLE_UV);

    // enum RenderCommandSortMode | File: ../Graphics/RenderPath.h
    engine->RegisterEnum("RenderCommandSortMode");
    engine->RegisterEnumValue("RenderCommandSortMode", "SORT_FRONTTOBACK", SORT_FRONTTOBACK);
    engine->RegisterEnumValue("RenderCommandSortMode", "SORT_BACKTOFRONT", SORT_BACKTOFRONT);

    // enum RenderCommandType | File: ../Graphics/RenderPath.h
    engine->RegisterEnum("RenderCommandType");
    engine->RegisterEnumValue("RenderCommandType", "CMD_NONE", CMD_NONE);
    engine->RegisterEnumValue("RenderCommandType", "CMD_CLEAR", CMD_CLEAR);
    engine->RegisterEnumValue("RenderCommandType", "CMD_SCENEPASS", CMD_SCENEPASS);
    engine->RegisterEnumValue("RenderCommandType", "CMD_QUAD", CMD_QUAD);
    engine->RegisterEnumValue("RenderCommandType", "CMD_FORWARDLIGHTS", CMD_FORWARDLIGHTS);
    engine->RegisterEnumValue("RenderCommandType", "CMD_LIGHTVOLUMES", CMD_LIGHTVOLUMES);
    engine->RegisterEnumValue("RenderCommandType", "CMD_RENDERUI", CMD_RENDERUI);
    engine->RegisterEnumValue("RenderCommandType", "CMD_SENDEVENT", CMD_SENDEVENT);

    // enum RenderSurfaceUpdateMode | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("RenderSurfaceUpdateMode");
    engine->RegisterEnumValue("RenderSurfaceUpdateMode", "SURFACE_MANUALUPDATE", SURFACE_MANUALUPDATE);
    engine->RegisterEnumValue("RenderSurfaceUpdateMode", "SURFACE_UPDATEVISIBLE", SURFACE_UPDATEVISIBLE);
    engine->RegisterEnumValue("RenderSurfaceUpdateMode", "SURFACE_UPDATEALWAYS", SURFACE_UPDATEALWAYS);

    // enum RenderTargetSizeMode | File: ../Graphics/RenderPath.h
    engine->RegisterEnum("RenderTargetSizeMode");
    engine->RegisterEnumValue("RenderTargetSizeMode", "SIZE_ABSOLUTE", SIZE_ABSOLUTE);
    engine->RegisterEnumValue("RenderTargetSizeMode", "SIZE_VIEWPORTDIVISOR", SIZE_VIEWPORTDIVISOR);
    engine->RegisterEnumValue("RenderTargetSizeMode", "SIZE_VIEWPORTMULTIPLIER", SIZE_VIEWPORTMULTIPLIER);

    // enum ResourceRequest | File: ../Resource/ResourceCache.h
    engine->RegisterEnum("ResourceRequest");
    engine->RegisterEnumValue("ResourceRequest", "RESOURCE_CHECKEXISTS", RESOURCE_CHECKEXISTS);
    engine->RegisterEnumValue("ResourceRequest", "RESOURCE_GETFILE", RESOURCE_GETFILE);

    // enum Scancode | File: ../Input/InputConstants.h
    engine->RegisterEnum("Scancode");
    engine->RegisterEnumValue("Scancode", "SCANCODE_UNKNOWN", SCANCODE_UNKNOWN);
    engine->RegisterEnumValue("Scancode", "SCANCODE_CTRL", SCANCODE_CTRL);
    engine->RegisterEnumValue("Scancode", "SCANCODE_SHIFT", SCANCODE_SHIFT);
    engine->RegisterEnumValue("Scancode", "SCANCODE_ALT", SCANCODE_ALT);
    engine->RegisterEnumValue("Scancode", "SCANCODE_GUI", SCANCODE_GUI);
    engine->RegisterEnumValue("Scancode", "SCANCODE_A", SCANCODE_A);
    engine->RegisterEnumValue("Scancode", "SCANCODE_B", SCANCODE_B);
    engine->RegisterEnumValue("Scancode", "SCANCODE_C", SCANCODE_C);
    engine->RegisterEnumValue("Scancode", "SCANCODE_D", SCANCODE_D);
    engine->RegisterEnumValue("Scancode", "SCANCODE_E", SCANCODE_E);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F", SCANCODE_F);
    engine->RegisterEnumValue("Scancode", "SCANCODE_G", SCANCODE_G);
    engine->RegisterEnumValue("Scancode", "SCANCODE_H", SCANCODE_H);
    engine->RegisterEnumValue("Scancode", "SCANCODE_I", SCANCODE_I);
    engine->RegisterEnumValue("Scancode", "SCANCODE_J", SCANCODE_J);
    engine->RegisterEnumValue("Scancode", "SCANCODE_K", SCANCODE_K);
    engine->RegisterEnumValue("Scancode", "SCANCODE_L", SCANCODE_L);
    engine->RegisterEnumValue("Scancode", "SCANCODE_M", SCANCODE_M);
    engine->RegisterEnumValue("Scancode", "SCANCODE_N", SCANCODE_N);
    engine->RegisterEnumValue("Scancode", "SCANCODE_O", SCANCODE_O);
    engine->RegisterEnumValue("Scancode", "SCANCODE_P", SCANCODE_P);
    engine->RegisterEnumValue("Scancode", "SCANCODE_Q", SCANCODE_Q);
    engine->RegisterEnumValue("Scancode", "SCANCODE_R", SCANCODE_R);
    engine->RegisterEnumValue("Scancode", "SCANCODE_S", SCANCODE_S);
    engine->RegisterEnumValue("Scancode", "SCANCODE_T", SCANCODE_T);
    engine->RegisterEnumValue("Scancode", "SCANCODE_U", SCANCODE_U);
    engine->RegisterEnumValue("Scancode", "SCANCODE_V", SCANCODE_V);
    engine->RegisterEnumValue("Scancode", "SCANCODE_W", SCANCODE_W);
    engine->RegisterEnumValue("Scancode", "SCANCODE_X", SCANCODE_X);
    engine->RegisterEnumValue("Scancode", "SCANCODE_Y", SCANCODE_Y);
    engine->RegisterEnumValue("Scancode", "SCANCODE_Z", SCANCODE_Z);
    engine->RegisterEnumValue("Scancode", "SCANCODE_1", SCANCODE_1);
    engine->RegisterEnumValue("Scancode", "SCANCODE_2", SCANCODE_2);
    engine->RegisterEnumValue("Scancode", "SCANCODE_3", SCANCODE_3);
    engine->RegisterEnumValue("Scancode", "SCANCODE_4", SCANCODE_4);
    engine->RegisterEnumValue("Scancode", "SCANCODE_5", SCANCODE_5);
    engine->RegisterEnumValue("Scancode", "SCANCODE_6", SCANCODE_6);
    engine->RegisterEnumValue("Scancode", "SCANCODE_7", SCANCODE_7);
    engine->RegisterEnumValue("Scancode", "SCANCODE_8", SCANCODE_8);
    engine->RegisterEnumValue("Scancode", "SCANCODE_9", SCANCODE_9);
    engine->RegisterEnumValue("Scancode", "SCANCODE_0", SCANCODE_0);
    engine->RegisterEnumValue("Scancode", "SCANCODE_RETURN", SCANCODE_RETURN);
    engine->RegisterEnumValue("Scancode", "SCANCODE_ESCAPE", SCANCODE_ESCAPE);
    engine->RegisterEnumValue("Scancode", "SCANCODE_BACKSPACE", SCANCODE_BACKSPACE);
    engine->RegisterEnumValue("Scancode", "SCANCODE_TAB", SCANCODE_TAB);
    engine->RegisterEnumValue("Scancode", "SCANCODE_SPACE", SCANCODE_SPACE);
    engine->RegisterEnumValue("Scancode", "SCANCODE_MINUS", SCANCODE_MINUS);
    engine->RegisterEnumValue("Scancode", "SCANCODE_EQUALS", SCANCODE_EQUALS);
    engine->RegisterEnumValue("Scancode", "SCANCODE_LEFTBRACKET", SCANCODE_LEFTBRACKET);
    engine->RegisterEnumValue("Scancode", "SCANCODE_RIGHTBRACKET", SCANCODE_RIGHTBRACKET);
    engine->RegisterEnumValue("Scancode", "SCANCODE_BACKSLASH", SCANCODE_BACKSLASH);
    engine->RegisterEnumValue("Scancode", "SCANCODE_NONUSHASH", SCANCODE_NONUSHASH);
    engine->RegisterEnumValue("Scancode", "SCANCODE_SEMICOLON", SCANCODE_SEMICOLON);
    engine->RegisterEnumValue("Scancode", "SCANCODE_APOSTROPHE", SCANCODE_APOSTROPHE);
    engine->RegisterEnumValue("Scancode", "SCANCODE_GRAVE", SCANCODE_GRAVE);
    engine->RegisterEnumValue("Scancode", "SCANCODE_COMMA", SCANCODE_COMMA);
    engine->RegisterEnumValue("Scancode", "SCANCODE_PERIOD", SCANCODE_PERIOD);
    engine->RegisterEnumValue("Scancode", "SCANCODE_SLASH", SCANCODE_SLASH);
    engine->RegisterEnumValue("Scancode", "SCANCODE_CAPSLOCK", SCANCODE_CAPSLOCK);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F1", SCANCODE_F1);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F2", SCANCODE_F2);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F3", SCANCODE_F3);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F4", SCANCODE_F4);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F5", SCANCODE_F5);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F6", SCANCODE_F6);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F7", SCANCODE_F7);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F8", SCANCODE_F8);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F9", SCANCODE_F9);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F10", SCANCODE_F10);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F11", SCANCODE_F11);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F12", SCANCODE_F12);
    engine->RegisterEnumValue("Scancode", "SCANCODE_PRINTSCREEN", SCANCODE_PRINTSCREEN);
    engine->RegisterEnumValue("Scancode", "SCANCODE_SCROLLLOCK", SCANCODE_SCROLLLOCK);
    engine->RegisterEnumValue("Scancode", "SCANCODE_PAUSE", SCANCODE_PAUSE);
    engine->RegisterEnumValue("Scancode", "SCANCODE_INSERT", SCANCODE_INSERT);
    engine->RegisterEnumValue("Scancode", "SCANCODE_HOME", SCANCODE_HOME);
    engine->RegisterEnumValue("Scancode", "SCANCODE_PAGEUP", SCANCODE_PAGEUP);
    engine->RegisterEnumValue("Scancode", "SCANCODE_DELETE", SCANCODE_DELETE);
    engine->RegisterEnumValue("Scancode", "SCANCODE_END", SCANCODE_END);
    engine->RegisterEnumValue("Scancode", "SCANCODE_PAGEDOWN", SCANCODE_PAGEDOWN);
    engine->RegisterEnumValue("Scancode", "SCANCODE_RIGHT", SCANCODE_RIGHT);
    engine->RegisterEnumValue("Scancode", "SCANCODE_LEFT", SCANCODE_LEFT);
    engine->RegisterEnumValue("Scancode", "SCANCODE_DOWN", SCANCODE_DOWN);
    engine->RegisterEnumValue("Scancode", "SCANCODE_UP", SCANCODE_UP);
    engine->RegisterEnumValue("Scancode", "SCANCODE_NUMLOCKCLEAR", SCANCODE_NUMLOCKCLEAR);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_DIVIDE", SCANCODE_KP_DIVIDE);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_MULTIPLY", SCANCODE_KP_MULTIPLY);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_MINUS", SCANCODE_KP_MINUS);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_PLUS", SCANCODE_KP_PLUS);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_ENTER", SCANCODE_KP_ENTER);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_1", SCANCODE_KP_1);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_2", SCANCODE_KP_2);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_3", SCANCODE_KP_3);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_4", SCANCODE_KP_4);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_5", SCANCODE_KP_5);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_6", SCANCODE_KP_6);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_7", SCANCODE_KP_7);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_8", SCANCODE_KP_8);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_9", SCANCODE_KP_9);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_0", SCANCODE_KP_0);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_PERIOD", SCANCODE_KP_PERIOD);
    engine->RegisterEnumValue("Scancode", "SCANCODE_NONUSBACKSLASH", SCANCODE_NONUSBACKSLASH);
    engine->RegisterEnumValue("Scancode", "SCANCODE_APPLICATION", SCANCODE_APPLICATION);
    engine->RegisterEnumValue("Scancode", "SCANCODE_POWER", SCANCODE_POWER);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_EQUALS", SCANCODE_KP_EQUALS);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F13", SCANCODE_F13);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F14", SCANCODE_F14);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F15", SCANCODE_F15);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F16", SCANCODE_F16);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F17", SCANCODE_F17);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F18", SCANCODE_F18);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F19", SCANCODE_F19);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F20", SCANCODE_F20);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F21", SCANCODE_F21);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F22", SCANCODE_F22);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F23", SCANCODE_F23);
    engine->RegisterEnumValue("Scancode", "SCANCODE_F24", SCANCODE_F24);
    engine->RegisterEnumValue("Scancode", "SCANCODE_EXECUTE", SCANCODE_EXECUTE);
    engine->RegisterEnumValue("Scancode", "SCANCODE_HELP", SCANCODE_HELP);
    engine->RegisterEnumValue("Scancode", "SCANCODE_MENU", SCANCODE_MENU);
    engine->RegisterEnumValue("Scancode", "SCANCODE_SELECT", SCANCODE_SELECT);
    engine->RegisterEnumValue("Scancode", "SCANCODE_STOP", SCANCODE_STOP);
    engine->RegisterEnumValue("Scancode", "SCANCODE_AGAIN", SCANCODE_AGAIN);
    engine->RegisterEnumValue("Scancode", "SCANCODE_UNDO", SCANCODE_UNDO);
    engine->RegisterEnumValue("Scancode", "SCANCODE_CUT", SCANCODE_CUT);
    engine->RegisterEnumValue("Scancode", "SCANCODE_COPY", SCANCODE_COPY);
    engine->RegisterEnumValue("Scancode", "SCANCODE_PASTE", SCANCODE_PASTE);
    engine->RegisterEnumValue("Scancode", "SCANCODE_FIND", SCANCODE_FIND);
    engine->RegisterEnumValue("Scancode", "SCANCODE_MUTE", SCANCODE_MUTE);
    engine->RegisterEnumValue("Scancode", "SCANCODE_VOLUMEUP", SCANCODE_VOLUMEUP);
    engine->RegisterEnumValue("Scancode", "SCANCODE_VOLUMEDOWN", SCANCODE_VOLUMEDOWN);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_COMMA", SCANCODE_KP_COMMA);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_EQUALSAS400", SCANCODE_KP_EQUALSAS400);
    engine->RegisterEnumValue("Scancode", "SCANCODE_INTERNATIONAL1", SCANCODE_INTERNATIONAL1);
    engine->RegisterEnumValue("Scancode", "SCANCODE_INTERNATIONAL2", SCANCODE_INTERNATIONAL2);
    engine->RegisterEnumValue("Scancode", "SCANCODE_INTERNATIONAL3", SCANCODE_INTERNATIONAL3);
    engine->RegisterEnumValue("Scancode", "SCANCODE_INTERNATIONAL4", SCANCODE_INTERNATIONAL4);
    engine->RegisterEnumValue("Scancode", "SCANCODE_INTERNATIONAL5", SCANCODE_INTERNATIONAL5);
    engine->RegisterEnumValue("Scancode", "SCANCODE_INTERNATIONAL6", SCANCODE_INTERNATIONAL6);
    engine->RegisterEnumValue("Scancode", "SCANCODE_INTERNATIONAL7", SCANCODE_INTERNATIONAL7);
    engine->RegisterEnumValue("Scancode", "SCANCODE_INTERNATIONAL8", SCANCODE_INTERNATIONAL8);
    engine->RegisterEnumValue("Scancode", "SCANCODE_INTERNATIONAL9", SCANCODE_INTERNATIONAL9);
    engine->RegisterEnumValue("Scancode", "SCANCODE_LANG1", SCANCODE_LANG1);
    engine->RegisterEnumValue("Scancode", "SCANCODE_LANG2", SCANCODE_LANG2);
    engine->RegisterEnumValue("Scancode", "SCANCODE_LANG3", SCANCODE_LANG3);
    engine->RegisterEnumValue("Scancode", "SCANCODE_LANG4", SCANCODE_LANG4);
    engine->RegisterEnumValue("Scancode", "SCANCODE_LANG5", SCANCODE_LANG5);
    engine->RegisterEnumValue("Scancode", "SCANCODE_LANG6", SCANCODE_LANG6);
    engine->RegisterEnumValue("Scancode", "SCANCODE_LANG7", SCANCODE_LANG7);
    engine->RegisterEnumValue("Scancode", "SCANCODE_LANG8", SCANCODE_LANG8);
    engine->RegisterEnumValue("Scancode", "SCANCODE_LANG9", SCANCODE_LANG9);
    engine->RegisterEnumValue("Scancode", "SCANCODE_ALTERASE", SCANCODE_ALTERASE);
    engine->RegisterEnumValue("Scancode", "SCANCODE_SYSREQ", SCANCODE_SYSREQ);
    engine->RegisterEnumValue("Scancode", "SCANCODE_CANCEL", SCANCODE_CANCEL);
    engine->RegisterEnumValue("Scancode", "SCANCODE_CLEAR", SCANCODE_CLEAR);
    engine->RegisterEnumValue("Scancode", "SCANCODE_PRIOR", SCANCODE_PRIOR);
    engine->RegisterEnumValue("Scancode", "SCANCODE_RETURN2", SCANCODE_RETURN2);
    engine->RegisterEnumValue("Scancode", "SCANCODE_SEPARATOR", SCANCODE_SEPARATOR);
    engine->RegisterEnumValue("Scancode", "SCANCODE_OUT", SCANCODE_OUT);
    engine->RegisterEnumValue("Scancode", "SCANCODE_OPER", SCANCODE_OPER);
    engine->RegisterEnumValue("Scancode", "SCANCODE_CLEARAGAIN", SCANCODE_CLEARAGAIN);
    engine->RegisterEnumValue("Scancode", "SCANCODE_CRSEL", SCANCODE_CRSEL);
    engine->RegisterEnumValue("Scancode", "SCANCODE_EXSEL", SCANCODE_EXSEL);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_00", SCANCODE_KP_00);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_000", SCANCODE_KP_000);
    engine->RegisterEnumValue("Scancode", "SCANCODE_THOUSANDSSEPARATOR", SCANCODE_THOUSANDSSEPARATOR);
    engine->RegisterEnumValue("Scancode", "SCANCODE_DECIMALSEPARATOR", SCANCODE_DECIMALSEPARATOR);
    engine->RegisterEnumValue("Scancode", "SCANCODE_CURRENCYUNIT", SCANCODE_CURRENCYUNIT);
    engine->RegisterEnumValue("Scancode", "SCANCODE_CURRENCYSUBUNIT", SCANCODE_CURRENCYSUBUNIT);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_LEFTPAREN", SCANCODE_KP_LEFTPAREN);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_RIGHTPAREN", SCANCODE_KP_RIGHTPAREN);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_LEFTBRACE", SCANCODE_KP_LEFTBRACE);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_RIGHTBRACE", SCANCODE_KP_RIGHTBRACE);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_TAB", SCANCODE_KP_TAB);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_BACKSPACE", SCANCODE_KP_BACKSPACE);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_A", SCANCODE_KP_A);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_B", SCANCODE_KP_B);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_C", SCANCODE_KP_C);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_D", SCANCODE_KP_D);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_E", SCANCODE_KP_E);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_F", SCANCODE_KP_F);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_XOR", SCANCODE_KP_XOR);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_POWER", SCANCODE_KP_POWER);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_PERCENT", SCANCODE_KP_PERCENT);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_LESS", SCANCODE_KP_LESS);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_GREATER", SCANCODE_KP_GREATER);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_AMPERSAND", SCANCODE_KP_AMPERSAND);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_DBLAMPERSAND", SCANCODE_KP_DBLAMPERSAND);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_VERTICALBAR", SCANCODE_KP_VERTICALBAR);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_DBLVERTICALBAR", SCANCODE_KP_DBLVERTICALBAR);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_COLON", SCANCODE_KP_COLON);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_HASH", SCANCODE_KP_HASH);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_SPACE", SCANCODE_KP_SPACE);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_AT", SCANCODE_KP_AT);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_EXCLAM", SCANCODE_KP_EXCLAM);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_MEMSTORE", SCANCODE_KP_MEMSTORE);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_MEMRECALL", SCANCODE_KP_MEMRECALL);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_MEMCLEAR", SCANCODE_KP_MEMCLEAR);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_MEMADD", SCANCODE_KP_MEMADD);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_MEMSUBTRACT", SCANCODE_KP_MEMSUBTRACT);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_MEMMULTIPLY", SCANCODE_KP_MEMMULTIPLY);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_MEMDIVIDE", SCANCODE_KP_MEMDIVIDE);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_PLUSMINUS", SCANCODE_KP_PLUSMINUS);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_CLEAR", SCANCODE_KP_CLEAR);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_CLEARENTRY", SCANCODE_KP_CLEARENTRY);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_BINARY", SCANCODE_KP_BINARY);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_OCTAL", SCANCODE_KP_OCTAL);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_DECIMAL", SCANCODE_KP_DECIMAL);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KP_HEXADECIMAL", SCANCODE_KP_HEXADECIMAL);
    engine->RegisterEnumValue("Scancode", "SCANCODE_LCTRL", SCANCODE_LCTRL);
    engine->RegisterEnumValue("Scancode", "SCANCODE_LSHIFT", SCANCODE_LSHIFT);
    engine->RegisterEnumValue("Scancode", "SCANCODE_LALT", SCANCODE_LALT);
    engine->RegisterEnumValue("Scancode", "SCANCODE_LGUI", SCANCODE_LGUI);
    engine->RegisterEnumValue("Scancode", "SCANCODE_RCTRL", SCANCODE_RCTRL);
    engine->RegisterEnumValue("Scancode", "SCANCODE_RSHIFT", SCANCODE_RSHIFT);
    engine->RegisterEnumValue("Scancode", "SCANCODE_RALT", SCANCODE_RALT);
    engine->RegisterEnumValue("Scancode", "SCANCODE_RGUI", SCANCODE_RGUI);
    engine->RegisterEnumValue("Scancode", "SCANCODE_MODE", SCANCODE_MODE);
    engine->RegisterEnumValue("Scancode", "SCANCODE_AUDIONEXT", SCANCODE_AUDIONEXT);
    engine->RegisterEnumValue("Scancode", "SCANCODE_AUDIOPREV", SCANCODE_AUDIOPREV);
    engine->RegisterEnumValue("Scancode", "SCANCODE_AUDIOSTOP", SCANCODE_AUDIOSTOP);
    engine->RegisterEnumValue("Scancode", "SCANCODE_AUDIOPLAY", SCANCODE_AUDIOPLAY);
    engine->RegisterEnumValue("Scancode", "SCANCODE_AUDIOMUTE", SCANCODE_AUDIOMUTE);
    engine->RegisterEnumValue("Scancode", "SCANCODE_MEDIASELECT", SCANCODE_MEDIASELECT);
    engine->RegisterEnumValue("Scancode", "SCANCODE_WWW", SCANCODE_WWW);
    engine->RegisterEnumValue("Scancode", "SCANCODE_MAIL", SCANCODE_MAIL);
    engine->RegisterEnumValue("Scancode", "SCANCODE_CALCULATOR", SCANCODE_CALCULATOR);
    engine->RegisterEnumValue("Scancode", "SCANCODE_COMPUTER", SCANCODE_COMPUTER);
    engine->RegisterEnumValue("Scancode", "SCANCODE_AC_SEARCH", SCANCODE_AC_SEARCH);
    engine->RegisterEnumValue("Scancode", "SCANCODE_AC_HOME", SCANCODE_AC_HOME);
    engine->RegisterEnumValue("Scancode", "SCANCODE_AC_BACK", SCANCODE_AC_BACK);
    engine->RegisterEnumValue("Scancode", "SCANCODE_AC_FORWARD", SCANCODE_AC_FORWARD);
    engine->RegisterEnumValue("Scancode", "SCANCODE_AC_STOP", SCANCODE_AC_STOP);
    engine->RegisterEnumValue("Scancode", "SCANCODE_AC_REFRESH", SCANCODE_AC_REFRESH);
    engine->RegisterEnumValue("Scancode", "SCANCODE_AC_BOOKMARKS", SCANCODE_AC_BOOKMARKS);
    engine->RegisterEnumValue("Scancode", "SCANCODE_BRIGHTNESSDOWN", SCANCODE_BRIGHTNESSDOWN);
    engine->RegisterEnumValue("Scancode", "SCANCODE_BRIGHTNESSUP", SCANCODE_BRIGHTNESSUP);
    engine->RegisterEnumValue("Scancode", "SCANCODE_DISPLAYSWITCH", SCANCODE_DISPLAYSWITCH);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KBDILLUMTOGGLE", SCANCODE_KBDILLUMTOGGLE);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KBDILLUMDOWN", SCANCODE_KBDILLUMDOWN);
    engine->RegisterEnumValue("Scancode", "SCANCODE_KBDILLUMUP", SCANCODE_KBDILLUMUP);
    engine->RegisterEnumValue("Scancode", "SCANCODE_EJECT", SCANCODE_EJECT);
    engine->RegisterEnumValue("Scancode", "SCANCODE_SLEEP", SCANCODE_SLEEP);
    engine->RegisterEnumValue("Scancode", "SCANCODE_APP1", SCANCODE_APP1);
    engine->RegisterEnumValue("Scancode", "SCANCODE_APP2", SCANCODE_APP2);

    // enum ShaderParameterGroup | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("ShaderParameterGroup");
    engine->RegisterEnumValue("ShaderParameterGroup", "SP_FRAME", SP_FRAME);
    engine->RegisterEnumValue("ShaderParameterGroup", "SP_CAMERA", SP_CAMERA);
    engine->RegisterEnumValue("ShaderParameterGroup", "SP_ZONE", SP_ZONE);
    engine->RegisterEnumValue("ShaderParameterGroup", "SP_LIGHT", SP_LIGHT);
    engine->RegisterEnumValue("ShaderParameterGroup", "SP_MATERIAL", SP_MATERIAL);
    engine->RegisterEnumValue("ShaderParameterGroup", "SP_OBJECT", SP_OBJECT);
    engine->RegisterEnumValue("ShaderParameterGroup", "SP_CUSTOM", SP_CUSTOM);
    engine->RegisterEnumValue("ShaderParameterGroup", "MAX_SHADER_PARAMETER_GROUPS", MAX_SHADER_PARAMETER_GROUPS);

    // enum ShaderType | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("ShaderType");
    engine->RegisterEnumValue("ShaderType", "VS", VS);
    engine->RegisterEnumValue("ShaderType", "PS", PS);

    // enum ShadowQuality | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("ShadowQuality");
    engine->RegisterEnumValue("ShadowQuality", "SHADOWQUALITY_SIMPLE_16BIT", SHADOWQUALITY_SIMPLE_16BIT);
    engine->RegisterEnumValue("ShadowQuality", "SHADOWQUALITY_SIMPLE_24BIT", SHADOWQUALITY_SIMPLE_24BIT);
    engine->RegisterEnumValue("ShadowQuality", "SHADOWQUALITY_PCF_16BIT", SHADOWQUALITY_PCF_16BIT);
    engine->RegisterEnumValue("ShadowQuality", "SHADOWQUALITY_PCF_24BIT", SHADOWQUALITY_PCF_24BIT);
    engine->RegisterEnumValue("ShadowQuality", "SHADOWQUALITY_VSM", SHADOWQUALITY_VSM);
    engine->RegisterEnumValue("ShadowQuality", "SHADOWQUALITY_BLUR_VSM", SHADOWQUALITY_BLUR_VSM);

    // enum SmoothingType | File: ../Scene/SmoothedTransform.h
    engine->RegisterEnum("SmoothingType");
    engine->RegisterEnumValue("SmoothingType", "SMOOTH_NONE", SMOOTH_NONE);
    engine->RegisterEnumValue("SmoothingType", "SMOOTH_POSITION", SMOOTH_POSITION);
    engine->RegisterEnumValue("SmoothingType", "SMOOTH_ROTATION", SMOOTH_ROTATION);

    // URHO3D_FLAGSET(SmoothingType, SmoothingTypeFlags) | File: ../Scene/SmoothedTransform.h
    engine->RegisterTypedef("SmoothingTypeFlags", "int");

    // enum StencilOp | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("StencilOp");
    engine->RegisterEnumValue("StencilOp", "OP_KEEP", OP_KEEP);
    engine->RegisterEnumValue("StencilOp", "OP_ZERO", OP_ZERO);
    engine->RegisterEnumValue("StencilOp", "OP_REF", OP_REF);
    engine->RegisterEnumValue("StencilOp", "OP_INCR", OP_INCR);
    engine->RegisterEnumValue("StencilOp", "OP_DECR", OP_DECR);

    // enum TextEffect | File: ../UI/Text.h
    engine->RegisterEnum("TextEffect");
    engine->RegisterEnumValue("TextEffect", "TE_NONE", TE_NONE);
    engine->RegisterEnumValue("TextEffect", "TE_SHADOW", TE_SHADOW);
    engine->RegisterEnumValue("TextEffect", "TE_STROKE", TE_STROKE);

    // enum TextureAddressMode | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("TextureAddressMode");
    engine->RegisterEnumValue("TextureAddressMode", "ADDRESS_WRAP", ADDRESS_WRAP);
    engine->RegisterEnumValue("TextureAddressMode", "ADDRESS_MIRROR", ADDRESS_MIRROR);
    engine->RegisterEnumValue("TextureAddressMode", "ADDRESS_CLAMP", ADDRESS_CLAMP);
    engine->RegisterEnumValue("TextureAddressMode", "ADDRESS_BORDER", ADDRESS_BORDER);
    engine->RegisterEnumValue("TextureAddressMode", "MAX_ADDRESSMODES", MAX_ADDRESSMODES);

    // enum TextureCoordinate | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("TextureCoordinate");
    engine->RegisterEnumValue("TextureCoordinate", "COORD_U", COORD_U);
    engine->RegisterEnumValue("TextureCoordinate", "COORD_V", COORD_V);
    engine->RegisterEnumValue("TextureCoordinate", "COORD_W", COORD_W);
    engine->RegisterEnumValue("TextureCoordinate", "MAX_COORDS", MAX_COORDS);

    // enum TextureFilterMode | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("TextureFilterMode");
    engine->RegisterEnumValue("TextureFilterMode", "FILTER_NEAREST", FILTER_NEAREST);
    engine->RegisterEnumValue("TextureFilterMode", "FILTER_BILINEAR", FILTER_BILINEAR);
    engine->RegisterEnumValue("TextureFilterMode", "FILTER_TRILINEAR", FILTER_TRILINEAR);
    engine->RegisterEnumValue("TextureFilterMode", "FILTER_ANISOTROPIC", FILTER_ANISOTROPIC);
    engine->RegisterEnumValue("TextureFilterMode", "FILTER_NEAREST_ANISOTROPIC", FILTER_NEAREST_ANISOTROPIC);
    engine->RegisterEnumValue("TextureFilterMode", "FILTER_DEFAULT", FILTER_DEFAULT);
    engine->RegisterEnumValue("TextureFilterMode", "MAX_FILTERMODES", MAX_FILTERMODES);

    // enum TextureUnit | File: ../GraphicsAPI/GraphicsDefs.h
    // Not registered because have @manualbind mark

    // enum TextureUsage | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("TextureUsage");
    engine->RegisterEnumValue("TextureUsage", "TEXTURE_STATIC", TEXTURE_STATIC);
    engine->RegisterEnumValue("TextureUsage", "TEXTURE_DYNAMIC", TEXTURE_DYNAMIC);
    engine->RegisterEnumValue("TextureUsage", "TEXTURE_RENDERTARGET", TEXTURE_RENDERTARGET);
    engine->RegisterEnumValue("TextureUsage", "TEXTURE_DEPTHSTENCIL", TEXTURE_DEPTHSTENCIL);

    // enum TrailType | File: ../Graphics/RibbonTrail.h
    engine->RegisterEnum("TrailType");
    engine->RegisterEnumValue("TrailType", "TT_FACE_CAMERA", TT_FACE_CAMERA);
    engine->RegisterEnumValue("TrailType", "TT_BONE", TT_BONE);

    // enum class TransformSpace | File: ../Scene/Node.h
    engine->RegisterTypedef("TransformSpace", "int");
    engine->SetDefaultNamespace("TransformSpace");
    engine->RegisterGlobalProperty("const int Local", (void*)&TransformSpace_Local);
    engine->RegisterGlobalProperty("const int Parent", (void*)&TransformSpace_Parent);
    engine->RegisterGlobalProperty("const int World", (void*)&TransformSpace_World);
    engine->SetDefaultNamespace("");

    // enum TraversalMode | File: ../UI/UIElement.h
    engine->RegisterEnum("TraversalMode");
    engine->RegisterEnumValue("TraversalMode", "TM_BREADTH_FIRST", TM_BREADTH_FIRST);
    engine->RegisterEnumValue("TraversalMode", "TM_DEPTH_FIRST", TM_DEPTH_FIRST);

    // enum UpdateGeometryType | File: ../Graphics/Drawable.h
    engine->RegisterEnum("UpdateGeometryType");
    engine->RegisterEnumValue("UpdateGeometryType", "UPDATE_NONE", UPDATE_NONE);
    engine->RegisterEnumValue("UpdateGeometryType", "UPDATE_MAIN_THREAD", UPDATE_MAIN_THREAD);
    engine->RegisterEnumValue("UpdateGeometryType", "UPDATE_WORKER_THREAD", UPDATE_WORKER_THREAD);

    // enum VariantType | File: ../Core/Variant.h
    engine->RegisterEnum("VariantType");
    engine->RegisterEnumValue("VariantType", "VAR_NONE", VAR_NONE);
    engine->RegisterEnumValue("VariantType", "VAR_INT", VAR_INT);
    engine->RegisterEnumValue("VariantType", "VAR_BOOL", VAR_BOOL);
    engine->RegisterEnumValue("VariantType", "VAR_FLOAT", VAR_FLOAT);
    engine->RegisterEnumValue("VariantType", "VAR_VECTOR2", VAR_VECTOR2);
    engine->RegisterEnumValue("VariantType", "VAR_VECTOR3", VAR_VECTOR3);
    engine->RegisterEnumValue("VariantType", "VAR_VECTOR4", VAR_VECTOR4);
    engine->RegisterEnumValue("VariantType", "VAR_QUATERNION", VAR_QUATERNION);
    engine->RegisterEnumValue("VariantType", "VAR_COLOR", VAR_COLOR);
    engine->RegisterEnumValue("VariantType", "VAR_STRING", VAR_STRING);
    engine->RegisterEnumValue("VariantType", "VAR_BUFFER", VAR_BUFFER);
    engine->RegisterEnumValue("VariantType", "VAR_VOIDPTR", VAR_VOIDPTR);
    engine->RegisterEnumValue("VariantType", "VAR_RESOURCEREF", VAR_RESOURCEREF);
    engine->RegisterEnumValue("VariantType", "VAR_RESOURCEREFLIST", VAR_RESOURCEREFLIST);
    engine->RegisterEnumValue("VariantType", "VAR_VARIANTVECTOR", VAR_VARIANTVECTOR);
    engine->RegisterEnumValue("VariantType", "VAR_VARIANTMAP", VAR_VARIANTMAP);
    engine->RegisterEnumValue("VariantType", "VAR_INTRECT", VAR_INTRECT);
    engine->RegisterEnumValue("VariantType", "VAR_INTVECTOR2", VAR_INTVECTOR2);
    engine->RegisterEnumValue("VariantType", "VAR_PTR", VAR_PTR);
    engine->RegisterEnumValue("VariantType", "VAR_MATRIX3", VAR_MATRIX3);
    engine->RegisterEnumValue("VariantType", "VAR_MATRIX3X4", VAR_MATRIX3X4);
    engine->RegisterEnumValue("VariantType", "VAR_MATRIX4", VAR_MATRIX4);
    engine->RegisterEnumValue("VariantType", "VAR_DOUBLE", VAR_DOUBLE);
    engine->RegisterEnumValue("VariantType", "VAR_STRINGVECTOR", VAR_STRINGVECTOR);
    engine->RegisterEnumValue("VariantType", "VAR_RECT", VAR_RECT);
    engine->RegisterEnumValue("VariantType", "VAR_INTVECTOR3", VAR_INTVECTOR3);
    engine->RegisterEnumValue("VariantType", "VAR_INT64", VAR_INT64);
    engine->RegisterEnumValue("VariantType", "VAR_CUSTOM_HEAP", VAR_CUSTOM_HEAP);
    engine->RegisterEnumValue("VariantType", "VAR_CUSTOM_STACK", VAR_CUSTOM_STACK);
    engine->RegisterEnumValue("VariantType", "MAX_VAR_TYPES", MAX_VAR_TYPES);

    // enum VertexElementSemantic | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("VertexElementSemantic");
    engine->RegisterEnumValue("VertexElementSemantic", "SEM_POSITION", SEM_POSITION);
    engine->RegisterEnumValue("VertexElementSemantic", "SEM_NORMAL", SEM_NORMAL);
    engine->RegisterEnumValue("VertexElementSemantic", "SEM_BINORMAL", SEM_BINORMAL);
    engine->RegisterEnumValue("VertexElementSemantic", "SEM_TANGENT", SEM_TANGENT);
    engine->RegisterEnumValue("VertexElementSemantic", "SEM_TEXCOORD", SEM_TEXCOORD);
    engine->RegisterEnumValue("VertexElementSemantic", "SEM_COLOR", SEM_COLOR);
    engine->RegisterEnumValue("VertexElementSemantic", "SEM_BLENDWEIGHTS", SEM_BLENDWEIGHTS);
    engine->RegisterEnumValue("VertexElementSemantic", "SEM_BLENDINDICES", SEM_BLENDINDICES);
    engine->RegisterEnumValue("VertexElementSemantic", "SEM_OBJECTINDEX", SEM_OBJECTINDEX);
    engine->RegisterEnumValue("VertexElementSemantic", "MAX_VERTEX_ELEMENT_SEMANTICS", MAX_VERTEX_ELEMENT_SEMANTICS);

    // enum VertexElementType | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterEnum("VertexElementType");
    engine->RegisterEnumValue("VertexElementType", "TYPE_INT", TYPE_INT);
    engine->RegisterEnumValue("VertexElementType", "TYPE_FLOAT", TYPE_FLOAT);
    engine->RegisterEnumValue("VertexElementType", "TYPE_VECTOR2", TYPE_VECTOR2);
    engine->RegisterEnumValue("VertexElementType", "TYPE_VECTOR3", TYPE_VECTOR3);
    engine->RegisterEnumValue("VertexElementType", "TYPE_VECTOR4", TYPE_VECTOR4);
    engine->RegisterEnumValue("VertexElementType", "TYPE_UBYTE4", TYPE_UBYTE4);
    engine->RegisterEnumValue("VertexElementType", "TYPE_UBYTE4_NORM", TYPE_UBYTE4_NORM);
    engine->RegisterEnumValue("VertexElementType", "MAX_VERTEX_ELEMENT_TYPES", MAX_VERTEX_ELEMENT_TYPES);

    // enum class VertexElements : u32 | File: ../GraphicsAPI/GraphicsDefs.h
    engine->RegisterTypedef("VertexElements", "uint");
    engine->SetDefaultNamespace("VertexElements");
    engine->RegisterGlobalProperty("const uint None", (void*)&VertexElements_None);
    engine->RegisterGlobalProperty("const uint Position", (void*)&VertexElements_Position);
    engine->RegisterGlobalProperty("const uint Normal", (void*)&VertexElements_Normal);
    engine->RegisterGlobalProperty("const uint Color", (void*)&VertexElements_Color);
    engine->RegisterGlobalProperty("const uint TexCoord1", (void*)&VertexElements_TexCoord1);
    engine->RegisterGlobalProperty("const uint TexCoord2", (void*)&VertexElements_TexCoord2);
    engine->RegisterGlobalProperty("const uint CubeTexCoord1", (void*)&VertexElements_CubeTexCoord1);
    engine->RegisterGlobalProperty("const uint CubeTexCoord2", (void*)&VertexElements_CubeTexCoord2);
    engine->RegisterGlobalProperty("const uint Tangent", (void*)&VertexElements_Tangent);
    engine->RegisterGlobalProperty("const uint BlendWeights", (void*)&VertexElements_BlendWeights);
    engine->RegisterGlobalProperty("const uint BlendIndices", (void*)&VertexElements_BlendIndices);
    engine->RegisterGlobalProperty("const uint InstanceMatrix1", (void*)&VertexElements_InstanceMatrix1);
    engine->RegisterGlobalProperty("const uint InstanceMatrix2", (void*)&VertexElements_InstanceMatrix2);
    engine->RegisterGlobalProperty("const uint InstanceMatrix3", (void*)&VertexElements_InstanceMatrix3);
    engine->RegisterGlobalProperty("const uint ObjectIndex", (void*)&VertexElements_ObjectIndex);
    engine->SetDefaultNamespace("");

    // enum VertexLightVSVariation | File: ../Graphics/Renderer.h
    engine->RegisterEnum("VertexLightVSVariation");
    engine->RegisterEnumValue("VertexLightVSVariation", "VLVS_NOLIGHTS", VLVS_NOLIGHTS);
    engine->RegisterEnumValue("VertexLightVSVariation", "VLVS_1LIGHT", VLVS_1LIGHT);
    engine->RegisterEnumValue("VertexLightVSVariation", "VLVS_2LIGHTS", VLVS_2LIGHTS);
    engine->RegisterEnumValue("VertexLightVSVariation", "VLVS_3LIGHTS", VLVS_3LIGHTS);
    engine->RegisterEnumValue("VertexLightVSVariation", "VLVS_4LIGHTS", VLVS_4LIGHTS);
    engine->RegisterEnumValue("VertexLightVSVariation", "MAX_VERTEXLIGHT_VS_VARIATIONS", MAX_VERTEXLIGHT_VS_VARIATIONS);

    // enum VerticalAlignment | File: ../UI/UIElement.h
    engine->RegisterEnum("VerticalAlignment");
    engine->RegisterEnumValue("VerticalAlignment", "VA_TOP", VA_TOP);
    engine->RegisterEnumValue("VerticalAlignment", "VA_CENTER", VA_CENTER);
    engine->RegisterEnumValue("VerticalAlignment", "VA_BOTTOM", VA_BOTTOM);
    engine->RegisterEnumValue("VerticalAlignment", "VA_CUSTOM", VA_CUSTOM);

    // enum ViewOverride | File: ../Graphics/Camera.h
    engine->RegisterEnum("ViewOverride");
    engine->RegisterEnumValue("ViewOverride", "VO_NONE", VO_NONE);
    engine->RegisterEnumValue("ViewOverride", "VO_LOW_MATERIAL_QUALITY", VO_LOW_MATERIAL_QUALITY);
    engine->RegisterEnumValue("ViewOverride", "VO_DISABLE_SHADOWS", VO_DISABLE_SHADOWS);
    engine->RegisterEnumValue("ViewOverride", "VO_DISABLE_OCCLUSION", VO_DISABLE_OCCLUSION);

    // URHO3D_FLAGSET(ViewOverride, ViewOverrideFlags) | File: ../Graphics/Camera.h
    engine->RegisterTypedef("ViewOverrideFlags", "int");

    // enum WindowDragMode | File: ../UI/Window.h
    engine->RegisterEnum("WindowDragMode");
    engine->RegisterEnumValue("WindowDragMode", "DRAG_NONE", DRAG_NONE);
    engine->RegisterEnumValue("WindowDragMode", "DRAG_MOVE", DRAG_MOVE);
    engine->RegisterEnumValue("WindowDragMode", "DRAG_RESIZE_TOPLEFT", DRAG_RESIZE_TOPLEFT);
    engine->RegisterEnumValue("WindowDragMode", "DRAG_RESIZE_TOP", DRAG_RESIZE_TOP);
    engine->RegisterEnumValue("WindowDragMode", "DRAG_RESIZE_TOPRIGHT", DRAG_RESIZE_TOPRIGHT);
    engine->RegisterEnumValue("WindowDragMode", "DRAG_RESIZE_RIGHT", DRAG_RESIZE_RIGHT);
    engine->RegisterEnumValue("WindowDragMode", "DRAG_RESIZE_BOTTOMRIGHT", DRAG_RESIZE_BOTTOMRIGHT);
    engine->RegisterEnumValue("WindowDragMode", "DRAG_RESIZE_BOTTOM", DRAG_RESIZE_BOTTOM);
    engine->RegisterEnumValue("WindowDragMode", "DRAG_RESIZE_BOTTOMLEFT", DRAG_RESIZE_BOTTOMLEFT);
    engine->RegisterEnumValue("WindowDragMode", "DRAG_RESIZE_LEFT", DRAG_RESIZE_LEFT);

    // enum WrapMode | File: ../Scene/AnimationDefs.h
    engine->RegisterEnum("WrapMode");
    engine->RegisterEnumValue("WrapMode", "WM_LOOP", WM_LOOP);
    engine->RegisterEnumValue("WrapMode", "WM_ONCE", WM_ONCE);
    engine->RegisterEnumValue("WrapMode", "WM_CLAMP", WM_CLAMP);

#ifdef URHO3D_DATABASE
    // enum DBAPI | File: ../Database/Database.h
    engine->RegisterEnum("DBAPI");
    engine->RegisterEnumValue("DBAPI", "DBAPI_SQLITE", DBAPI_SQLITE);
    engine->RegisterEnumValue("DBAPI", "DBAPI_ODBC", DBAPI_ODBC);
#endif

#ifdef URHO3D_NAVIGATION
    // enum CrowdAgentRequestedTarget | File: ../Navigation/CrowdAgent.h
    engine->RegisterEnum("CrowdAgentRequestedTarget");
    engine->RegisterEnumValue("CrowdAgentRequestedTarget", "CA_REQUESTEDTARGET_NONE", CA_REQUESTEDTARGET_NONE);
    engine->RegisterEnumValue("CrowdAgentRequestedTarget", "CA_REQUESTEDTARGET_POSITION", CA_REQUESTEDTARGET_POSITION);
    engine->RegisterEnumValue("CrowdAgentRequestedTarget", "CA_REQUESTEDTARGET_VELOCITY", CA_REQUESTEDTARGET_VELOCITY);

    // enum CrowdAgentState | File: ../Navigation/CrowdAgent.h
    engine->RegisterEnum("CrowdAgentState");
    engine->RegisterEnumValue("CrowdAgentState", "CA_STATE_INVALID", CA_STATE_INVALID);
    engine->RegisterEnumValue("CrowdAgentState", "CA_STATE_WALKING", CA_STATE_WALKING);
    engine->RegisterEnumValue("CrowdAgentState", "CA_STATE_OFFMESH", CA_STATE_OFFMESH);

    // enum CrowdAgentTargetState | File: ../Navigation/CrowdAgent.h
    engine->RegisterEnum("CrowdAgentTargetState");
    engine->RegisterEnumValue("CrowdAgentTargetState", "CA_TARGET_NONE", CA_TARGET_NONE);
    engine->RegisterEnumValue("CrowdAgentTargetState", "CA_TARGET_FAILED", CA_TARGET_FAILED);
    engine->RegisterEnumValue("CrowdAgentTargetState", "CA_TARGET_VALID", CA_TARGET_VALID);
    engine->RegisterEnumValue("CrowdAgentTargetState", "CA_TARGET_REQUESTING", CA_TARGET_REQUESTING);
    engine->RegisterEnumValue("CrowdAgentTargetState", "CA_TARGET_WAITINGFORQUEUE", CA_TARGET_WAITINGFORQUEUE);
    engine->RegisterEnumValue("CrowdAgentTargetState", "CA_TARGET_WAITINGFORPATH", CA_TARGET_WAITINGFORPATH);
    engine->RegisterEnumValue("CrowdAgentTargetState", "CA_TARGET_VELOCITY", CA_TARGET_VELOCITY);

    // enum NavigationPathPointFlag | File: ../Navigation/NavigationMesh.h
    engine->RegisterEnum("NavigationPathPointFlag");
    engine->RegisterEnumValue("NavigationPathPointFlag", "NAVPATHFLAG_NONE", NAVPATHFLAG_NONE);
    engine->RegisterEnumValue("NavigationPathPointFlag", "NAVPATHFLAG_START", NAVPATHFLAG_START);
    engine->RegisterEnumValue("NavigationPathPointFlag", "NAVPATHFLAG_END", NAVPATHFLAG_END);
    engine->RegisterEnumValue("NavigationPathPointFlag", "NAVPATHFLAG_OFF_MESH", NAVPATHFLAG_OFF_MESH);

    // enum NavigationPushiness | File: ../Navigation/CrowdAgent.h
    engine->RegisterEnum("NavigationPushiness");
    engine->RegisterEnumValue("NavigationPushiness", "NAVIGATIONPUSHINESS_LOW", NAVIGATIONPUSHINESS_LOW);
    engine->RegisterEnumValue("NavigationPushiness", "NAVIGATIONPUSHINESS_MEDIUM", NAVIGATIONPUSHINESS_MEDIUM);
    engine->RegisterEnumValue("NavigationPushiness", "NAVIGATIONPUSHINESS_HIGH", NAVIGATIONPUSHINESS_HIGH);
    engine->RegisterEnumValue("NavigationPushiness", "NAVIGATIONPUSHINESS_NONE", NAVIGATIONPUSHINESS_NONE);

    // enum NavigationQuality | File: ../Navigation/CrowdAgent.h
    engine->RegisterEnum("NavigationQuality");
    engine->RegisterEnumValue("NavigationQuality", "NAVIGATIONQUALITY_LOW", NAVIGATIONQUALITY_LOW);
    engine->RegisterEnumValue("NavigationQuality", "NAVIGATIONQUALITY_MEDIUM", NAVIGATIONQUALITY_MEDIUM);
    engine->RegisterEnumValue("NavigationQuality", "NAVIGATIONQUALITY_HIGH", NAVIGATIONQUALITY_HIGH);

    // enum NavmeshPartitionType | File: ../Navigation/NavigationMesh.h
    engine->RegisterEnum("NavmeshPartitionType");
    engine->RegisterEnumValue("NavmeshPartitionType", "NAVMESH_PARTITION_WATERSHED", NAVMESH_PARTITION_WATERSHED);
    engine->RegisterEnumValue("NavmeshPartitionType", "NAVMESH_PARTITION_MONOTONE", NAVMESH_PARTITION_MONOTONE);
#endif

#ifdef URHO3D_NETWORK
    // enum HttpRequestState | File: ../Network/HttpRequest.h
    engine->RegisterEnum("HttpRequestState");
    engine->RegisterEnumValue("HttpRequestState", "HTTP_INITIALIZING", HTTP_INITIALIZING);
    engine->RegisterEnumValue("HttpRequestState", "HTTP_ERROR", HTTP_ERROR);
    engine->RegisterEnumValue("HttpRequestState", "HTTP_OPEN", HTTP_OPEN);
    engine->RegisterEnumValue("HttpRequestState", "HTTP_CLOSED", HTTP_CLOSED);

    // enum ObserverPositionSendMode | File: ../Network/Connection.h
    engine->RegisterEnum("ObserverPositionSendMode");
    engine->RegisterEnumValue("ObserverPositionSendMode", "OPSM_NONE", OPSM_NONE);
    engine->RegisterEnumValue("ObserverPositionSendMode", "OPSM_POSITION", OPSM_POSITION);
    engine->RegisterEnumValue("ObserverPositionSendMode", "OPSM_POSITION_ROTATION", OPSM_POSITION_ROTATION);

    // enum PacketType | File: ../Network/Connection.h
    engine->RegisterEnum("PacketType");
    engine->RegisterEnumValue("PacketType", "PT_UNRELIABLE_UNORDERED", PT_UNRELIABLE_UNORDERED);
    engine->RegisterEnumValue("PacketType", "PT_UNRELIABLE_ORDERED", PT_UNRELIABLE_ORDERED);
    engine->RegisterEnumValue("PacketType", "PT_RELIABLE_UNORDERED", PT_RELIABLE_UNORDERED);
    engine->RegisterEnumValue("PacketType", "PT_RELIABLE_ORDERED", PT_RELIABLE_ORDERED);
#endif

#ifdef URHO3D_PHYSICS
    // enum CollisionEventMode | File: ../Physics/RigidBody.h
    engine->RegisterEnum("CollisionEventMode");
    engine->RegisterEnumValue("CollisionEventMode", "COLLISION_NEVER", COLLISION_NEVER);
    engine->RegisterEnumValue("CollisionEventMode", "COLLISION_ACTIVE", COLLISION_ACTIVE);
    engine->RegisterEnumValue("CollisionEventMode", "COLLISION_ALWAYS", COLLISION_ALWAYS);

    // enum ConstraintType | File: ../Physics/Constraint.h
    engine->RegisterEnum("ConstraintType");
    engine->RegisterEnumValue("ConstraintType", "CONSTRAINT_POINT", CONSTRAINT_POINT);
    engine->RegisterEnumValue("ConstraintType", "CONSTRAINT_HINGE", CONSTRAINT_HINGE);
    engine->RegisterEnumValue("ConstraintType", "CONSTRAINT_SLIDER", CONSTRAINT_SLIDER);
    engine->RegisterEnumValue("ConstraintType", "CONSTRAINT_CONETWIST", CONSTRAINT_CONETWIST);
    engine->RegisterEnumValue("ConstraintType", "CONSTRAINT_6DOF", CONSTRAINT_6DOF);
    engine->RegisterEnumValue("ConstraintType", "CONSTRAINT_6DOF_SPRING", CONSTRAINT_6DOF_SPRING);
    engine->RegisterEnumValue("ConstraintType", "CONSTRAINT_GEAR", CONSTRAINT_GEAR);
    engine->RegisterEnumValue("ConstraintType", "CONSTRAINT_6DOF_SPRING2", CONSTRAINT_6DOF_SPRING2);

    // enum ShapeType | File: ../Physics/CollisionShape.h
    engine->RegisterEnum("ShapeType");
    engine->RegisterEnumValue("ShapeType", "SHAPE_BOX", SHAPE_BOX);
    engine->RegisterEnumValue("ShapeType", "SHAPE_SPHERE", SHAPE_SPHERE);
    engine->RegisterEnumValue("ShapeType", "SHAPE_STATICPLANE", SHAPE_STATICPLANE);
    engine->RegisterEnumValue("ShapeType", "SHAPE_CYLINDER", SHAPE_CYLINDER);
    engine->RegisterEnumValue("ShapeType", "SHAPE_CAPSULE", SHAPE_CAPSULE);
    engine->RegisterEnumValue("ShapeType", "SHAPE_CONE", SHAPE_CONE);
    engine->RegisterEnumValue("ShapeType", "SHAPE_TRIANGLEMESH", SHAPE_TRIANGLEMESH);
    engine->RegisterEnumValue("ShapeType", "SHAPE_CONVEXHULL", SHAPE_CONVEXHULL);
    engine->RegisterEnumValue("ShapeType", "SHAPE_TERRAIN", SHAPE_TERRAIN);
    engine->RegisterEnumValue("ShapeType", "SHAPE_GIMPACTMESH", SHAPE_GIMPACTMESH);
#endif

#ifdef URHO3D_PHYSICS2D
    // enum BodyType2D | File: ../Physics2D/RigidBody2D.h
    engine->RegisterEnum("BodyType2D");
    engine->RegisterEnumValue("BodyType2D", "BT_STATIC", BT_STATIC);
    engine->RegisterEnumValue("BodyType2D", "BT_KINEMATIC", BT_KINEMATIC);
    engine->RegisterEnumValue("BodyType2D", "BT_DYNAMIC", BT_DYNAMIC);
#endif

#ifdef URHO3D_URHO2D
    // enum EmitterType2D | File: ../Urho2D/ParticleEffect2D.h
    engine->RegisterEnum("EmitterType2D");
    engine->RegisterEnumValue("EmitterType2D", "EMITTER_TYPE_GRAVITY", EMITTER_TYPE_GRAVITY);
    engine->RegisterEnumValue("EmitterType2D", "EMITTER_TYPE_RADIAL", EMITTER_TYPE_RADIAL);

    // enum LoopMode2D | File: ../Urho2D/AnimatedSprite2D.h
    engine->RegisterEnum("LoopMode2D");
    engine->RegisterEnumValue("LoopMode2D", "LM_DEFAULT", LM_DEFAULT);
    engine->RegisterEnumValue("LoopMode2D", "LM_FORCE_LOOPED", LM_FORCE_LOOPED);
    engine->RegisterEnumValue("LoopMode2D", "LM_FORCE_CLAMPED", LM_FORCE_CLAMPED);

    // enum Orientation2D | File: ../Urho2D/TileMapDefs2D.h
    engine->RegisterEnum("Orientation2D");
    engine->RegisterEnumValue("Orientation2D", "O_ORTHOGONAL", O_ORTHOGONAL);
    engine->RegisterEnumValue("Orientation2D", "O_ISOMETRIC", O_ISOMETRIC);
    engine->RegisterEnumValue("Orientation2D", "O_STAGGERED", O_STAGGERED);
    engine->RegisterEnumValue("Orientation2D", "O_HEXAGONAL", O_HEXAGONAL);

    // enum TileMapLayerType2D | File: ../Urho2D/TileMapDefs2D.h
    engine->RegisterEnum("TileMapLayerType2D");
    engine->RegisterEnumValue("TileMapLayerType2D", "LT_TILE_LAYER", LT_TILE_LAYER);
    engine->RegisterEnumValue("TileMapLayerType2D", "LT_OBJECT_GROUP", LT_OBJECT_GROUP);
    engine->RegisterEnumValue("TileMapLayerType2D", "LT_IMAGE_LAYER", LT_IMAGE_LAYER);
    engine->RegisterEnumValue("TileMapLayerType2D", "LT_INVALID", LT_INVALID);

    // enum TileMapObjectType2D | File: ../Urho2D/TileMapDefs2D.h
    engine->RegisterEnum("TileMapObjectType2D");
    engine->RegisterEnumValue("TileMapObjectType2D", "OT_RECTANGLE", OT_RECTANGLE);
    engine->RegisterEnumValue("TileMapObjectType2D", "OT_ELLIPSE", OT_ELLIPSE);
    engine->RegisterEnumValue("TileMapObjectType2D", "OT_POLYGON", OT_POLYGON);
    engine->RegisterEnumValue("TileMapObjectType2D", "OT_POLYLINE", OT_POLYLINE);
    engine->RegisterEnumValue("TileMapObjectType2D", "OT_TILE", OT_TILE);
    engine->RegisterEnumValue("TileMapObjectType2D", "OT_INVALID", OT_INVALID);
#endif
}

}
