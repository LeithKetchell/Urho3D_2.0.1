#include "Uniforms.glsl"
#include "Transform.glsl"

uniform float cWiggleAmplitude;
uniform float cWiggleFrequency;
uniform float cWiggleBodyStart;

varying vec3 vTexCoord;

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
    vTexCoord = vec3(GetTexCoord(iTexCoord), GetDepth(gl_Position));
}
