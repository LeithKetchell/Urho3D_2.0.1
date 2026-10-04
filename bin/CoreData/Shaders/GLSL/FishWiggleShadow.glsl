#include "Uniforms.glsl"
#include "Transform.glsl"

uniform float cWiggleAmplitude;
uniform float cWiggleFrequency;
uniform float cWiggleBodyStart;

#ifdef VSM_SHADOW
    varying vec4 vTexCoord;
#else
    varying vec2 vTexCoord;
#endif

void VS()
{
    mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = GetWorldPos(modelMatrix);

    // Fish tail wiggle (must match FishWiggle.glsl)
    float t = (iPos.z + 0.159) / 0.422;
    float ramp = smoothstep(cWiggleBodyStart, 1.0, t);
    float localFreq = cWiggleFrequency * (1.0 + 2.0 * t);
    float phase = cElapsedTime * localFreq + t * 6.283;
    worldPos.x += ramp * cWiggleAmplitude * (1.0 + 0.5 * t) * sin(phase);

    gl_Position = GetClipPos(worldPos);
    #ifdef VSM_SHADOW
        vTexCoord = vec4(GetTexCoord(iTexCoord), gl_Position.z, gl_Position.w);
    #else
        vTexCoord = GetTexCoord(iTexCoord);
    #endif
}
