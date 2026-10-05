#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 vUv;

layout(set = 1, binding = 0) uniform sampler2D uScene;

#include "../../common/shaders/tonemap.glsl"

layout(location = 0) out vec4 oColor;

void main()
{
    oColor = vec4(linearToSrgb(tonemapAcesLegacy(texture(uScene, vUv).rgb)), 1.0);
}
