#include "Uniforms.glsl"
#include "Samplers.glsl"
#include "Transform.glsl"

varying vec2 vTexCoord;
varying vec3 vWorldDir;

#ifdef COMPILEPS
uniform float cCloudAngle;
uniform float cNightFactor;

vec3 RotateAroundAxis(vec3 dir, vec3 axis, float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return dir * c + cross(axis, dir) * s + axis * dot(axis, dir) * (1.0 - c);
}

vec3 RotateClouds(vec3 dir)
{
    const vec3 axis = normalize(vec3(0.12, 1.0, 0.07));
    return RotateAroundAxis(dir, axis, cCloudAngle);
}
#endif

void VS()
{
    mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = GetWorldPos(modelMatrix);
    gl_Position = GetClipPos(worldPos);
    vTexCoord = iTexCoord.xy;
    // Direction from camera to this vertex for cubemap lookup
    vWorldDir = normalize(worldPos - cCameraPos);
}

void PS()
{
    vec4 ufoTex = texture2D(sDiffMap, vTexCoord);
    if (ufoTex.a < 0.1)
        discard;

    // Sample the skybox at this fragment's direction
    vec3 skyColor = textureCube(sEnvCubeMap, vWorldDir).rgb;

    // How "blue" is the sky behind us?
    // Blue sky: high blue, low red/green. Clouds/other: more uniform RGB.
    float blueness = skyColor.b - max(skyColor.r, skyColor.g);
    // Blue sky = visible, cloud/other = hidden behind cloud
    float visibility = smoothstep(-0.05, 0.15, blueness);

    // Fade out at night
    visibility *= 1.0 - cNightFactor;

    if (visibility < 0.01)
        discard;

    gl_FragColor = vec4(ufoTex.rgb, ufoTex.a * visibility);
}
