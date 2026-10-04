#include "Uniforms.glsl"
#include "Samplers.glsl"
#include "Transform.glsl"

varying vec3 vTexCoord;

#ifdef COMPILEPS
uniform float cNightFactor;
uniform float cCloudAngle;
uniform float cStarAngle;
uniform float cSeasonBlend;       // 0..1 blend between current and next season cubemap
uniform samplerCube sSpecCubeMap; // next season cubemap (on TU_SPECULAR)

// Rodrigues rotation around an arbitrary axis
vec3 RotateAroundAxis(vec3 dir, vec3 axis, float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return dir * c + cross(axis, dir) * s + axis * dot(axis, dir) * (1.0 - c);
}

// Cloud rotation: slightly tilted off vertical
vec3 RotateClouds(vec3 dir)
{
    const vec3 axis = normalize(vec3(0.12, 1.0, 0.07));
    return RotateAroundAxis(dir, axis, cCloudAngle);
}

// Star rotation: around the South Celestial Pole axis
// Melbourne lat -37.8: SCP is at altitude 37.8 above due south
// In world space: axis = (0, sin(37.8), -cos(37.8))
vec3 RotateStars(vec3 dir)
{
    const vec3 axis = normalize(vec3(0.0, 0.6129, -0.7902)); // SCP from Melbourne
    return RotateAroundAxis(dir, axis, cStarAngle);
}
#endif

void VS()
{
#ifdef IGNORENODETRANSFORM
    mat4 modelMatrix = mat4(
        vec4(1.0, 0.0, 0.0, cViewInv[0].w),
        vec4(0.0, 1.0, 0.0, cViewInv[1].w),
        vec4(0.0, 0.0, 1.0, cViewInv[2].w),
        vec4(0.0, 0.0, 0.0, 1.0));
#else
    mat4 modelMatrix = iModelMatrix;
#endif
    vec3 worldPos = GetWorldPos(modelMatrix);
    gl_Position = GetClipPos(worldPos);
    gl_Position.z = gl_Position.w;
    vTexCoord = iPos.xyz;
}

void PS()
{
    // Rotate day sky lookup for cloud drift
    vec3 cloudDir = RotateClouds(vTexCoord);

    // Sample current and next season cubemaps, blend between them
    vec4 currentSeason = textureCube(sDiffCubeMap, cloudDir);
    vec4 nextSeason = textureCube(sSpecCubeMap, cloudDir);
    vec4 daySky = cMatDiffColor * mix(currentSeason, nextSeason, cSeasonBlend);

    // Rotate star field around celestial pole
    vec3 starDir = RotateStars(vTexCoord);
    vec4 nightSky = textureCube(sEnvCubeMap, starDir);

    // Blend between day sky and star field based on cNightFactor (0=day, 1=night)
    gl_FragColor = mix(daySky, nightSky, cNightFactor);
}
