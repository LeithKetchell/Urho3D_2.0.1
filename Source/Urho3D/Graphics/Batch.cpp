// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "../Precompiled.h"

#include "../Graphics/Camera.h"
#include "../Graphics/Geometry.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Material.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/Technique.h"
#include "../Graphics/View.h"
#include "../GraphicsAPI/GraphicsImpl.h"
#include "../GraphicsAPI/ShaderVariation.h"
#include "../GraphicsAPI/Texture2D.h"
#include "../GraphicsAPI/VertexBuffer.h"
#include "../Scene/Scene.h"

#ifdef URHO3D_VULKAN
#include "../GraphicsAPI/Vulkan/VulkanPushConstants.h"
#include "../GraphicsAPI/Vulkan/VulkanBatchDispatcher.h"
#include "../GraphicsAPI/Vulkan/VulkanGraphicsImpl.h"
#include <vulkan/vulkan.h>
#endif

#include "../DebugNew.h"

namespace Urho3D
{

inline bool CompareBatchesState(Batch* lhs, Batch* rhs)
{
    if (lhs->renderOrder_ != rhs->renderOrder_)
        return lhs->renderOrder_ < rhs->renderOrder_;
    else if (lhs->sortKey_ != rhs->sortKey_)
        return lhs->sortKey_ < rhs->sortKey_;
    else
        return lhs->distance_ < rhs->distance_;
}

inline bool CompareBatchesFrontToBack(Batch* lhs, Batch* rhs)
{
    if (lhs->renderOrder_ != rhs->renderOrder_)
        return lhs->renderOrder_ < rhs->renderOrder_;
    else if (lhs->distance_ != rhs->distance_)
        return lhs->distance_ < rhs->distance_;
    else
        return lhs->sortKey_ < rhs->sortKey_;
}

inline bool CompareBatchesBackToFront(Batch* lhs, Batch* rhs)
{
    if (lhs->renderOrder_ != rhs->renderOrder_)
        return lhs->renderOrder_ < rhs->renderOrder_;
    else if (lhs->distance_ != rhs->distance_)
        return lhs->distance_ > rhs->distance_;
    else
        return lhs->sortKey_ < rhs->sortKey_;
}

inline bool CompareInstancesFrontToBack(const InstanceData& lhs, const InstanceData& rhs)
{
    return lhs.distance_ < rhs.distance_;
}

inline bool CompareBatchGroupOrder(BatchGroup* lhs, BatchGroup* rhs)
{
    return lhs->renderOrder_ < rhs->renderOrder_;
}

void CalculateShadowMatrix(Matrix4& dest, LightBatchQueue* queue, i32 split, Renderer* renderer)
{
    assert(split >= 0);

    Camera* shadowCamera = queue->shadowSplits_[split].shadowCamera_;
    const IntRect& viewport = queue->shadowSplits_[split].shadowViewport_;

    const Matrix3x4& shadowView(shadowCamera->GetView());
    Matrix4 shadowProj(shadowCamera->GetGPUProjection());
    Matrix4 texAdjust(Matrix4::IDENTITY);

    Texture2D* shadowMap = queue->shadowMap_;
    if (!shadowMap)
        return;

    auto width = (float)shadowMap->GetWidth();
    auto height = (float)shadowMap->GetHeight();

    Vector3 offset(
        (float)viewport.left_ / width,
        (float)viewport.top_ / height,
        0.0f
    );

    Vector3 scale(
        0.5f * (float)viewport.Width() / width,
        0.5f * (float)viewport.Height() / height,
        1.0f
    );

    offset.x_ += scale.x_;
    offset.y_ += scale.y_;

    if (Graphics::GetGAPI() == GAPI_OPENGL)
    {
        offset.z_ = 0.5f;
        scale.z_ = 0.5f;
        offset.y_ = 1.0f - offset.y_;
    }
    else if (Graphics::GetGAPI() != GAPI_VULKAN)
    {
        // D3D11: NDC Y=+1 at top, texture V=0 at top — need Y-flip
        scale.y_ = -scale.y_;
    }
    // Vulkan: shadow maps rendered with positive viewport height (no flip).
    // NDC Y → texture V mapping doesn't need negation.

    // If using 4 shadow samples, offset the position diagonally by half pixel
    if (renderer->GetShadowQuality() == SHADOWQUALITY_PCF_16BIT || renderer->GetShadowQuality() == SHADOWQUALITY_PCF_24BIT)
    {
        offset.x_ -= 0.5f / width;
        offset.y_ -= 0.5f / height;
    }
    texAdjust.SetTranslation(offset);
    texAdjust.SetScale(scale);

    dest = texAdjust * shadowProj * shadowView;
}

void CalculateSpotMatrix(Matrix4& dest, Light* light)
{
    Node* lightNode = light->GetNode();
    Matrix3x4 spotView = Matrix3x4(lightNode->GetWorldPosition(), lightNode->GetWorldRotation(), 1.0f).Inverse();
    Matrix4 spotProj(Matrix4::ZERO);
    Matrix4 texAdjust(Matrix4::IDENTITY);

    // Make the projected light slightly smaller than the shadow map to prevent light spill
    float h = 1.005f / tanf(light->GetFov() * M_DEGTORAD * 0.5f);
    float w = h / light->GetAspectRatio();
    spotProj.m00_ = w;
    spotProj.m11_ = h;
    spotProj.m22_ = 1.0f / Max(light->GetRange(), M_EPSILON);
    spotProj.m32_ = 1.0f;

    if (Graphics::GetGAPI() == GAPI_OPENGL)
    {
        texAdjust.SetTranslation(Vector3(0.5f, 0.5f, 0.5f));
        texAdjust.SetScale(Vector3(0.5f, -0.5f, 0.5f));
    }
    else
    {
        texAdjust.SetTranslation(Vector3(0.5f, 0.5f, 0.0f));
        texAdjust.SetScale(Vector3(0.5f, -0.5f, 1.0f));
    }

    dest = texAdjust * spotProj * spotView;
}

void Batch::CalculateSortKey()
{
    u32 shaderID = (u32)(
        ((*((u32*)&vertexShader_) / sizeof(ShaderVariation)) + (*((u32*)&pixelShader_) / sizeof(ShaderVariation))) &
        0x7fffu);
    if (!isBase_)
        shaderID |= 0x8000;

    u32 lightQueueID = (u32)((*((u32*)&lightQueue_) / sizeof(LightBatchQueue)) & 0xffffu);
    u32 materialID = (u32)((*((u32*)&material_) / sizeof(Material)) & 0xffffu);
    u32 geometryID = (u32)((*((u32*)&geometry_) / sizeof(Geometry)) & 0xffffu);

    sortKey_ = (((hash64)shaderID) << 48u) | (((hash64)lightQueueID) << 32u) |
               (((hash64)materialID) << 16u) | geometryID;
}

void Batch::Prepare(View* view, Camera* camera, bool setModelTransform, bool allowDepthWrite) const
{
    if (!vertexShader_ || !pixelShader_)
    {
        static int shaderMissCount = 0;
        if (shaderMissCount < 10)
        {
            URHO3D_LOGWARNINGF("[BATCH DIAG] Prepare skipped: vs=%p ps=%p pass=%s mat=%s",
                (void*)vertexShader_, (void*)pixelShader_,
                pass_ ? pass_->GetName().CString() : "null",
                material_ ? material_->GetName().CString() : "null");
            shaderMissCount++;
        }
        return;
    }

    Graphics* graphics = view->GetGraphics();
    Renderer* renderer = view->GetRenderer();
    Node* cameraNode = camera ? camera->GetNode() : nullptr;
    Light* light = lightQueue_ ? lightQueue_->light_ : nullptr;
    Texture2D* shadowMap = lightQueue_ ? lightQueue_->shadowMap_ : nullptr;
    if (light && shadowMap)
    {
    }

    // Set shaders first. The available shader parameters and their register/uniform positions depend on the currently set shaders
    graphics->SetShaders(vertexShader_, pixelShader_);

    // Set pass / material-specific renderstates
    if (pass_ && material_)
    {
        BlendMode blend = pass_->GetBlendMode();
        // Turn additive blending into subtract if the light is negative
        if (light && light->IsNegative())
        {
            if (blend == BLEND_ADD)
                blend = BLEND_SUBTRACT;
            else if (blend == BLEND_ADDALPHA)
                blend = BLEND_SUBTRACTALPHA;
        }
        graphics->SetBlendMode(blend, pass_->GetAlphaToCoverage() || material_->GetAlphaToCoverage());
        graphics->SetLineAntiAlias(material_->GetLineAntiAlias());

        bool isShadowPass = pass_->GetIndex() == Technique::shadowPassIndex;
        CullMode effectiveCullMode = pass_->GetCullMode();
        // Get cull mode from material if pass doesn't override it
        if (effectiveCullMode == MAX_CULLMODES)
            effectiveCullMode = isShadowPass ? material_->GetShadowCullMode() : material_->GetCullMode();

        renderer->SetCullMode(effectiveCullMode, camera);
        if (!isShadowPass)
        {
            const BiasParameters& depthBias = material_->GetDepthBias();
            graphics->SetDepthBias(depthBias.constantBias_, depthBias.slopeScaledBias_);
        }

        // Use the "least filled" fill mode combined from camera & material
        graphics->SetFillMode((FillMode)(Max(camera->GetFillMode(), material_->GetFillMode())));
        graphics->SetDepthTest(pass_->GetDepthTestMode());
        graphics->SetDepthWrite(pass_->GetDepthWrite() && allowDepthWrite);
    }

    // Set global (per-frame) shader parameters
    if (graphics->NeedParameterUpdate(SP_FRAME, nullptr))
        view->SetGlobalShaderParameters();

    // Set camera & viewport shader parameters
    hash32 cameraHash = (hash32)(size_t)camera;
    IntRect viewport = graphics->GetViewport();
    IntVector2 viewSize = IntVector2(viewport.Width(), viewport.Height());
    hash32 viewportHash = (hash32)viewSize.x_ | (hash32)viewSize.y_ << 16u;
    if (graphics->NeedParameterUpdate(SP_CAMERA, reinterpret_cast<const void*>(cameraHash + viewportHash)))
    {
        view->SetCameraShaderParameters(camera);
        // During renderpath commands the G-Buffer or viewport texture is assumed to always be viewport-sized
        view->SetGBufferShaderParameters(viewSize, IntRect(0, 0, viewSize.x_, viewSize.y_));
    }

    // Set model or skinning transforms
    if (setModelTransform && graphics->NeedParameterUpdate(SP_OBJECT, worldTransform_))
    {
        if (geometryType_ == GEOM_SKINNED)
        {
            graphics->SetShaderParameter(VSP_SKINMATRICES, reinterpret_cast<const float*>(worldTransform_),
                12 * numWorldTransforms_);
        }
        else
            graphics->SetShaderParameter(VSP_MODEL, *worldTransform_);

        // Set the orientation for billboards, either from the object itself or from the camera
        if (geometryType_ == GEOM_BILLBOARD)
        {
            if (numWorldTransforms_ > 1)
                graphics->SetShaderParameter(VSP_BILLBOARDROT, worldTransform_[1].RotationMatrix());
            else
                graphics->SetShaderParameter(VSP_BILLBOARDROT, cameraNode->GetWorldRotation().RotationMatrix());
        }
    }

    // Set zone-related shader parameters
    BlendMode blend = graphics->GetBlendMode();
    // If the pass is additive, override fog color to black so that shaders do not need a separate additive path
    bool overrideFogColorToBlack = blend == BLEND_ADD || blend == BLEND_ADDALPHA;
    hash32 zoneHash = (hash32)(size_t)zone_;
    if (overrideFogColorToBlack)
        zoneHash += 0x80000000;
    if (zone_ && graphics->NeedParameterUpdate(SP_ZONE, reinterpret_cast<const void*>(zoneHash)))
    {
        graphics->SetShaderParameter(VSP_AMBIENTSTARTCOLOR, zone_->GetAmbientStartColor());
        graphics->SetShaderParameter(VSP_AMBIENTENDCOLOR,
            zone_->GetAmbientEndColor().ToVector4() - zone_->GetAmbientStartColor().ToVector4());

        const BoundingBox& box = zone_->GetBoundingBox();
        Vector3 boxSize = box.Size();
        Matrix3x4 adjust(Matrix3x4::IDENTITY);
        adjust.SetScale(Vector3(1.0f / boxSize.x_, 1.0f / boxSize.y_, 1.0f / boxSize.z_));
        adjust.SetTranslation(Vector3(0.5f, 0.5f, 0.5f));
        Matrix3x4 zoneTransform = adjust * zone_->GetInverseWorldTransform();
        graphics->SetShaderParameter(VSP_ZONE, zoneTransform);

        graphics->SetShaderParameter(PSP_AMBIENTCOLOR, zone_->GetAmbientColor());

        // Hemisphere lighting: upload sky/ground ambient colors (PS + VS)
        if (zone_->HasHemisphereAmbient())
        {
            Vector3 sky = zone_->GetSkyAmbientColor().ToVector3();
            Vector3 ground = zone_->GetGroundAmbientColor().ToVector3();
            graphics->SetShaderParameter(PSP_SKYAMBIENT, sky);
            graphics->SetShaderParameter(PSP_GROUNDAMBIENT, ground);
            graphics->SetShaderParameter(VSP_SKYAMBIENT, sky);
            graphics->SetShaderParameter(VSP_GROUNDAMBIENT, ground);
        }
        else
        {
            // Fallback: both equal flat ambient — hemisphere blend becomes a no-op
            Vector3 amb = zone_->GetAmbientColor().ToVector3();
            graphics->SetShaderParameter(PSP_SKYAMBIENT, amb);
            graphics->SetShaderParameter(PSP_GROUNDAMBIENT, amb);
            graphics->SetShaderParameter(VSP_SKYAMBIENT, amb);
            graphics->SetShaderParameter(VSP_GROUNDAMBIENT, amb);
        }
        graphics->SetShaderParameter(PSP_FOGCOLOR, overrideFogColorToBlack ? Color::BLACK : zone_->GetFogColor());
        graphics->SetShaderParameter(PSP_ZONEMIN, zone_->GetBoundingBox().min_);
        graphics->SetShaderParameter(PSP_ZONEMAX, zone_->GetBoundingBox().max_);

        float farClip = camera->GetFarClip();
        float fogStart = Min(zone_->GetFogStart(), farClip);
        float fogEnd = Min(zone_->GetFogEnd(), farClip);
        if (fogStart >= fogEnd * (1.0f - M_LARGE_EPSILON))
            fogStart = fogEnd * (1.0f - M_LARGE_EPSILON);
        float fogRange = Max(fogEnd - fogStart, M_EPSILON);
        Vector4 fogParams(fogEnd / farClip, farClip / fogRange, 0.0f, 0.0f);

        Node* zoneNode = zone_->GetNode();
        if (zone_->GetHeightFog() && zoneNode)
        {
            Vector3 worldFogHeightVec = zoneNode->GetWorldTransform() * Vector3(0.0f, zone_->GetFogHeight(), 0.0f);
            fogParams.z_ = worldFogHeightVec.y_;
            fogParams.w_ = zone_->GetFogHeightScale() / Max(zoneNode->GetWorldScale().y_, M_EPSILON);
        }

        graphics->SetShaderParameter(PSP_FOGPARAMS, fogParams);
    }

    // Set light-related shader parameters
    if (lightQueue_)
    {
        if (light && graphics->NeedParameterUpdate(SP_LIGHT, lightQueue_))
        {
            Node* lightNode = light->GetNode();
            float atten = 1.0f / Max(light->GetRange(), M_EPSILON);
            Vector3 lightDir(lightNode->GetWorldRotation() * Vector3::BACK);
            Vector4 lightPos(lightNode->GetWorldPosition(), atten);

            graphics->SetShaderParameter(VSP_LIGHTDIR, lightDir);
            graphics->SetShaderParameter(VSP_LIGHTPOS, lightPos);

            if (graphics->HasShaderParameter(VSP_LIGHTMATRICES))
            {
                switch (light->GetLightType())
                {
                case LIGHT_DIRECTIONAL:
                    {
                        Matrix4 shadowMatrices[MAX_CASCADE_SPLITS];
                        i32 numSplits = Min(MAX_CASCADE_SPLITS, lightQueue_->shadowSplits_.Size());

                        for (i32 i = 0; i < numSplits; ++i)
                            CalculateShadowMatrix(shadowMatrices[i], lightQueue_, i, renderer);

                        graphics->SetShaderParameter(VSP_LIGHTMATRICES, shadowMatrices[0].Data(), 16 * numSplits);
                    }
                    break;

                case LIGHT_SPOT:
                    {
                        Matrix4 shadowMatrices[2];

                        CalculateSpotMatrix(shadowMatrices[0], light);
                        bool isShadowed = shadowMap && graphics->HasTextureUnit(TU_SHADOWMAP);
                        if (isShadowed)
                            CalculateShadowMatrix(shadowMatrices[1], lightQueue_, 0, renderer);

                        graphics->SetShaderParameter(VSP_LIGHTMATRICES, shadowMatrices[0].Data(), isShadowed ? 32 : 16);
                    }
                    break;

                case LIGHT_POINT:
                    {
                        Matrix4 lightVecRot(lightNode->GetWorldRotation().RotationMatrix());
                        // HLSL compiler will pack the parameters as if the matrix is only 3x4, so must be careful to not overwrite
                        // the next parameter. GLSL (OpenGL/Vulkan) uses full mat4 (16 floats).
                        if (Graphics::GetGAPI() == GAPI_OPENGL || Graphics::GetGAPI() == GAPI_VULKAN)
                            graphics->SetShaderParameter(VSP_LIGHTMATRICES, lightVecRot.Data(), 16);
                        else
                            graphics->SetShaderParameter(VSP_LIGHTMATRICES, lightVecRot.Data(), 12);
                    }
                    break;
                }
            }

            float fade = 1.0f;
            float fadeEnd = light->GetDrawDistance();
            float fadeStart = light->GetFadeDistance();

            // Do fade calculation for light if both fade & draw distance defined
            if (light->GetLightType() != LIGHT_DIRECTIONAL && fadeEnd > 0.0f && fadeStart > 0.0f && fadeStart < fadeEnd)
                fade = Min(1.0f - (light->GetDistance() - fadeStart) / (fadeEnd - fadeStart), 1.0f);

            // Negative lights will use subtract blending, so write absolute RGB values to the shader parameter
            Color effectiveLightColor = Color(light->GetEffectiveColor().Abs(),
                light->GetEffectiveSpecularIntensity()) * fade;
            graphics->SetShaderParameter(PSP_LIGHTCOLOR, effectiveLightColor);
            graphics->SetShaderParameter(PSP_LIGHTDIR, lightDir);
            graphics->SetShaderParameter(PSP_LIGHTPOS, lightPos);
            graphics->SetShaderParameter(PSP_LIGHTRAD, light->GetRadius());
            graphics->SetShaderParameter(PSP_LIGHTLENGTH, light->GetLength());

            if (graphics->HasShaderParameter(PSP_LIGHTMATRICES))
            {
                switch (light->GetLightType())
                {
                case LIGHT_DIRECTIONAL:
                    {
                        Matrix4 shadowMatrices[MAX_CASCADE_SPLITS];
                        i32 numSplits = Min(MAX_CASCADE_SPLITS, lightQueue_->shadowSplits_.Size());

                        for (i32 i = 0; i < numSplits; ++i)
                            CalculateShadowMatrix(shadowMatrices[i], lightQueue_, i, renderer);

                        graphics->SetShaderParameter(PSP_LIGHTMATRICES, shadowMatrices[0].Data(), 16 * numSplits);
                    }
                    break;

                case LIGHT_SPOT:
                    {
                        Matrix4 shadowMatrices[2];

                        CalculateSpotMatrix(shadowMatrices[0], light);
                        bool isShadowed = lightQueue_->shadowMap_ != nullptr;
                        if (isShadowed)
                            CalculateShadowMatrix(shadowMatrices[1], lightQueue_, 0, renderer);

                        graphics->SetShaderParameter(PSP_LIGHTMATRICES, shadowMatrices[0].Data(), isShadowed ? 32 : 16);
                    }
                    break;

                case LIGHT_POINT:
                    {
                        Matrix4 lightVecRot(lightNode->GetWorldRotation().RotationMatrix());
                        // HLSL compiler will pack the parameters as if the matrix is only 3x4, so must be careful to not overwrite
                        // the next parameter. GLSL (OpenGL/Vulkan) uses full mat4 (16 floats).
                        if (Graphics::GetGAPI() == GAPI_OPENGL || Graphics::GetGAPI() == GAPI_VULKAN)
                            graphics->SetShaderParameter(PSP_LIGHTMATRICES, lightVecRot.Data(), 16);
                        else
                            graphics->SetShaderParameter(PSP_LIGHTMATRICES, lightVecRot.Data(), 12);
                    }
                    break;
                }
            }

            // Set shadow mapping shader parameters
            if (shadowMap)
            {
                {
                    // Calculate point light shadow sampling offsets (unrolled cube map)
                    auto faceWidth = (unsigned)(shadowMap->GetWidth() / 2);
                    auto faceHeight = (unsigned)(shadowMap->GetHeight() / 3);
                    auto width = (float)shadowMap->GetWidth();
                    auto height = (float)shadowMap->GetHeight();

                    float mulX, mulY, addX, addY;
                    if (Graphics::GetGAPI() == GAPI_OPENGL || Graphics::GetGAPI() == GAPI_VULKAN)
                    {
                        mulX = (float)(faceWidth - 3) / width;
                        mulY = (float)(faceHeight - 3) / height;
                        addX = 1.5f / width;
                        addY = 1.5f / height;
                    }
                    else
                    {
                        mulX = (float)(faceWidth - 4) / width;
                        mulY = (float)(faceHeight - 4) / height;
                        addX = 2.5f / width;
                        addY = 2.5f / height;
                    }

                    // If using 4 shadow samples, offset the position diagonally by half pixel
                    if (renderer->GetShadowQuality() == SHADOWQUALITY_PCF_16BIT || renderer->GetShadowQuality() == SHADOWQUALITY_PCF_24BIT)
                    {
                        addX -= 0.5f / width;
                        addY -= 0.5f / height;
                    }
                    graphics->SetShaderParameter(PSP_SHADOWCUBEADJUST, Vector4(mulX, mulY, addX, addY));
                }

                {
                    // Calculate shadow camera depth parameters for point light shadows and shadow fade parameters for
                    //  directional light shadows, stored in the same uniform
                    Camera* shadowCamera = lightQueue_->shadowSplits_[0].shadowCamera_;
                    float nearClip = shadowCamera->GetNearClip();
                    float farClip = shadowCamera->GetFarClip();
                    float q = farClip / (farClip - nearClip);
                    float r = -q * nearClip;

                    const CascadeParameters& parameters = light->GetShadowCascade();
                    float viewFarClip = camera->GetFarClip();
                    float shadowRange = parameters.GetShadowRange();
                    float fadeStart = parameters.fadeStart_ * shadowRange / viewFarClip;
                    float fadeEnd = shadowRange / viewFarClip;
                    float fadeRange = fadeEnd - fadeStart;

                    graphics->SetShaderParameter(PSP_SHADOWDEPTHFADE, Vector4(q, r, fadeStart, 1.0f / fadeRange));
                }

                {
                    float intensity = light->GetShadowIntensity();
                    float fadeStart = light->GetShadowFadeDistance();
                    float fadeEnd = light->GetShadowDistance();
                    if (fadeStart > 0.0f && fadeEnd > 0.0f && fadeEnd > fadeStart)
                        intensity =
                            Lerp(intensity, 1.0f, Clamp((light->GetDistance() - fadeStart) / (fadeEnd - fadeStart), 0.0f, 1.0f));
                    float pcfValues = (1.0f - intensity);
                    float samples = 1.0f;
                    if (renderer->GetShadowQuality() == SHADOWQUALITY_PCF_16BIT || renderer->GetShadowQuality() == SHADOWQUALITY_PCF_24BIT)
                        samples = 4.0f;
                    graphics->SetShaderParameter(PSP_SHADOWINTENSITY, Vector4(pcfValues / samples, intensity, 0.0f, 0.0f));
                }

                float sizeX = 1.0f / (float)shadowMap->GetWidth();
                float sizeY = 1.0f / (float)shadowMap->GetHeight();
                graphics->SetShaderParameter(PSP_SHADOWMAPINVSIZE, Vector2(sizeX, sizeY));

                Vector4 lightSplits(M_LARGE_VALUE, M_LARGE_VALUE, M_LARGE_VALUE, M_LARGE_VALUE);
                if (lightQueue_->shadowSplits_.Size() > 1)
                    lightSplits.x_ = lightQueue_->shadowSplits_[0].farSplit_ / camera->GetFarClip();
                if (lightQueue_->shadowSplits_.Size() > 2)
                    lightSplits.y_ = lightQueue_->shadowSplits_[1].farSplit_ / camera->GetFarClip();
                if (lightQueue_->shadowSplits_.Size() > 3)
                    lightSplits.z_ = lightQueue_->shadowSplits_[2].farSplit_ / camera->GetFarClip();

                graphics->SetShaderParameter(PSP_SHADOWSPLITS, lightSplits);

                if (graphics->HasShaderParameter(PSP_VSMSHADOWPARAMS))
                    graphics->SetShaderParameter(PSP_VSMSHADOWPARAMS, renderer->GetVSMShadowParameters());

                if (light->GetShadowBias().normalOffset_ > 0.0f)
                {
                    Vector4 normalOffsetScale(Vector4::ZERO);

                    // Scale normal offset strength with the width of the shadow camera view
                    if (light->GetLightType() != LIGHT_DIRECTIONAL)
                    {
                        Camera* shadowCamera = lightQueue_->shadowSplits_[0].shadowCamera_;
                        normalOffsetScale.x_ = 2.0f * tanf(shadowCamera->GetFov() * M_DEGTORAD * 0.5f) * shadowCamera->GetFarClip();
                    }
                    else
                    {
                        normalOffsetScale.x_ = lightQueue_->shadowSplits_[0].shadowCamera_->GetOrthoSize();
                        if (lightQueue_->shadowSplits_.Size() > 1)
                            normalOffsetScale.y_ = lightQueue_->shadowSplits_[1].shadowCamera_->GetOrthoSize();
                        if (lightQueue_->shadowSplits_.Size() > 2)
                            normalOffsetScale.z_ = lightQueue_->shadowSplits_[2].shadowCamera_->GetOrthoSize();
                        if (lightQueue_->shadowSplits_.Size() > 3)
                            normalOffsetScale.w_ = lightQueue_->shadowSplits_[3].shadowCamera_->GetOrthoSize();
                    }

                    normalOffsetScale *= light->GetShadowBias().normalOffset_;
#ifdef GL_ES_VERSION_2_0
                    normalOffsetScale *= renderer->GetMobileNormalOffsetMul();
#endif
                    graphics->SetShaderParameter(VSP_NORMALOFFSETSCALE, normalOffsetScale);
                    graphics->SetShaderParameter(PSP_NORMALOFFSETSCALE, normalOffsetScale);
                }
            }
        }
        else if (lightQueue_->vertexLights_.Size() && graphics->HasShaderParameter(VSP_VERTEXLIGHTS) &&
                 graphics->NeedParameterUpdate(SP_LIGHT, lightQueue_))
        {
            Vector4 vertexLights[MAX_VERTEX_LIGHTS * 3];
            const Vector<Light*>& lights = lightQueue_->vertexLights_;

            for (i32 i = 0; i < lights.Size(); ++i)
            {
                Light* vertexLight = lights[i];
                Node* vertexLightNode = vertexLight->GetNode();
                LightType type = vertexLight->GetLightType();

                // Attenuation
                float invRange, cutoff, invCutoff;
                if (type == LIGHT_DIRECTIONAL)
                    invRange = 0.0f;
                else
                    invRange = 1.0f / Max(vertexLight->GetRange(), M_EPSILON);
                if (type == LIGHT_SPOT)
                {
                    cutoff = Cos(vertexLight->GetFov() * 0.5f);
                    invCutoff = 1.0f / (1.0f - cutoff);
                }
                else
                {
                    cutoff = -2.0f;
                    invCutoff = 1.0f;
                }

                // Color
                float fade = 1.0f;
                float fadeEnd = vertexLight->GetDrawDistance();
                float fadeStart = vertexLight->GetFadeDistance();

                // Do fade calculation for light if both fade & draw distance defined
                if (vertexLight->GetLightType() != LIGHT_DIRECTIONAL && fadeEnd > 0.0f && fadeStart > 0.0f && fadeStart < fadeEnd)
                    fade = Min(1.0f - (vertexLight->GetDistance() - fadeStart) / (fadeEnd - fadeStart), 1.0f);

                Color color = vertexLight->GetEffectiveColor() * fade;
                vertexLights[i * 3] = Vector4(color.r_, color.g_, color.b_, invRange);

                // Direction
                vertexLights[i * 3 + 1] = Vector4(-(vertexLightNode->GetWorldDirection()), cutoff);

                // Position
                vertexLights[i * 3 + 2] = Vector4(vertexLightNode->GetWorldPosition(), invCutoff);
            }

            graphics->SetShaderParameter(VSP_VERTEXLIGHTS, vertexLights[0].Data(), lights.Size() * 3 * 4);
        }
    }

    // Set zone texture if necessary
#ifndef GL_ES_VERSION_2_0
    if (zone_ && graphics->HasTextureUnit(TU_ZONE))
        graphics->SetTexture(TU_ZONE, zone_->GetZoneTexture());
#else
    // On OpenGL ES set the zone texture to the environment unit instead
    if (zone_ && zone_->GetZoneTexture() && graphics->HasTextureUnit(TU_ENVIRONMENT))
        graphics->SetTexture(TU_ENVIRONMENT, zone_->GetZoneTexture());
#endif

    // Store current material for Vulkan descriptor set creation
    // Vulkan needs to read textures from the material directly, not from textures_[] array
    // which can be modified by other systems (e.g., UI cleanup) between Prepare() and Draw()
    if (material_)
        URHO3D_LOGDEBUG("Batch::Prepare: Setting currentMaterial to " + material_->GetName());
    graphics->SetCurrentMaterial(material_);

    // Set material-specific shader parameters and textures
    if (material_)
    {
        bool needUpdate = graphics->NeedParameterUpdate(SP_MATERIAL, reinterpret_cast<const void*>(material_->GetShaderParameterHash()));

        if (needUpdate)
        {
            const HashMap<StringHash, MaterialShaderParameter>& parameters = material_->GetShaderParameters();
            for (HashMap<StringHash, MaterialShaderParameter>::ConstIterator i = parameters.Begin(); i != parameters.End(); ++i)
                graphics->SetShaderParameter(i->first_, i->second_.value_);
        }

        const HashMap<TextureUnit, SharedPtr<Texture>>& textures = material_->GetTextures();
        for (HashMap<TextureUnit, SharedPtr<Texture>>::ConstIterator i = textures.Begin(); i != textures.End(); ++i)
        {
            if (graphics->HasTextureUnit(i->first_))
                graphics->SetTexture(i->first_, i->second_.Get());
        }
    }

    // Set light-related textures
    if (light)
    {
        if (shadowMap && graphics->HasTextureUnit(TU_SHADOWMAP))
        {
            graphics->SetTexture(TU_SHADOWMAP, shadowMap);
        }
        if (graphics->HasTextureUnit(TU_LIGHTRAMP))
        {
            Texture* rampTexture = light->GetRampTexture();
            if (!rampTexture)
                rampTexture = renderer->GetDefaultLightRamp();
            graphics->SetTexture(TU_LIGHTRAMP, rampTexture);
        }
        if (graphics->HasTextureUnit(TU_LIGHTSHAPE))
        {
            Texture* shapeTexture = light->GetShapeTexture();
            if (!shapeTexture && light->GetLightType() == LIGHT_SPOT)
                shapeTexture = renderer->GetDefaultLightSpot();
            graphics->SetTexture(TU_LIGHTSHAPE, shapeTexture);
        }
    }
}

void Batch::Draw(View* view, Camera* camera, bool allowDepthWrite) const
{
    if (!geometry_->IsEmpty())
    {
        Prepare(view, camera, true, allowDepthWrite);
        geometry_->Draw(view->GetGraphics());
    }
}

void Batch::DrawToSecondaryCommandBuffer(View* view, Camera* camera, bool allowDepthWrite, void* secondaryCommandBuffer) const
{
    // For non-Vulkan builds or when secondary command buffer is not available, fall back to regular draw
    if (!secondaryCommandBuffer)
    {
        Draw(view, camera, allowDepthWrite);
        return;
    }

#ifdef URHO3D_VULKAN
    // Vulkan secondary command buffer recording with push constants
    if (!geometry_->IsEmpty() && worldTransform_)
    {
        Prepare(view, camera, true, allowDepthWrite);

        // Record push constants for per-batch transform data
        // Calculate normal matrix from model matrix for lighting
        Matrix3x4 normalMatrix = VulkanPushConstantManager::CalculateNormalMatrix(*worldTransform_);
        VulkanBatchPushConstants pushConstants(*worldTransform_);
        pushConstants.normalMatrix = normalMatrix;
        pushConstants.materialIndex = 0;  // Placeholder — per-material indexing not yet implemented
        pushConstants.batchFlags = 0;     // Placeholder — skinning/billboarding flags not yet implemented

        // Get pipeline layout from graphics implementation
        Graphics* graphics = view->GetGraphics();
        VulkanGraphicsImpl* vkImpl = graphics->GetImpl_Vulkan();
        VkPipelineLayout pipelineLayout = vkImpl ? vkImpl->GetCurrentPipelineLayout() : VK_NULL_HANDLE;

        // Record to secondary command buffer via Vulkan API
        VkCommandBuffer cmdBuffer = static_cast<VkCommandBuffer>(secondaryCommandBuffer);
        if (pipelineLayout)
        {
            vkCmdPushConstants(
                cmdBuffer,
                pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT,
                0,  // offset
                VulkanBatchPushConstants::SIZE,
                &pushConstants
            );
        }

        geometry_->Draw(view->GetGraphics());
    }
#else
    // Fall back to primary command buffer if not using Vulkan
    Draw(view, camera, allowDepthWrite);
#endif
}

bool Batch::RecordToIndirectBuffer(View* view, Camera* camera, bool allowDepthWrite, void* dispatcher) const
{
    // For non-Vulkan builds or when dispatcher is not available, fall back to regular draw
    if (!dispatcher)
    {
        Draw(view, camera, allowDepthWrite);
        return true;
    }

#ifdef URHO3D_VULKAN
    // Vulkan GPU-driven rendering via indirect draw dispatcher
    if (!geometry_ || geometry_->IsEmpty() || !worldTransform_)
        return false;

    // Prepare state (set shaders, materials, parameters)
    Prepare(view, camera, true, allowDepthWrite);

    // Cast dispatcher to correct type and record batch
    VulkanBatchDispatcher* batchDispatcher = static_cast<VulkanBatchDispatcher*>(dispatcher);
    return batchDispatcher->RecordBatch(this, view, camera);
#else
    // Fall back to immediate rendering if not using Vulkan
    Draw(view, camera, allowDepthWrite);
    return true;
#endif
}

void BatchGroup::SetInstancingData(void* lockedData, i32 stride, i32& freeIndex)
{
    assert(stride >= 0);

    // Do not use up buffer space if not going to draw as instanced
    if (geometryType_ != GEOM_INSTANCED)
        return;

    startIndex_ = freeIndex;
    unsigned char* buffer = static_cast<unsigned char*>(lockedData) + startIndex_ * stride;

    for (unsigned i = 0; i < instances_.Size(); ++i)
    {
        const InstanceData& instance = instances_[i];

        // Copy Matrix3x4 as-is for INSTANCED shader path
        memcpy(buffer, instance.worldTransform_, sizeof(Matrix3x4));

        if (instance.instancingData_)
            memcpy(buffer + sizeof(Matrix3x4), instance.instancingData_, stride - sizeof(Matrix3x4));

        buffer += stride;
    }

    freeIndex += instances_.Size();
}

void BatchGroup::Draw(View* view, Camera* camera, bool allowDepthWrite) const
{
    Graphics* graphics = view->GetGraphics();
    Renderer* renderer = view->GetRenderer();

    if (instances_.Size() && !geometry_->IsEmpty())
    {
        // Draw as individual objects if instancing not supported or could not fill the instancing buffer
        VertexBuffer* instanceBuffer = renderer->GetInstancingBuffer();
        if (!instanceBuffer || geometryType_ != GEOM_INSTANCED || startIndex_ == NINDEX)
        {
            Batch::Prepare(view, camera, false, allowDepthWrite);

            graphics->SetIndexBuffer(geometry_->GetIndexBuffer());
            graphics->SetVertexBuffers(geometry_->GetVertexBuffers());

            for (const InstanceData& instance : instances_)
            {
                if (graphics->NeedParameterUpdate(SP_OBJECT, instance.worldTransform_))
                    graphics->SetShaderParameter(VSP_MODEL, *instance.worldTransform_);

                graphics->Draw(geometry_->GetPrimitiveType(), geometry_->GetIndexStart(), geometry_->GetIndexCount(),
                    geometry_->GetVertexStart(), geometry_->GetVertexCount());
            }
        }
        else
        {
            Batch::Prepare(view, camera, false, allowDepthWrite);

            // Get the geometry vertex buffers, then add the instancing stream buffer
            // Hack: use a const_cast to avoid dynamic allocation of new temp vectors
            auto& vertexBuffers = const_cast<Vector<SharedPtr<VertexBuffer>>&>(
                geometry_->GetVertexBuffers());

            vertexBuffers.Push(SharedPtr<VertexBuffer>(instanceBuffer));

            graphics->SetIndexBuffer(geometry_->GetIndexBuffer());
            graphics->SetVertexBuffers(vertexBuffers, startIndex_);

            // BUGFIX: Call the 7-param DrawInstanced with correct parameter mapping:
            // DrawInstanced(type, indexStart, indexCount, baseVertexIndex, minVertex, vertexCount, instanceCount)
            // Note: startIndex_ (instance offset) is already applied in SetVertexBuffers above, not passed here
            graphics->DrawInstanced(geometry_->GetPrimitiveType(), geometry_->GetIndexStart(), geometry_->GetIndexCount(),
                geometry_->GetVertexStart(), 0, geometry_->GetVertexCount(), instances_.Size());

            // Remove the instancing buffer & element mask now
            vertexBuffers.Pop();
        }
    }
}

void BatchGroup::DrawToSecondaryCommandBuffer(View* view, Camera* camera, bool allowDepthWrite, void* secondaryCommandBuffer) const
{
    // For non-Vulkan builds or when secondary command buffer is not available, fall back to regular draw
    if (!secondaryCommandBuffer)
    {
        Draw(view, camera, allowDepthWrite);
        return;
    }

#ifdef URHO3D_VULKAN
    // Vulkan secondary command buffer recording with push constants
    Graphics* graphics = view->GetGraphics();
    Renderer* renderer = view->GetRenderer();
    VkCommandBuffer cmdBuffer = static_cast<VkCommandBuffer>(secondaryCommandBuffer);

    if (instances_.Size() && !geometry_->IsEmpty())
    {
        // Draw as individual objects if instancing not supported or could not fill the instancing buffer
        VertexBuffer* instanceBuffer = renderer->GetInstancingBuffer();
        if (!instanceBuffer || geometryType_ != GEOM_INSTANCED || startIndex_ == NINDEX)
        {
            Batch::Prepare(view, camera, false, allowDepthWrite);

            graphics->SetIndexBuffer(geometry_->GetIndexBuffer());
            graphics->SetVertexBuffers(geometry_->GetVertexBuffers());

            for (const InstanceData& instance : instances_)
            {
                if (graphics->NeedParameterUpdate(SP_OBJECT, instance.worldTransform_))
                    graphics->SetShaderParameter(VSP_MODEL, *instance.worldTransform_);

                // Record push constants for each instance
                if (instance.worldTransform_)
                {
                    Matrix3x4 normalMatrix = VulkanPushConstantManager::CalculateNormalMatrix(*instance.worldTransform_);
                    VulkanBatchPushConstants pushConstants(*instance.worldTransform_);
                    pushConstants.normalMatrix = normalMatrix;
                    pushConstants.materialIndex = 0;  // Placeholder — per-material indexing not yet implemented
                    pushConstants.batchFlags = 0;     // Placeholder — skinning/billboarding flags not yet implemented

                    // Get pipeline layout from graphics implementation
                    VulkanGraphicsImpl* vkImpl = graphics->GetImpl_Vulkan();
                    VkPipelineLayout pipelineLayout = vkImpl ? vkImpl->GetCurrentPipelineLayout() : VK_NULL_HANDLE;

                    if (pipelineLayout)
                    {
                        vkCmdPushConstants(
                            cmdBuffer,
                            pipelineLayout,
                            VK_SHADER_STAGE_VERTEX_BIT,
                            0,
                            VulkanBatchPushConstants::SIZE,
                            &pushConstants
                        );
                    }
                }

                graphics->Draw(geometry_->GetPrimitiveType(), geometry_->GetIndexStart(), geometry_->GetIndexCount(),
                    geometry_->GetVertexStart(), geometry_->GetVertexCount());
            }
        }
        else
        {
            Batch::Prepare(view, camera, false, allowDepthWrite);

            // Get the geometry vertex buffers, then add the instancing stream buffer
            // Hack: use a const_cast to avoid dynamic allocation of new temp vectors
            auto& vertexBuffers = const_cast<Vector<SharedPtr<VertexBuffer>>&>(
                geometry_->GetVertexBuffers());

            vertexBuffers.Push(SharedPtr<VertexBuffer>(instanceBuffer));

            graphics->SetIndexBuffer(geometry_->GetIndexBuffer());
            graphics->SetVertexBuffers(vertexBuffers, startIndex_);

            // Record push constants for the batch group as a whole (instanced)
            // For instanced rendering, we record once before DrawInstanced
            if (worldTransform_)
            {
                Matrix3x4 normalMatrix = VulkanPushConstantManager::CalculateNormalMatrix(*worldTransform_);
                VulkanBatchPushConstants pushConstants(*worldTransform_);
                pushConstants.normalMatrix = normalMatrix;
                pushConstants.materialIndex = 0;  // TODO: Per-material index
                pushConstants.batchFlags = 0;     // TODO: Batch flags

                // Get pipeline layout from graphics implementation
                VulkanGraphicsImpl* vkImpl = graphics->GetImpl_Vulkan();
                VkPipelineLayout pipelineLayout = vkImpl ? vkImpl->GetCurrentPipelineLayout() : VK_NULL_HANDLE;

                if (pipelineLayout)
                {
                    vkCmdPushConstants(
                        cmdBuffer,
                        pipelineLayout,
                        VK_SHADER_STAGE_VERTEX_BIT,
                        0,
                        VulkanBatchPushConstants::SIZE,
                        &pushConstants
                    );
                }
            }

            // BUGFIX: Call the 7-param DrawInstanced with correct parameter mapping:
            // DrawInstanced(type, indexStart, indexCount, baseVertexIndex, minVertex, vertexCount, instanceCount)
            // Note: startIndex_ (instance offset) is already applied in SetVertexBuffers above, not passed here
            graphics->DrawInstanced(geometry_->GetPrimitiveType(), geometry_->GetIndexStart(), geometry_->GetIndexCount(),
                geometry_->GetVertexStart(), 0, geometry_->GetVertexCount(), instances_.Size());

            // Remove the instancing buffer & element mask now
            vertexBuffers.Pop();
        }
    }
#else
    // Fall back to primary command buffer if not using Vulkan
    Draw(view, camera, allowDepthWrite);
#endif
}

bool BatchGroup::RecordToIndirectBuffer(View* view, Camera* camera, bool allowDepthWrite, void* dispatcher) const
{
    // For non-Vulkan builds or when dispatcher is not available, fall back to regular draw
    if (!dispatcher || instances_.Empty())
    {
        if (!dispatcher && !instances_.Empty())
            Draw(view, camera, allowDepthWrite);
        return true;
    }

#ifdef URHO3D_VULKAN
    // Vulkan GPU-driven rendering via indirect draw dispatcher for instanced data
    if (geometry_->IsEmpty())
        return true;

    // Prepare state once for entire group (single setup for all instances)
    Prepare(view, camera, false, allowDepthWrite);

    // Cast dispatcher to correct type
    VulkanBatchDispatcher* batchDispatcher = static_cast<VulkanBatchDispatcher*>(dispatcher);

    // Record batch group with all instances
    return batchDispatcher->RecordBatchGroup(this, view, camera);
#else
    // Fall back to immediate rendering if not using Vulkan
    Draw(view, camera, allowDepthWrite);
    return true;
#endif
}

hash32 BatchGroupKey::ToHash() const
{
    return (hash32)((size_t)zone_ / sizeof(Zone) + (size_t)lightQueue_ / sizeof(LightBatchQueue) + (size_t)pass_ / sizeof(Pass) +
                      (size_t)material_ / sizeof(Material) + (size_t)geometry_ / sizeof(Geometry)) + (u8)renderOrder_;
}

void BatchQueue::Clear(int maxSortedInstances)
{
    batches_.Clear();
    sortedBatches_.Clear();
    batchGroups_.Clear();
    maxSortedInstances_ = maxSortedInstances;
}

void BatchQueue::SortBackToFront()
{
    sortedBatches_.Resize(batches_.Size());

    for (i32 i = 0; i < batches_.Size(); ++i)
        sortedBatches_[i] = &batches_[i];

    Sort(sortedBatches_.Begin(), sortedBatches_.End(), CompareBatchesBackToFront);

    sortedBatchGroups_.Resize(batchGroups_.Size());

    unsigned index = 0;
    for (HashMap<BatchGroupKey, BatchGroup>::Iterator i = batchGroups_.Begin(); i != batchGroups_.End(); ++i)
        sortedBatchGroups_[index++] = &i->second_;

    Sort(sortedBatchGroups_.Begin(), sortedBatchGroups_.End(), CompareBatchGroupOrder);
}

void BatchQueue::SortFrontToBack()
{
    sortedBatches_.Clear();

    for (Batch& batch : batches_)
        sortedBatches_.Push(&batch);

    SortFrontToBack2Pass(sortedBatches_);

    // Sort each group front to back
    for (HashMap<BatchGroupKey, BatchGroup>::Iterator i = batchGroups_.Begin(); i != batchGroups_.End(); ++i)
    {
        if (i->second_.instances_.Size() <= maxSortedInstances_)
        {
            Sort(i->second_.instances_.Begin(), i->second_.instances_.End(), CompareInstancesFrontToBack);
            if (i->second_.instances_.Size())
                i->second_.distance_ = i->second_.instances_[0].distance_;
        }
        else
        {
            float minDistance = M_INFINITY;
            for (Vector<InstanceData>::ConstIterator j = i->second_.instances_.Begin(); j != i->second_.instances_.End(); ++j)
                minDistance = Min(minDistance, j->distance_);
            i->second_.distance_ = minDistance;
        }
    }

    sortedBatchGroups_.Resize(batchGroups_.Size());

    unsigned index = 0;
    for (HashMap<BatchGroupKey, BatchGroup>::Iterator i = batchGroups_.Begin(); i != batchGroups_.End(); ++i)
        sortedBatchGroups_[index++] = &i->second_;

    SortFrontToBack2Pass(reinterpret_cast<Vector<Batch*>& >(sortedBatchGroups_));
}

void BatchQueue::SortFrontToBack2Pass(Vector<Batch*>& batches)
{
    // Mobile devices likely use a tiled deferred approach, with which front-to-back sorting is irrelevant. The 2-pass
    // method is also time consuming, so just sort with state having priority
#ifdef GL_ES_VERSION_2_0
    Sort(batches.Begin(), batches.End(), CompareBatchesState);
#else
    // For desktop, first sort by distance and remap shader/material/geometry IDs in the sort key
    Sort(batches.Begin(), batches.End(), CompareBatchesFrontToBack);

    hash32 freeShaderID = 0;
    hash16 freeMaterialID = 0;
    hash16 freeGeometryID = 0;

    for (Vector<Batch*>::Iterator i = batches.Begin(); i != batches.End(); ++i)
    {
        Batch* batch = *i;

        hash32 shaderID = (hash32)(batch->sortKey_ >> 32u);
        HashMap<hash32, hash32>::ConstIterator j = shaderRemapping_.Find(shaderID);
        if (j != shaderRemapping_.End())
            shaderID = j->second_;
        else
        {
            shaderID = shaderRemapping_[shaderID] = freeShaderID | (shaderID & 0x80000000);
            ++freeShaderID;
        }

        hash16 materialID = (hash16)((batch->sortKey_ & 0xffff0000) >> 16u);
        HashMap<hash16, hash16>::ConstIterator k = materialRemapping_.Find(materialID);
        if (k != materialRemapping_.End())
            materialID = k->second_;
        else
        {
            materialID = materialRemapping_[materialID] = freeMaterialID;
            ++freeMaterialID;
        }

        hash16 geometryID = (hash16)(batch->sortKey_ & 0xffffu);
        HashMap<hash16, hash16>::ConstIterator l = geometryRemapping_.Find(geometryID);
        if (l != geometryRemapping_.End())
            geometryID = l->second_;
        else
        {
            geometryID = geometryRemapping_[geometryID] = freeGeometryID;
            ++freeGeometryID;
        }

        batch->sortKey_ = (((hash64)shaderID) << 32u) | (((hash64)materialID) << 16u) | geometryID;
    }

    shaderRemapping_.Clear();
    materialRemapping_.Clear();
    geometryRemapping_.Clear();

    // Finally sort again with the rewritten ID's
    Sort(batches.Begin(), batches.End(), CompareBatchesState);
#endif
}

void BatchQueue::SetInstancingData(void* lockedData, i32 stride, i32& freeIndex)
{
    assert(stride >= 0);
    for (HashMap<BatchGroupKey, BatchGroup>::Iterator i = batchGroups_.Begin(); i != batchGroups_.End(); ++i)
        i->second_.SetInstancingData(lockedData, stride, freeIndex);
}

void BatchQueue::Draw(View* view, Camera* camera, bool markToStencil, bool usingLightOptimization, bool allowDepthWrite) const
{
    Graphics* graphics = view->GetGraphics();
    Renderer* renderer = view->GetRenderer();

    // If View has set up its own light optimizations, do not disturb the stencil/scissor test settings
    if (!usingLightOptimization)
    {
        graphics->SetScissorTest(false);

        // During G-buffer rendering, mark opaque pixels' lightmask to stencil buffer if requested
        if (!markToStencil)
            graphics->SetStencilTest(false);
    }

    // Instanced
    for (Vector<BatchGroup*>::ConstIterator i = sortedBatchGroups_.Begin(); i != sortedBatchGroups_.End(); ++i)
    {
        BatchGroup* group = *i;
        if (markToStencil)
            graphics->SetStencilTest(true, CMP_ALWAYS, OP_REF, OP_KEEP, OP_KEEP, group->lightMask_);

        group->Draw(view, camera, allowDepthWrite);
    }
    // Non-instanced
    for (Vector<Batch*>::ConstIterator i = sortedBatches_.Begin(); i != sortedBatches_.End(); ++i)
    {
        Batch* batch = *i;
        if (markToStencil)
            graphics->SetStencilTest(true, CMP_ALWAYS, OP_REF, OP_KEEP, OP_KEEP, batch->lightMask_);
        if (!usingLightOptimization)
        {
            // If drawing an alpha batch, we can optimize fillrate by scissor test
            if (!batch->isBase_ && batch->lightQueue_)
                renderer->OptimizeLightByScissor(batch->lightQueue_->light_, camera);
            else
                graphics->SetScissorTest(false);
        }

        batch->Draw(view, camera, allowDepthWrite);
    }
}

void BatchQueue::RecordToIndirectBuffer(View* view, Camera* camera, bool markToStencil, bool usingLightOptimization, bool allowDepthWrite, void* dispatcher) const
{
    // If no dispatcher provided, fall back to immediate rendering
    if (!dispatcher)
    {
        Draw(view, camera, markToStencil, usingLightOptimization, allowDepthWrite);
        return;
    }

#ifdef URHO3D_VULKAN
    // Phase 14.6: Record batch queue to indirect draw buffer
    // Record all batch groups and individual batches to dispatcher for GPU-driven rendering

    // Record instanced batch groups
    for (Vector<BatchGroup*>::ConstIterator i = sortedBatchGroups_.Begin(); i != sortedBatchGroups_.End(); ++i)
    {
        BatchGroup* group = *i;
        // Phase 14.6.1: stencil masking per batch group — deferred until indirect rendering is prioritized
        group->RecordToIndirectBuffer(view, camera, allowDepthWrite, dispatcher);
    }

    // Record non-instanced batches
    for (Vector<Batch*>::ConstIterator i = sortedBatches_.Begin(); i != sortedBatches_.End(); ++i)
    {
        Batch* batch = *i;
        // Phase 14.6.2-3: stencil masking + scissor optimization — deferred until indirect rendering is prioritized

        batch->RecordToIndirectBuffer(view, camera, allowDepthWrite, dispatcher);
    }

    /// \brief Phase 14.6.4 - Submit all recorded indirect draw commands
    /// \details After all batches have been recorded to GPU indirect buffers,
    /// submit them together as a single indirect draw to minimize CPU overhead.
    /// This replaces immediate per-batch vkCmdDrawIndexed() calls with
    /// a single vkCmdDrawIndexedIndirect() call for true GPU-driven rendering.

    VulkanBatchDispatcher* batchDispatcher = static_cast<VulkanBatchDispatcher*>(dispatcher);
    if (batchDispatcher)
    {
        // Get current frame's command buffer from graphics implementation
        // This buffer has already been allocated and begun recording in BeginFrame()
        Graphics* graphics = view->GetGraphics();
        if (graphics)
        {
            VkCommandBuffer cmdBuffer = graphics->GetImpl_Vulkan() ?
                graphics->GetImpl_Vulkan()->GetFrameCommandBuffer() : VK_NULL_HANDLE;

            if (cmdBuffer != VK_NULL_HANDLE)
            {
                // Submit all recorded indirect draws with proper state binding
                // This is called once per view after all batches in this queue are recorded
                batchDispatcher->SubmitIndirectDraws(cmdBuffer);
            }
        }
    }
#else
    // Non-Vulkan fallback: immediate rendering
    Draw(view, camera, markToStencil, usingLightOptimization, allowDepthWrite);
#endif
}

i32 BatchQueue::GetNumInstances() const
{
    i32 total = 0;

    for (HashMap<BatchGroupKey, BatchGroup>::ConstIterator i = batchGroups_.Begin(); i != batchGroups_.End(); ++i)
    {
        if (i->second_.geometryType_ == GEOM_INSTANCED)
            total += i->second_.instances_.Size();
    }

    return total;
}

}
