#include "pbr_surface.glsl"

layout(set = 0, binding = 1, std140) uniform Ibl
{
    vec4 uIblSh[9];
    vec4 uIblParams;
};

layout(set = 1, binding = 0) uniform samplerCube uIblSpecular;
layout(set = 1, binding = 1) uniform sampler2D uIblDfg;

PbrSurface makeSurface(vec3 baseColor, float metallic, float perceptualRoughness, vec3 n, vec3 v)
{
    PbrSurface surface;
    surface.diffuseColor = baseColor * (1.0 - metallic);
    surface.f0 = baseColor * metallic + vec3(0.04 * (1.0 - metallic));
    surface.perceptualRoughness = clamp(perceptualRoughness, kMinPerceptualRoughness, 1.0);
    surface.roughness = surface.perceptualRoughness * surface.perceptualRoughness;
    surface.noV = max(dot(n, v), kMinNoV);
    vec2 dfg = textureLod(uIblDfg, vec2(surface.noV, surface.perceptualRoughness), 0.0).xy;
    surface.dfg = vec3(dfg, 0.0);
    surface.energyCompensation = 1.0 + surface.f0 * (1.0 / dfg.y - 1.0);
    return surface;
}

vec3 irradianceSh(vec3 n)
{
    vec3 sh = uIblSh[0].xyz;
    sh += uIblSh[1].xyz * n.y + uIblSh[2].xyz * n.z + uIblSh[3].xyz * n.x;
    sh += uIblSh[4].xyz * (n.y * n.x) + uIblSh[5].xyz * (n.y * n.z) +
          uIblSh[6].xyz * (3.0 * n.z * n.z - 1.0) + uIblSh[7].xyz * (n.z * n.x) +
          uIblSh[8].xyz * (n.x * n.x - n.y * n.y);
    return max(sh, 0.0);
}

float perceptualRoughnessToLod(float perceptualRoughness)
{
    return uIblParams.x * perceptualRoughness * (2.0 - perceptualRoughness);
}

vec3 evaluateIbl(PbrSurface surface, vec3 n, vec3 v)
{
    vec3 r = reflect(-v, n);
    vec3 e = mix(surface.dfg.xxx, surface.dfg.yyy, surface.f0);

    vec3 dominant = mix(r, n, surface.roughness * surface.roughness);
    float lod = perceptualRoughnessToLod(surface.perceptualRoughness);
    vec3 specular = e * textureLod(uIblSpecular, dominant, lod).rgb * surface.energyCompensation;
    vec3 diffuse = surface.diffuseColor * irradianceSh(n) * (1.0 - e);
    return (specular + diffuse) * uIblParams.y;
}
