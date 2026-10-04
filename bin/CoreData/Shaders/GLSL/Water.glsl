#include "Uniforms.glsl"
#include "Samplers.glsl"
#include "Transform.glsl"
#include "ScreenPos.glsl"
#include "Fog.glsl"

#ifndef GL_ES
varying vec4 vScreenPos;
varying vec2 vReflectUV;
varying vec2 vWaterUV;
varying vec4 vEyeVec;
#else
varying highp vec4 vScreenPos;
varying highp vec2 vReflectUV;
varying highp vec2 vWaterUV;
varying highp vec4 vEyeVec;
#endif
varying vec3 vNormal;

#ifdef COMPILEVS
uniform vec2 cNoiseSpeed;
uniform float cNoiseTiling;
#endif
#ifdef COMPILEPS
uniform float cNoiseStrength;
uniform float cFresnelPower;
uniform vec3 cWaterTint;
uniform vec3 cShallowColor;
uniform vec3 cDeepColor;
uniform float cDepthScale;
uniform float cRippleStrength;
uniform float cRippleScale;
uniform vec3 cMoonDir;
uniform vec3 cMoonColor;
uniform float cMoonSpecular;
uniform vec3 cSunDir;
uniform vec3 cSunColor;
uniform float cSunSpecular;
#endif

void VS()
{
    mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = GetWorldPos(modelMatrix);
    gl_Position = GetClipPos(worldPos);
    // Bias water surface slightly toward camera to win depth test at terrain intersection.
    // Prevents Z-fight white line at shoreline, especially visible from below.
    // NOTE: this is 0.000002 NDC (constant after dividing by w). The old value 0.0005
    // was 250x too large: at camera Y=75 the NDC difference between water (Y=5) and a
    // box at Y=6 is only ~4e-5 — a 0.0005 bias overrides it, making the box appear
    // submerged. 0.000002 is still ~30x the 24-bit depth buffer precision at close range
    // (sufficient for z-fighting prevention) while not covering objects above water.
    gl_Position.z -= 0.000002 * gl_Position.w;
    vScreenPos = GetScreenPos(gl_Position);
    // GetQuadTexCoord() returns a vec2 that is OK for quad rendering; multiply it with output W
    // coordinate to make it work with arbitrary meshes such as the water plane (perform divide in pixel shader)
    // Also because the quadTexCoord is based on the clip position, and Y is flipped when rendering to a texture
    // on OpenGL, must flip again to cancel it out
    vReflectUV = GetQuadTexCoord(gl_Position);
    vReflectUV.y = 1.0 - vReflectUV.y;
    vReflectUV *= gl_Position.w;
    vWaterUV = iTexCoord * cNoiseTiling + cElapsedTime * cNoiseSpeed;
    vNormal = GetWorldNormal(modelMatrix);
    vEyeVec = vec4(cCameraPos - worldPos, GetDepth(gl_Position));
}

void PS()
{
    vec2 refractUV = vScreenPos.xy / vScreenPos.w;
    vec2 reflectUV = vReflectUV.xy / vScreenPos.w;

    vec2 noise = (texture2D(sNormalMap, vWaterUV).rg - 0.5) * cNoiseStrength;

    // Ripple normal perturbation — gradient of the wave height texture.
    // sEmissiveMap is the WaterRippleSystem texture (L8, centre 128).
    // cRippleScale = simulation world diameter (WORLD_HALF * 2).
    // worldXZ reconstructed from eye vector: vEyeVec.xyz = cCameraPos - worldPos.
    if (cRippleStrength > 0.0)
    {
        vec2 worldXZ = vec2(cCameraPosPS.x - vEyeVec.x, cCameraPosPS.z - vEyeVec.z);
        vec2 rippleUV = worldXZ / cRippleScale + 0.5;
        float ts = 1.0 / 128.0;
        float hPX = texture2D(sEmissiveMap, rippleUV + vec2(ts,  0.0)).r;
        float hNX = texture2D(sEmissiveMap, rippleUV - vec2(ts,  0.0)).r;
        float hPZ = texture2D(sEmissiveMap, rippleUV + vec2(0.0, ts )).r;
        float hNZ = texture2D(sEmissiveMap, rippleUV - vec2(0.0, ts )).r;
        noise += vec2(hPX - hNX, hPZ - hNZ) * cRippleStrength;
    }

    refractUV += noise;
    // Do not shift reflect UV coordinate upward, because it will reveal the clipping of geometry below water
    if (noise.y < 0.0)
        noise.y = 0.0;
    reflectUV += noise;

    vec3 refractColor = texture2D(sEnvMap, refractUV).rgb * cWaterTint;
    vec3 reflectColor = texture2D(sDiffMap, reflectUV).rgb;

    float eyeDotN = dot(normalize(vEyeVec.xyz), vNormal);

    vec3 finalColor;
    if (eyeDotN < 0.0)
    {
        // Viewed from below — Snell's window: looking straight up = see through,
        // grazing = subtle water tint. Keep tint low to avoid painting the sky aqua.
        float transparency = clamp(-eyeDotN, 0.0, 1.0);
        vec3 waterColor = mix(cShallowColor, cDeepColor, 0.5);
        float tint = 0.15 * (1.0 - transparency);
        finalColor = mix(refractColor, waterColor, tint);
    }
    else
    {
        float fresnel = pow(1.0 - clamp(eyeDotN, 0.0, 1.0), cFresnelPower);
        fresnel = min(fresnel, 0.7);

        // Depth-based water color: steep view = deep, grazing = shallow
        float depthFactor = clamp(eyeDotN * cDepthScale, 0.0, 1.0);
        vec3 waterColor = mix(cShallowColor, cDeepColor, depthFactor);

        // Blend refraction with water color — more tint at grazing angles, minimum floor when looking straight down
        // so the water surface never becomes fully transparent (which would create a camera-chasing transparent disc)
        float tintAmount = 0.08 + 0.12 * (1.0 - eyeDotN);
        vec3 tintedRefract = mix(refractColor, waterColor, tintAmount);
        finalColor = mix(tintedRefract, reflectColor, fresnel);
    }

    // Sun / moon Phong specular glint — independent of Fresnel so visible from any angle.
    // Only when viewed from above water (eyeDotN > 0).
    if (eyeDotN > 0.0)
    {
        vec3 eyeDir = normalize(vEyeVec.xyz);
        if (cMoonSpecular > 0.0)
        {
            vec3 reflMoon = reflect(-cMoonDir, vNormal);
            float moonSpec = pow(max(dot(eyeDir, reflMoon), 0.0), 2048.0);
            finalColor += cMoonColor * moonSpec * cMoonSpecular * 0.6;
        }
        if (cSunSpecular > 0.0)
        {
            vec3 reflSun = reflect(-cSunDir, vNormal);
            float sunSpec = pow(max(dot(eyeDir, reflSun), 0.0), 256.0);
            finalColor += cSunColor * sunSpec * cSunSpecular * 10.0;
        }
    }

    gl_FragColor = vec4(GetFog(finalColor, GetFogFactor(vEyeVec.w)), 1.0);
}
