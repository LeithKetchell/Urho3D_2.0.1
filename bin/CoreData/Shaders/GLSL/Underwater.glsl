#include "Uniforms.glsl"
#include "Samplers.glsl"
#include "Transform.glsl"
#include "ScreenPos.glsl"

varying vec2 vScreenPos;
varying vec3 vFarRay;
#ifdef ORTHO
    varying vec3 vNearRay;
#endif

#ifdef COMPILEPS
uniform float cWaterLevel;
uniform float cNoiseStrength;
uniform float cNoiseTiling;
uniform vec2 cNoiseSpeed;
uniform vec3 cUnderwaterColor;
uniform float cTintIntensity;
uniform float cDepthFalloff;
uniform float cBreachDepth;
uniform float cMainCameraY;
#endif

void VS()
{
    mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = GetWorldPos(modelMatrix);
    gl_Position = GetClipPos(worldPos);
    vScreenPos = GetScreenPosPreDiv(gl_Position);
    vFarRay = GetFarRay(gl_Position);
    #ifdef ORTHO
        vNearRay = GetNearRay(gl_Position);
    #endif
}

void PS()
{
    #ifdef UNDERWATER
    // Read hardware depth
    #ifdef HWDEPTH
        float hwDepth = texture2D(sDepthBuffer, vScreenPos).r;
    #else
        float hwDepth = DecodeDepth(texture2D(sDepthBuffer, vScreenPos).rgb);
    #endif

    // Reconstruct world position from depth
    float depth = ReconstructDepth(hwDepth);
    #ifdef ORTHO
        vec3 worldPos = mix(vNearRay, vFarRay, depth) + cCameraPosPS;
    #else
        vec3 worldPos = vFarRay * depth + cCameraPosPS;
    #endif

    // Camera above water — slightly darken submerged pixels
    if (cMainCameraY >= cWaterLevel)
    {
        vec4 aboveColor = texture2D(sDiffMap, vScreenPos);
        if (hwDepth < 0.999 && worldPos.y < cWaterLevel)
        {
            float submerged = clamp((cWaterLevel - worldPos.y) * cDepthFalloff, 0.0, 1.0);
            aboveColor.rgb *= 1.0 - submerged * 0.05;
        }
        gl_FragColor = aboveColor;
        return;
    }

    // Breach fade — how submerged the camera is (0 = at surface, 1 = fully under)
    float breachFade = clamp((cWaterLevel - cMainCameraY) / cBreachDepth, 0.0, 1.0);

    vec4 passThrough = texture2D(sDiffMap, vScreenPos);

    // How far below the water surface this pixel is
    float depthBelowWater = cWaterLevel - worldPos.y;

    // Skybox / far plane — darken toward murky depth, not aqua tint
    if (hwDepth > 0.999)
    {
        float darkness = clamp((cWaterLevel - cMainCameraY) * cDepthFalloff, 0.0, 0.85) * breachFade;
        // Darken the sky toward near-black when deep underwater
        // Use passThrough color scaled down, not underwater tint — avoids aqua flood at night
        gl_FragColor = vec4(passThrough.rgb * (1.0 - darkness), 1.0);
        return;
    }

    // Blend factor: 0 = above water surface, 1 = fully submerged
    // smoothstep over a 1-unit band to avoid hard-edge flicker
    float surfaceBlend = smoothstep(-0.5, 0.5, depthBelowWater);

    // Surface distortion (for above-water geometry seen from below)
    vec2 surfNoiseUV = vScreenPos * cNoiseTiling * 1.5 + cElapsedTimePS * cNoiseSpeed * 1.5;
    vec2 surfNoise = (texture2D(sNormalMap, surfNoiseUV).rg - 0.5) * cNoiseStrength * 0.3 * breachFade;
    vec2 surfUV = clamp(vScreenPos + surfNoise, vec2(0.001), vec2(0.999));
    vec4 surfColor = texture2D(sDiffMap, surfUV);
    // Darken surface view instead of tinting aqua — prevents sky flooding
    vec3 surfResult = surfColor.rgb * (1.0 - 0.3 * breachFade);

    // Underwater distortion (for submerged geometry)
    vec2 noiseUV = vScreenPos * cNoiseTiling + cElapsedTimePS * cNoiseSpeed;
    vec2 noise = (texture2D(sNormalMap, noiseUV).rg - 0.5) * cNoiseStrength * breachFade;
    vec2 distortedUV = clamp(vScreenPos + noise, vec2(0.001), vec2(0.999));
    vec4 sceneColor = texture2D(sDiffMap, distortedUV);
    float tint = clamp(depthBelowWater * cDepthFalloff, 0.0, 1.0) * breachFade;
    vec3 deepColor = cUnderwaterColor * (1.0 - tint * 0.7);
    vec3 underwaterResult = mix(sceneColor.rgb, deepColor, tint * cTintIntensity);

    // Blend between surface and underwater treatment
    gl_FragColor = vec4(mix(surfResult, underwaterResult, surfaceBlend), 1.0);
    #endif
}
