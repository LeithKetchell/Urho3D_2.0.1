#include "Uniforms.glsl"
#include "Samplers.glsl"
#include "Transform.glsl"
#include "ScreenPos.glsl"

#ifdef MASK
// --- Selection mask pass: render selected object as solid white ---
#ifdef COMPILEVS
void VS()
{
    mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = GetWorldPos(modelMatrix);
    gl_Position = GetClipPos(worldPos);
}
#endif

#ifdef COMPILEPS
void PS()
{
    // Encode selection type: 1.0 = selected, 0.5 = hovered
    gl_FragColor = vec4(cMatDiffColor.r, cMatDiffColor.r, cMatDiffColor.r, 1.0);
}
#endif

#else
// --- Edge detection post-process quad ---
varying vec2 vTexCoord;
varying vec2 vScreenPos;

#ifdef COMPILEVS
void VS()
{
    mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = GetWorldPos(modelMatrix);
    gl_Position = GetClipPos(worldPos);
    vTexCoord = GetQuadTexCoord(gl_Position);
    vScreenPos = GetScreenPosPreDiv(gl_Position);
}
#endif

#ifdef COMPILEPS
void PS()
{
    // Sobel edge detection on the selection mask (sDiffMap)
    vec2 ts = cGBufferInvSize; // texel size = 1/width, 1/height

    // 3x3 Sobel kernel samples
    float tl = texture2D(sDiffMap, vScreenPos + vec2(-ts.x, -ts.y)).r;
    float tc = texture2D(sDiffMap, vScreenPos + vec2( 0.0,  -ts.y)).r;
    float tr = texture2D(sDiffMap, vScreenPos + vec2( ts.x, -ts.y)).r;
    float ml = texture2D(sDiffMap, vScreenPos + vec2(-ts.x,  0.0)).r;
    float mr = texture2D(sDiffMap, vScreenPos + vec2( ts.x,  0.0)).r;
    float bl = texture2D(sDiffMap, vScreenPos + vec2(-ts.x,  ts.y)).r;
    float bc = texture2D(sDiffMap, vScreenPos + vec2( 0.0,   ts.y)).r;
    float br = texture2D(sDiffMap, vScreenPos + vec2( ts.x,  ts.y)).r;

    // Sobel operators
    float gx = -tl - 2.0*ml - bl + tr + 2.0*mr + br;
    float gy = -tl - 2.0*tc - tr + bl + 2.0*bc + br;
    float edge = sqrt(gx * gx + gy * gy);

    // Thicken the edge by sampling at 2x distance as well
    float tl2 = texture2D(sDiffMap, vScreenPos + vec2(-2.0*ts.x, -2.0*ts.y)).r;
    float tc2 = texture2D(sDiffMap, vScreenPos + vec2( 0.0,      -2.0*ts.y)).r;
    float tr2 = texture2D(sDiffMap, vScreenPos + vec2( 2.0*ts.x, -2.0*ts.y)).r;
    float ml2 = texture2D(sDiffMap, vScreenPos + vec2(-2.0*ts.x,  0.0)).r;
    float mr2 = texture2D(sDiffMap, vScreenPos + vec2( 2.0*ts.x,  0.0)).r;
    float bl2 = texture2D(sDiffMap, vScreenPos + vec2(-2.0*ts.x,  2.0*ts.y)).r;
    float bc2 = texture2D(sDiffMap, vScreenPos + vec2( 0.0,       2.0*ts.y)).r;
    float br2 = texture2D(sDiffMap, vScreenPos + vec2( 2.0*ts.x,  2.0*ts.y)).r;

    float gx2 = -tl2 - 2.0*ml2 - bl2 + tr2 + 2.0*mr2 + br2;
    float gy2 = -tl2 - 2.0*tc2 - tr2 + bl2 + 2.0*bc2 + br2;
    float edge2 = sqrt(gx2 * gx2 + gy2 * gy2);

    edge = max(edge, edge2);

    // Determine outline color from mask value: >0.75 = selected (bright yellow), else hovered (deep orange)
    float center = texture2D(sDiffMap, vScreenPos).r;
    float alpha = clamp(edge * 3.0, 0.0, 1.0);
    vec3 outlineColor = center > 0.75 ? vec3(1.0, 1.0, 0.0) : vec3(1.0, 0.35, 0.0);
    gl_FragColor = vec4(outlineColor, alpha);
}
#endif
#endif
