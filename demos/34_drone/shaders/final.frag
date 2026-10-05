#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 vUv;

layout(set = 0, binding = 0, std140) uniform Post
{
    vec4 uParams;
};

layout(set = 1, binding = 0) uniform sampler2D uScene;
layout(set = 1, binding = 1) uniform sampler2D uBloom;

#include "../../common/shaders/tonemap.glsl"

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 color = texture(uScene, vUv).rgb + texture(uBloom, vUv).rgb * uParams.x;
    oColor = vec4(linearToSrgb(tonemapAcesLegacy(color)), 1.0);
}
