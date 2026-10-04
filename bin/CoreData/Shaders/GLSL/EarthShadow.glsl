#include "Uniforms.glsl"
#include "Samplers.glsl"
#include "Transform.glsl"

varying vec2 vTexCoord;

void VS()
{
    mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = GetWorldPos(modelMatrix);
    gl_Position = GetClipPos(worldPos);
    vTexCoord = GetTexCoord(iTexCoord);
}

void PS()
{
    // Earth shadow disc: dark center, feathered alpha edges.
    // The disc is positioned by the CPU to overlap the moon billboard.
    vec2 uv = vTexCoord - vec2(0.5);
    float dist = length(uv) * 2.0;  // 0 at center, 1 at edge

    // Solid dark interior with soft feathered edge (10% of radius)
    float shadow = 1.0 - smoothstep(0.80, 1.0, dist);

    if (shadow < 0.01)
        discard;

    // Very faint earthshine in the shadow (blue-grey tint, not pure black)
    vec3 earthshine = vec3(0.02, 0.025, 0.04);

    gl_FragColor = vec4(earthshine, shadow * cMatDiffColor.a);
    gl_FragDepth = 0.9998;  // Slightly in front of moon (0.9999)
}
