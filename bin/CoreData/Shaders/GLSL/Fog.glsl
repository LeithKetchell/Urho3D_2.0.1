#ifdef COMPILEPS
vec3 GetFog(vec3 color, float fogFactor)
{
    return mix(cFogColor, color, fogFactor);
}

vec3 GetLitFog(vec3 color, float fogFactor)
{
    return color * fogFactor;
}

float GetFogFactor(float depth)
{
    return clamp((cFogParams.x - depth) * cFogParams.y, 0.0, 1.0);
}

float GetHeightFogFactor(float depth, float height)
{
    float fogFactor = GetFogFactor(depth);
    float rawHeight = (height - cFogParams.z) * cFogParams.w;
    if (rawHeight >= 1.0)
        return fogFactor;  // above fog ceiling — distance fog only
    if (rawHeight <= 0.0)
    {
        // Inverse fog underwater: thickest at surface, fades with depth over 1/3 of above-water range
        float underwaterFogFactor = clamp((cFogParams.z - height) * 3.0 * cFogParams.w, 0.0, 1.0);
        return min(underwaterFogFactor, fogFactor);
    }
    float heightFogFactor = clamp(rawHeight, 0.0, 1.0);  // linear: 0 at base (opaque), 1 at ceiling (clear)
    return min(heightFogFactor, fogFactor);
}
#endif
