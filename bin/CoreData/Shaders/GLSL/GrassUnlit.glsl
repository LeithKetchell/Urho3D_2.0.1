#include "Uniforms.glsl"
#include "Samplers.glsl"
#include "Transform.glsl"
#include "ScreenPos.glsl"
#include "Fog.glsl"

// Wind parameters
uniform float cWindHeightFactor;
uniform float cWindHeightPivot;
uniform float cWindPeriod;
uniform vec2 cWindWorldSpacing;

// Varyings — identical in VS and PS to avoid Vulkan location mismatch
varying vec2 vTexCoord;
varying vec4 vWorldPos;

void VS()
{
    mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = GetWorldPos(modelMatrix);

    // Wind animation: sway increases with height above pivot
    float stemDistance = iPos.y;
    float windStrength = max(stemDistance - cWindHeightPivot, 0.0) * cWindHeightFactor;
    float windPeriod = cElapsedTime * cWindPeriod + dot(worldPos.xz, cWindWorldSpacing);
    worldPos.x += windStrength * sin(windPeriod);
    worldPos.z -= windStrength * cos(windPeriod);

    gl_Position = GetClipPos(worldPos);
    vTexCoord = GetTexCoord(iTexCoord);
    vWorldPos = vec4(worldPos, GetDepth(gl_Position));
}

void PS()
{
    ApplyClipPlane();

    #ifdef DIFFMAP
        vec4 diffColor = cMatDiffColor * texture2D(sDiffMap, vTexCoord);
        #ifdef ALPHAMASK
            if (diffColor.a < 0.5)
                discard;
        #endif
    #else
        vec4 diffColor = cMatDiffColor;
    #endif

    // Fog
    #ifdef HEIGHTFOG
        float fogFactor = GetHeightFogFactor(vWorldPos.w, vWorldPos.y);
    #else
        float fogFactor = GetFogFactor(vWorldPos.w);
    #endif

    gl_FragColor = vec4(GetFog(diffColor.rgb, fogFactor), diffColor.a);
}
