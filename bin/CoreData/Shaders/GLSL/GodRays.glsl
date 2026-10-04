#include "Uniforms.glsl"
#include "Samplers.glsl"
#include "Transform.glsl"
#include "ScreenPos.glsl"

varying vec2 vTexCoord;
varying vec2 vScreenPos;

#ifdef COMPILEPS
uniform vec2 cLightScreenPos;
uniform float cLightRadius;
uniform float cGodRayDensity;
uniform float cGodRayDecay;
uniform float cGodRayWeight;
uniform float cGodRayExposure;
uniform vec3 cGodRayColor;
uniform float cGodRayIntensity;
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
    // Single-pass god rays: procedural disc + radial blur + composite
    vec3 scene = texture2D(sDiffMap, vScreenPos).rgb;

    // Depth occlusion: sample depth at light center and nearby points
    // Celestial bodies write gl_FragDepth = 0.9999 — anything closer occludes
    float visibility = 0.0;
    float celestialDepth = 0.9998;
    float offset = 0.01;
    visibility += step(celestialDepth, texture2D(sDepthBuffer, cLightScreenPos).r);
    visibility += step(celestialDepth, texture2D(sDepthBuffer, cLightScreenPos + vec2(offset, 0.0)).r);
    visibility += step(celestialDepth, texture2D(sDepthBuffer, cLightScreenPos - vec2(offset, 0.0)).r);
    visibility += step(celestialDepth, texture2D(sDepthBuffer, cLightScreenPos + vec2(0.0, offset)).r);
    visibility += step(celestialDepth, texture2D(sDepthBuffer, cLightScreenPos - vec2(0.0, offset)).r);
    visibility *= 0.2;  // average of 5 samples

    // March from this pixel toward the light, testing a procedural disc
    vec2 texCoord = vScreenPos;
    vec2 delta = (texCoord - cLightScreenPos) * cGodRayDensity / 48.0;

    float illumination = 0.0;
    float decay = 1.0;
    float invRadius = 1.0 / max(cLightRadius, 0.001);

    for (int i = 0; i < 48; i++)
    {
        texCoord -= delta;
        // Procedural disc — distance to light center in screen space
        float d = length(texCoord - cLightScreenPos) * invRadius;
        float disc = 1.0 - smoothstep(0.5, 1.0, d);
        disc *= decay * cGodRayWeight;
        illumination += disc;
        decay *= cGodRayDecay;
    }

    float ray = clamp(illumination * cGodRayExposure, 0.0, 1.0);
    vec3 godray = cGodRayColor * ray * cGodRayIntensity * visibility;
    gl_FragColor = vec4(scene + godray, 1.0);
}
