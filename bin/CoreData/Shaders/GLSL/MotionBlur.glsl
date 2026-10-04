#include "Uniforms.glsl"
#include "Samplers.glsl"
#include "Transform.glsl"
#include "ScreenPos.glsl"

varying vec2 vTexCoord;
varying vec2 vScreenPos;

#ifdef COMPILEPS
uniform mat4 cPrevViewProj;
uniform float cMotionBlurStrength;
uniform float cMotionBlurSamples;
#endif

void VS()
{
    mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = GetWorldPos(modelMatrix);
    gl_Position = GetClipPos(worldPos);
    vTexCoord = GetQuadTexCoord(gl_Position);
    vScreenPos = GetScreenPosPreDiv(gl_Position);
}

void PS()
{
    #ifdef MOTIONBLUR
    // Read hardware depth
    #ifdef HWDEPTH
        float hwDepth = texture2D(sDepthBuffer, vScreenPos).r;
    #else
        float hwDepth = DecodeDepth(texture2D(sDepthBuffer, vScreenPos).rgb);
    #endif

    // Early exit for skybox / far plane
    if (hwDepth > 0.999)
    {
        gl_FragColor = texture2D(sDiffMap, vScreenPos);
        return;
    }

    // Reconstruct NDC position from screen UV + hardware depth
    // vScreenPos is [0,1], convert to NDC [-1,1]
    vec4 currentNDC = vec4(vScreenPos * 2.0 - 1.0, hwDepth * 2.0 - 1.0, 1.0);

    // cPrevViewProj is a REPROJECTION matrix: inverse(currentVP) * prevVP
    // Urho3D row-major → GLSL column-major, so M * v in GLSL = v * M in Urho3D
    vec4 prevClip = cPrevViewProj * currentNDC;

    // Safety: if matrix not yet set
    if (abs(prevClip.w) < 0.0001)
    {
        gl_FragColor = texture2D(sDiffMap, vScreenPos);
        return;
    }

    vec2 prevScreenPos = (prevClip.xy / prevClip.w) * 0.5 + 0.5;

    // Screen-space velocity
    vec2 velocity = (vScreenPos - prevScreenPos) * cMotionBlurStrength;

    // Clamp velocity to prevent extreme blur
    float velocityLen = length(velocity);
    float maxVelocity = 0.05;
    if (velocityLen > maxVelocity)
        velocity *= maxVelocity / velocityLen;

    // Accumulate samples along velocity vector
    int numSamples = int(cMotionBlurSamples);
    vec4 color = texture2D(sDiffMap, vScreenPos);

    for (int i = 1; i < numSamples; ++i)
    {
        float t = float(i) / float(numSamples - 1) - 0.5;
        vec2 samplePos = clamp(vScreenPos + velocity * t, vec2(0.001), vec2(0.999));
        color += texture2D(sDiffMap, samplePos);
    }
    color /= float(numSamples);

    gl_FragColor = color;
    #endif
}
