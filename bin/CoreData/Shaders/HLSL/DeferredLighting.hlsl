// Phase 36: Deferred Lighting Pass Shader
// Reads G-Buffer data via input attachments and performs lighting calculations

#include "Uniforms.hlsl"

// Input attachments (from G-Buffer)
// In Vulkan, these are declared as subpassInput for input attachments
uniform subpassInput inputPosition;   // G-Buffer[0]: World position
uniform subpassInput inputNormal;     // G-Buffer[1]: World normal
uniform subpassInput inputAlbedo;     // G-Buffer[2]: Diffuse color
uniform subpassInput inputSpecular;   // G-Buffer[3]: Specular properties

// Lighting parameters
uniform float3 cLightDir;
uniform float3 cLightColor;
uniform float3 cAmbient;

void VS(float4 iPos : POSITION,
    float2 iTexCoord : TEXCOORD0,
    out float2 oTexCoord : TEXCOORD0,
    out float4 oPos : OUTPOSITION)
{
    // Full-screen quad: just pass through NDC coordinates
    oPos = iPos;
    oTexCoord = iTexCoord;
}

void PS(float2 iTexCoord : TEXCOORD0,
    out float4 oColor : OUTCOLOR0)
{
    // Phase 36: Read G-Buffer data via input attachments (pixel-local storage)
    vec4 positionData = subpassLoad(inputPosition);
    vec4 normalData = subpassLoad(inputNormal);
    vec4 albedoData = subpassLoad(inputAlbedo);
    vec4 specularData = subpassLoad(inputSpecular);

    vec3 worldPos = positionData.xyz;
    vec3 worldNormal = normalize(normalData.xyz);
    vec3 albedo = albedoData.xyz;
    vec3 specularColor = specularData.xyz;
    float roughness = specularData.w;

    // Basic Phong lighting
    vec3 lightDir = normalize(cLightDir);
    vec3 viewDir = normalize(-worldPos);  // Simplified: camera at origin
    vec3 halfDir = normalize(lightDir + viewDir);

    // Diffuse term
    float ndotl = max(0.0, dot(worldNormal, lightDir));
    vec3 diffuse = albedo * cLightColor * ndotl;

    // Specular term (Phong)
    float shine = 1.0 / max(roughness, 0.01);
    float ndoth = max(0.0, dot(worldNormal, halfDir));
    vec3 specular = specularColor * pow(ndoth, shine);

    // Combine lighting
    vec3 finalColor = diffuse + specular + albedo * cAmbient;

    oColor = float4(finalColor, 1.0);
}
