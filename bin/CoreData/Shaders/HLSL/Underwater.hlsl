#include "Uniforms.hlsl"
#include "Samplers.hlsl"
#include "Transform.hlsl"
#include "ScreenPos.hlsl"

void VS(float4 iPos : POSITION,
    out float2 oScreenPos : TEXCOORD0,
    out float3 oFarRay : TEXCOORD1,
    #ifdef ORTHO
        out float3 oNearRay : TEXCOORD2,
    #endif
    out float4 oPos : OUTPOSITION)
{
    float4x3 modelMatrix = iModelMatrix;
    float3 worldPos = GetWorldPos(modelMatrix);
    oPos = GetClipPos(worldPos);
    oScreenPos = GetScreenPosPreDiv(oPos);
    oFarRay = GetFarRay(oPos);
    #ifdef ORTHO
        oNearRay = GetNearRay(oPos);
    #endif
}

void PS(float2 iScreenPos : TEXCOORD0,
    float3 iFarRay : TEXCOORD1,
    #ifdef ORTHO
        float3 iNearRay : TEXCOORD2,
    #endif
    out float4 oColor : OUTCOLOR0)
{
    #ifdef UNDERWATER
    // Read hardware depth
    #ifdef HWDEPTH
        float hwDepth = Sample2D(DepthBuffer, iScreenPos).r;
    #else
        float hwDepth = DecodeDepth(Sample2D(DepthBuffer, iScreenPos).rgb);
    #endif

    // Reconstruct world position from depth
    float depth = ReconstructDepth(hwDepth);
    #ifdef ORTHO
        float3 worldPos = lerp(iNearRay, iFarRay, depth) + cCameraPosPS;
    #else
        float3 worldPos = iFarRay * depth + cCameraPosPS;
    #endif

    // Camera above water — slightly darken submerged pixels
    if (cMainCameraY >= cWaterLevel)
    {
        float4 aboveColor = Sample2D(DiffMap, iScreenPos);
        if (hwDepth < 0.999 && worldPos.y < cWaterLevel)
        {
            float submerged = clamp((cWaterLevel - worldPos.y) * cDepthFalloff, 0.0, 1.0);
            aboveColor.rgb *= 1.0 - submerged * 0.05;
        }
        oColor = aboveColor;
        return;
    }

    // Breach fade — how submerged the camera is (0 = at surface, 1 = fully under)
    float breachFade = clamp((cWaterLevel - cMainCameraY) / cBreachDepth, 0.0, 1.0);

    float4 passThrough = Sample2D(DiffMap, iScreenPos);

    // How far below the water surface this pixel is
    float depthBelowWater = cWaterLevel - worldPos.y;

    // Skybox / far plane — darken toward underwater color
    if (hwDepth > 0.999)
    {
        float darkness = clamp((cWaterLevel - cMainCameraY) * cDepthFalloff, 0.0, 1.0) * breachFade;
        oColor = lerp(passThrough, float4(cUnderwaterColor * 0.3, 1.0), darkness);
        return;
    }

    // Above water geometry seen from below — strong surface distortion
    if (depthBelowWater <= 0.0)
    {
        float2 surfNoiseUV = iScreenPos * cNoiseTiling * 1.5 + cElapsedTimePS * cNoiseSpeed * 1.5;
        float2 surfNoise = (Sample2D(NormalMap, surfNoiseUV).rg - 0.5) * cNoiseStrength * 3.0 * breachFade;
        float2 surfUV = clamp(iScreenPos + surfNoise, float2(0.001, 0.001), float2(0.999, 0.999));
        float4 surfColor = Sample2D(DiffMap, surfUV);
        oColor = float4(surfColor.rgb * 1.15, 1.0);
        return;
    }

    // Below water — apply turbulence distortion scaled by breach fade
    float2 noiseUV = iScreenPos * cNoiseTiling + cElapsedTimePS * cNoiseSpeed;
    float2 noise = (Sample2D(NormalMap, noiseUV).rg - 0.5) * cNoiseStrength * breachFade;
    float2 distortedUV = clamp(iScreenPos + noise, float2(0.001, 0.001), float2(0.999, 0.999));

    float4 sceneColor = Sample2D(DiffMap, distortedUV);

    // Tint and darken based on depth below water surface
    float tint = clamp(depthBelowWater * cDepthFalloff, 0.0, 1.0) * breachFade;
    float3 deepColor = cUnderwaterColor * (1.0 - tint * 0.7);
    oColor = lerp(sceneColor, float4(deepColor, 1.0), tint * cTintIntensity);
    #endif
}
