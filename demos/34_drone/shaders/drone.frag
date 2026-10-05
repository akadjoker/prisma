#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorld;
layout(location = 2) in vec3 vClip;
layout(location = 3) in vec4 vTangent;
layout(location = 4) in vec2 vUv;

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
    vec4 uSurface;
    vec4 uSpecular;
    vec4 uEmissive;
    vec4 uFlags;
    vec4 uModes;
};

layout(set = 0, binding = 8, std140) uniform SunShadow
{
    mat4 uSunShadowMatrix;
    vec4 uSunShadowParams;
    vec4 uSunShadowAxisX;
    vec4 uSunShadowAxisY;
};

layout(set = 0, binding = 9, std140) uniform AoParams
{
    vec4 uAoParams;
};

layout(set = 1, binding = 7) uniform sampler2DShadow uSunShadowMap;
layout(set = 1, binding = 8) uniform sampler2D uAmbientOcclusion;
layout(set = 1, binding = 2) uniform sampler2D uBaseTexture;
layout(set = 1, binding = 3) uniform sampler2D uSurfaceTexture;
layout(set = 1, binding = 4) uniform sampler2D uNormalTexture;
layout(set = 1, binding = 5) uniform sampler2D uOcclusionTexture;
layout(set = 1, binding = 6) uniform sampler2D uEmissiveTexture;

#include "../../common/shaders/pbr.glsl"
#include "../../common/shaders/lighting.glsl"

layout(location = 0) out vec4 oColor;

float sunShadow(vec3 worldPosition, vec3 normal)
{
    float size = uSunShadowParams.x;
    float texelSize = uSunShadowParams.y;
    vec2 lateral = vec2(dot(normal, uSunShadowAxisX.xyz), dot(normal, uSunShadowAxisY.xyz));
    vec3 p = worldPosition + normal * (abs(lateral.x * uSunShadowParams.z) +
                                        abs(lateral.y * uSunShadowParams.w));
    vec4 clip = uSunShadowMatrix * vec4(p, 1.0);
    vec3 position = clip.xyz / clip.w;
    position.xy = position.xy * 0.5 + 0.5;
    position.z = clamp(position.z, 0.0, 1.0);
    position.xy = clamp(position.xy, vec2(-1.0), vec2(2.0));

    vec2 offset = vec2(0.5);
    vec2 uv = position.xy * size + offset;
    vec2 base = (floor(uv) - offset) * texelSize;
    vec2 st = fract(uv);
    vec2 uw = vec2(3.0 - 2.0 * st.x, 1.0 + 2.0 * st.x);
    vec2 vw = vec2(3.0 - 2.0 * st.y, 1.0 + 2.0 * st.y);
    vec2 u = vec2((2.0 - st.x) / uw.x - 1.0, st.x / uw.y + 1.0) * texelSize;
    vec2 v = vec2((2.0 - st.y) / vw.x - 1.0, st.y / vw.y + 1.0) * texelSize;

    float sum = uw.x * vw.x * texture(uSunShadowMap, vec3(base + vec2(u.x, v.x), position.z));
    sum += uw.y * vw.x * texture(uSunShadowMap, vec3(base + vec2(u.y, v.x), position.z));
    sum += uw.x * vw.y * texture(uSunShadowMap, vec3(base + vec2(u.x, v.y), position.z));
    sum += uw.y * vw.y * texture(uSunShadowMap, vec3(base + vec2(u.y, v.y), position.z));
    return sum * (1.0 / 16.0);
}

void main()
{
    vec4 base = uBaseColor;
    if (uFlags.x > 0.5)
        base *= texture(uBaseTexture, vUv);
    if (uModes.w > 0.5 && uModes.w < 1.5 && base.a < uSurface.w)
        discard;

    vec3 n = normalize(vNormal);
    if (uFlags.z > 0.5)
    {
        vec3 t = normalize(vTangent.xyz);
        vec3 b = cross(n, t) * vTangent.w;
        vec3 sampled = texture(uNormalTexture, vUv).xyz * 2.0 - 1.0;
        sampled.xy *= uSpecular.w;
        n = normalize(mat3(t, b, n) * sampled);
    }
    if (uModes.z > 0.5 && !gl_FrontFacing)
        n = -n;
    vec3 v = normalize(uCamera.xyz - vWorld);

    float metallic = uSurface.x;
    float roughness = uSurface.y;
    if (uFlags.y > 0.5)
    {
        vec3 mr = texture(uSurfaceTexture, vUv).rgb;
        roughness *= mr.g;
        metallic *= mr.b;
    }
    PbrSurface surface = makeSurface(base.rgb, metallic, roughness, n, v);

    float occlusion = uFlags.w > 0.5 ? texture(uOcclusionTexture, vUv).r : 1.0;
    Light sun = directionalLight(uSunDirection, uSunColorIntensity);
    sun.attenuation *= sunShadow(vWorld, normalize(vNormal));
    float ssao = 1.0;
    if (uAoParams.x > 0.5)
        ssao = texture(uAmbientOcclusion, vClip.xy / vClip.z * 0.5 + 0.5).r;
    float diffuseAo = min(occlusion, ssao);
    vec3 color = evaluateIblAmbientOcclusion(surface, n, v, diffuseAo) + surfaceShading(surface, sun, n, v);
    if (uAoParams.y > 0.5)
        color = vec3(0.3 * pow(diffuseAo, 6.0));
    vec3 emissive = uEmissive.rgb;
    if (uModes.x > 0.5)
        emissive *= texture(uEmissiveTexture, vUv).rgb;
    color += emissive;

    oColor = vec4(color, 1.0);
}
