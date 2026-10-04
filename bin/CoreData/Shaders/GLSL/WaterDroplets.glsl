#include "Uniforms.glsl"
#include "Samplers.glsl"
#include "Transform.glsl"
#include "ScreenPos.glsl"

varying vec2 vScreenPos;

#ifdef COMPILEPS
uniform float cBreachTime;
uniform float cDryDuration;
uniform float cDropletIntensity;
#endif

void VS()
{
    mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = GetWorldPos(modelMatrix);
    gl_Position = GetClipPos(worldPos);
    vScreenPos = GetScreenPosPreDiv(gl_Position);
}

#ifdef COMPILEPS

float hash1(float n) { return fract(sin(n) * 43758.5453123); }
vec2 hash2(float n) { return vec2(hash1(n), hash1(n + 127.1)); }

void PS()
{
    #ifdef WATERDROPS
    float elapsed = cElapsedTimePS - cBreachTime;
    float dryProgress = clamp(elapsed / cDryDuration, 0.0, 1.0);

    vec4 sceneColor = texture2D(sDiffMap, vScreenPos);

    if (dryProgress >= 1.0)
    {
        gl_FragColor = sceneColor;
        return;
    }

    float baseSeed = floor(cBreachTime * 100.0);
    float dryFactor = 1.0 - dryProgress;

    vec2 totalOffset = vec2(0.0);
    float totalCoverage = 0.0;
    float totalTint = 0.0;

    const int NUM_DROPLETS = 20;

    for (int i = 0; i < NUM_DROPLETS; ++i)
    {
        float seed = baseSeed + float(i) * 17.31;
        vec2 startPos = hash2(seed);
        float sizeRand = hash1(seed + 53.7);
        float baseRad = mix(0.002, 0.01, sizeRand * sizeRand);

        float delay = hash1(seed + 91.3) * 0.8;
        float dropElapsed = max(elapsed - delay, 0.0);
        if (dropElapsed <= 0.0) continue;

        // Size-dependent gravity — bigger slides faster
        float dropGravity = mix(0.01, 0.08, sizeRand * sizeRand);

        vec2 pos = startPos;
        pos.x += sin(dropElapsed * 1.5 + seed) * 0.002;
        pos.y += dropGravity * dropElapsed;

        if (pos.y > 1.2) continue;

        // Dissolve near bottom of screen
        float waterFade = 1.0 - smoothstep(0.85, 1.0, pos.y);
        if (waterFade < 0.01) continue;

        // Shrink over time
        float sizeSurvival = smoothstep(0.0, 0.7, sizeRand) + 0.3;
        float rad = baseRad * dryFactor * sizeSurvival;
        if (rad < 0.0005) continue;

        // Teardrop stretch
        float stretch = 1.0 + clamp(dropGravity * 12.0, 0.0, 2.0);
        vec2 delta = vScreenPos - pos;
        if (delta.y < 0.0)
            delta.y /= stretch;
        else
            delta.y *= (1.0 + stretch * 0.3);

        float dist = length(delta);
        if (dist >= rad) continue;

        float edge = dist / rad;
        float dropShape = 1.0 - edge * edge;

        vec2 refractionOffset = normalize(delta + vec2(0.0001)) * dropShape * cDropletIntensity * rad;

        float weight = dropShape * dryFactor * waterFade;
        totalOffset += refractionOffset * weight;
        totalCoverage += weight;

        float highlight = smoothstep(0.7, 0.95, edge) * (1.0 - smoothstep(0.95, 1.0, edge));
        if (delta.y < 0.0)
            highlight *= 1.5;
        totalTint += highlight * weight * 0.5;
    }

    totalCoverage = clamp(totalCoverage, 0.0, 1.0);

    vec2 refractedUV = clamp(vScreenPos + totalOffset, vec2(0.001), vec2(0.999));
    vec4 refractedColor = texture2D(sDiffMap, refractedUV);

    vec3 result = mix(sceneColor.rgb, refractedColor.rgb, totalCoverage);
    result += vec3(0.6, 0.7, 0.8) * totalTint * dryFactor;

    gl_FragColor = vec4(result, 1.0);
    #endif
}

#endif
