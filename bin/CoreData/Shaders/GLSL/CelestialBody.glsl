#include "Uniforms.glsl"
#include "Samplers.glsl"
#include "Transform.glsl"

varying vec2 vTexCoord;

#ifdef COMPILEPS
#endif

void VS()
{
    mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = GetWorldPos(modelMatrix);
    gl_Position = GetClipPos(worldPos);
    vTexCoord = GetTexCoord(iTexCoord);
}

void PS()
{
    // Sample body color — texture if DIFFMAP defined, flat color otherwise
    #ifdef DIFFMAP
        vec4 bodyColor = texture2D(sDiffMap, vTexCoord);
        bodyColor.rgb *= cMatDiffColor.rgb;
        // Combine luminance alpha with radial falloff to kill dark pixels and quad acne
        float lum = dot(bodyColor.rgb, vec3(0.299, 0.587, 0.114));
        float lumAlpha = smoothstep(0.05, 0.2, lum);
        vec2 uv = vTexCoord - vec2(0.5);
        float dist = length(uv) * 2.0;  // 0 at center, 1 at edge
        float radialAlpha = 1.0 - smoothstep(0.85, 1.0, dist);
        bodyColor.a = lumAlpha * radialAlpha;
    #else
        // Circular disc for flat-color bodies (sun)
        vec2 uv = vTexCoord - vec2(0.5);
        float dist = length(uv) * 2.0;
        float disc = 1.0 - smoothstep(0.85, 1.0, dist);
        if (disc < 0.01)
            discard;
        vec4 bodyColor = vec4(cMatDiffColor.rgb, disc);
    #endif

    // Moon phase rendering now uses a separate shadow disc billboard
    // positioned in front of the moon — no shader terminator needed.

    // Simple alpha from texture and material color
    float alpha = bodyColor.a * cMatDiffColor.a;

    if (alpha < 0.01)
        discard;

    gl_FragColor = vec4(bodyColor.rgb, alpha);
    gl_FragDepth = 0.9999;
}
