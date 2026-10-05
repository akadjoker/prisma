#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorld;
layout(location = 2) in vec4 vTangent;
layout(location = 3) in vec2 vUv;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uInverseViewProjection;
    vec4 uCamera;
    vec4 uExposure;
    vec4 uSunDirection;
    vec4 uSunColorIntensity;
};

layout(set = 0, binding = 7, std140) uniform Material
{
    vec4 uBaseColor;
    vec4 uParams;
    vec4 uFlags;
};

layout(set = 1, binding = 2) uniform sampler2D uBaseTexture;
layout(set = 1, binding = 3) uniform sampler2D uNormalTexture;
layout(set = 1, binding = 4) uniform sampler2D uOrmTexture;

#include "../../common/shaders/pbr.glsl"
#include "../../common/shaders/lighting.glsl"

layout(location = 0) out vec4 oColor;

void main()
{
    vec2 uv = vUv * uParams.z;
    vec3 baseColor = uBaseColor.rgb;
    if (uFlags.x > 0.5) baseColor *= texture(uBaseTexture, uv).rgb;
    baseColor = mix(vec3(dot(baseColor, vec3(0.2126, 0.7152, 0.0722))), baseColor, uFlags.w);

    vec3 n = normalize(vNormal);
    if (uFlags.y > 0.5)
    {
        vec3 t = normalize(vTangent.xyz);
        vec3 b = cross(n, t) * vTangent.w;
        vec3 sampled = texture(uNormalTexture, uv).xyz * 2.0 - 1.0;
        sampled.xy *= uParams.w;
        n = normalize(mat3(t, b, n) * sampled);
    }
    vec3 v = normalize(uCamera.xyz - vWorld);

    vec3 orm = uFlags.z > 0.5 ? texture(uOrmTexture, uv).rgb : vec3(1.0);
    float roughness = uParams.y * orm.g;
    float metallic = uParams.x * orm.b;
    PbrSurface surface = makeSurface(baseColor, metallic, roughness, n, v);

    vec3 color = evaluateIbl(surface, n, v) * orm.r;
    if (uSunColorIntensity.w > 0.0)
        color += surfaceShading(surface, directionalLight(uSunDirection, uSunColorIntensity), n, v);
    oColor = vec4(color, 1.0);
}
