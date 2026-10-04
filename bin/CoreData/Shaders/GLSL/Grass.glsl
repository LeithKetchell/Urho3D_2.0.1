#include "Uniforms.glsl"
#include "Samplers.glsl"
#include "Transform.glsl"
#include "ScreenPos.glsl"
#include "Lighting.glsl"
#include "Fog.glsl"

// Grass uniforms
uniform vec3 cGrassOrigin;        // World origin of grass grid (snapped to cell)
uniform vec2 cGrassParams;        // x=gridSize, y=cellSize
uniform vec2 cTerrainSize;        // Terrain world size XZ
uniform vec2 cTerrainOffset;      // Terrain world origin XZ
uniform float cGrassRadius;       // Max visible radius from camera
uniform float cGrassFadeWidth;    // Fade distance at edge
uniform float cWindStrength;      // Wind intensity
uniform vec2 cWindDirection;      // XZ wind vector (normalized)
uniform vec4 cPhysShape0;         // xyz=center, w=radius (player)
uniform vec4 cPhysShape1;         // xyz=center, w=radius (animal)
uniform vec4 cPhysShape2;         // xyz=center, w=radius (animal)
uniform vec4 cPhysShape3;         // xyz=center, w=radius (animal)
uniform vec4 cSeasonalTint;       // RGBA seasonal color multiplier
uniform float cHeightScale;       // heightmap pixel-to-world: 255.0 * spacing.y
uniform float cTerrainYOffset;    // terrain node Y position (added to sampled height)
uniform float cWaterLevel;        // world Y below which grass is suppressed

// Varyings — identical in VS and PS to avoid Vulkan location mismatch
varying vec2 vTexCoord;
varying vec4 vWorldPos;
varying float vFade;
varying vec3 vLighting;

#ifdef COMPILEVS

// VS needs sampler access for vertex texture fetch (Samplers.glsl is PS-only)
uniform sampler2D sNormalMap;   // heightmap (TU_NORMAL = unit 1)
#ifdef DENSITYMAP
uniform sampler2D sSpecMap;     // density map (TU_SPECULAR = unit 2)
#endif
// texture2D→texture alias is PS-only in Samplers.glsl; VS needs it for VTF
#ifdef GL3
#define texture2D texture
#endif

// Triangle wave — cheaper than sin
float triangleWave(float x)
{
    return abs(fract(x + 0.5) * 2.0 - 1.0);
}

void VS()
{
    // iPos.xz = local grid offset from origin
    // iPos.y = height along blade (0=base, tip=bladeHeight)
    // iTexCoord.x = U texture coord
    // iTexCoord.y = height factor (0=base, 1=tip)
    // iNormal.x = per-blade random hash (0-1)
    // iNormal.y = blade height scale

    float heightFactor = iTexCoord.y;  // 0 at base, 1 at tip
    float bladeRandom = iNormal.x;
    float bladeScale = iNormal.y;

    // World XZ position from grid + origin
    vec3 worldPos;
    worldPos.x = iPos.x + cGrassOrigin.x;
    worldPos.z = iPos.z + cGrassOrigin.z;

    // Terrain heightmap UV (0-1 across terrain)
    vec2 terrainUV = (worldPos.xz - cTerrainOffset) / cTerrainSize;

    // Early bail: off-terrain blades collapse to degenerate
    if (terrainUV.x < 0.0 || terrainUV.x > 1.0 || terrainUV.y < 0.0 || terrainUV.y > 1.0)
    {
        gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
        vTexCoord = vec2(0.0);
        vWorldPos = vec4(0.0);
        vFade = 0.0;
        vLighting = vec3(0.0);
        return;
    }

    // Sample heightmap via vertex texture fetch (texture unit 1)
    // Flip V because Urho3D terrain heightmap is stored Z-inverted
    vec2 hmUV = vec2(terrainUV.x, 1.0 - terrainUV.y);
    float rawHeight = texture2D(sNormalMap, hmUV).r;
    // Convert from 0-1 normalized pixel to world height + terrain Y offset
    float terrainHeight = cTerrainYOffset + rawHeight * cHeightScale;

    // Suppress grass below water level
    if (terrainHeight < cWaterLevel + 0.3)
    {
        gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
        vTexCoord = vec2(0.0);
        vWorldPos = vec4(0.0);
        vFade = 0.0;
        vLighting = vec3(0.0);
        return;
    }

    // Density from texture unit 2 (splat map or dedicated) — default 1.0 if no map
    float density = 1.0;
    #ifdef DENSITYMAP
        density = texture2D(sSpecMap, terrainUV).r;
    #endif

    // Distance fade
    vec2 toCam = worldPos.xz - cCameraPos.xz;
    float dist = length(toCam);
    float fade = smoothstep(cGrassRadius, cGrassRadius - cGrassFadeWidth, dist);

    // Combined density + fade — collapse blade if zero
    float visibility = density * fade * bladeScale;
    if (visibility < 0.01)
    {
        gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
        vTexCoord = vec2(0.0);
        vWorldPos = vec4(0.0);
        vFade = 0.0;
        vLighting = vec3(0.0);
        return;
    }

    // Place base on terrain
    worldPos.y = terrainHeight + iPos.y * visibility;

    // Wind — 4-frequency Crysis-style
    if (heightFactor > 0.0)
    {
        float t = cElapsedTime;
        float phase1 = t * 1.975 + dot(worldPos.xz, vec2(0.15, 0.13));
        float phase2 = t * 0.793 + dot(worldPos.xz, vec2(0.07, 0.11));
        float phase3 = t * 0.375 + dot(worldPos.xz, vec2(0.03, 0.05));
        float phase4 = t * 0.193 + dot(worldPos.xz, vec2(0.01, 0.02));

        float wind = (triangleWave(phase1) * 0.4
                    + triangleWave(phase2) * 0.3
                    + triangleWave(phase3) * 0.2
                    + triangleWave(phase4) * 0.1) * cWindStrength * heightFactor;

        worldPos.x += cWindDirection.x * wind;
        worldPos.z += cWindDirection.y * wind;
    }

    // Physics interaction — bend away from shapes
    if (heightFactor > 0.0)
    {
        vec4 shapes[4];
        shapes[0] = cPhysShape0;
        shapes[1] = cPhysShape1;
        shapes[2] = cPhysShape2;
        shapes[3] = cPhysShape3;

        for (int i = 0; i < 4; ++i)
        {
            if (shapes[i].w <= 0.0) continue;
            vec2 delta = worldPos.xz - shapes[i].xz;
            float d = length(delta);
            if (d < shapes[i].w && d > 0.001)
            {
                float bend = (1.0 - d / shapes[i].w) * 1.5 * heightFactor;
                worldPos.xz += normalize(delta) * bend;
            }
        }
    }

    // Per-vertex lighting: hemisphere ambient + directional sun + subsurface scatter
    vec3 bladeNormal = vec3(0.0, 1.0, 0.0);
    vec3 ambient = GetHemisphereAmbient(bladeNormal);

    vec3 lightDir = normalize(cLightDir);
    float NdotL = max(dot(bladeNormal, lightDir), 0.0);

    // Subsurface scatter: light through thin blade (backlit glow)
    float scatter = max(dot(-bladeNormal, lightDir), 0.0) * 0.3;

    vec3 lighting = ambient + vec3(0.6) * (NdotL + scatter);
    // Darken base, lighten tips
    lighting *= (0.6 + 0.4 * heightFactor);

    gl_Position = GetClipPos(worldPos);
    vTexCoord = GetTexCoord(iTexCoord);
    vWorldPos = vec4(worldPos, GetDepth(gl_Position));
    vFade = fade;
    vLighting = lighting;
}

#endif

#ifdef COMPILEPS

void PS()
{
    ApplyClipPlane();

    vec4 diffColor = texture2D(sDiffMap, vTexCoord);

    // Alpha test
    if (diffColor.a * vFade < 0.3)
        discard;

    // Apply seasonal tint and lighting
    diffColor.rgb *= cSeasonalTint.rgb * vLighting;

    // Fog
    #ifdef HEIGHTFOG
        float fogFactor = GetHeightFogFactor(vWorldPos.w, vWorldPos.y);
    #else
        float fogFactor = GetFogFactor(vWorldPos.w);
    #endif

    gl_FragColor = vec4(GetFog(diffColor.rgb, fogFactor), diffColor.a * vFade);
}

#endif
