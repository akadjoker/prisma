#include "pbr_surface.glsl"

const int kMaxLights = 16;

struct LightData
{
    vec4 positionFalloff;
    vec4 colorIntensity;
    vec4 direction;
    vec4 spot;
};

layout(set = 0, binding = 3, std140) uniform Lights
{
    vec4 uSunDirection;
    vec4 uSunColorIntensity;
    vec4 uLightCounts;
    LightData uLights[kMaxLights];
};

struct Light
{
    vec3 radiance;
    vec3 l;
    float attenuation;
};

float pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float distributionGgx(float roughness, float noH)
{
    float oneMinusNoHSquared = 1.0 - noH * noH;
    float a = noH * roughness;
    float k = min(roughness / (oneMinusNoHSquared + a * a), 453.5);
    return k * (k * (1.0 / kPi));
}

float visibilitySmithGgxCorrelated(float roughness, float noV, float noL)
{
    float a2 = roughness * roughness;
    float lambdaV = noL * sqrt((noV - a2 * noV) * noV + a2);
    float lambdaL = noV * sqrt((noL - a2 * noL) * noL + a2);
    return 0.5 / max(lambdaV + lambdaL, 0.0000077);
}

vec3 fresnelSchlick(vec3 f0, float f90, float voH)
{
    return f0 + (f90 - f0) * pow5(1.0 - voH);
}

float squareFalloffAttenuation(float distanceSquare, float falloff)
{
    float factor = distanceSquare * falloff;
    float smoothFactor = clamp(1.0 - factor * factor, 0.0, 1.0);
    return smoothFactor * smoothFactor;
}

float distanceAttenuation(vec3 posToLight, float falloff)
{
    float distanceSquare = dot(posToLight, posToLight);
    return squareFalloffAttenuation(distanceSquare, falloff) / max(distanceSquare, 1e-4);
}

float angleAttenuation(vec3 lightDir, vec3 l, vec2 scaleOffset)
{
    float cd = dot(lightDir, l);
    float attenuation = clamp(cd * scaleOffset.x + scaleOffset.y, 0.0, 1.0);
    return attenuation * attenuation;
}

Light directionalLight()
{
    Light light;
    light.radiance = uSunColorIntensity.rgb * uSunColorIntensity.w;
    light.l = uSunDirection.xyz;
    light.attenuation = 1.0;
    return light;
}

Light punctualLight(int index, vec3 worldPosition)
{
    LightData data = uLights[index];
    vec3 posToLight = data.positionFalloff.xyz - worldPosition;
    Light light;
    light.radiance = data.colorIntensity.rgb * data.colorIntensity.w;
    light.l = normalize(posToLight);
    light.attenuation = distanceAttenuation(posToLight, data.positionFalloff.w);
    if (data.spot.z > 0.5)
        light.attenuation *= angleAttenuation(-data.direction.xyz, light.l, data.spot.xy);
    return light;
}

vec3 surfaceShading(PbrSurface surface, Light light, vec3 n, vec3 v)
{
    vec3 h = normalize(v + light.l);
    float noL = clamp(dot(n, light.l), 0.0, 1.0);
    float noH = clamp(dot(n, h), 0.0, 1.0);
    float loH = clamp(dot(light.l, h), 0.0, 1.0);

    float d = distributionGgx(surface.roughness, noH);
    float vis = visibilitySmithGgxCorrelated(surface.roughness, surface.noV, noL);
    float f90 = clamp(dot(surface.f0, vec3(50.0 * 0.33)), 0.0, 1.0);
    vec3 f = fresnelSchlick(surface.f0, f90, loH);
    vec3 fr = (d * vis) * f * surface.energyCompensation;
    vec3 fd = surface.diffuseColor * (1.0 / kPi);
    return (fd + fr) * light.radiance * (light.attenuation * noL);
}

vec3 evaluateLights(PbrSurface surface, vec3 n, vec3 v, vec3 worldPosition)
{
    vec3 color = vec3(0.0);
    if (uSunColorIntensity.w > 0.0)
        color += surfaceShading(surface, directionalLight(), n, v);
    int count = int(uLightCounts.x);
    for (int i = 0; i < count; ++i)
        color += surfaceShading(surface, punctualLight(i, worldPosition), n, v);
    return color;
}
