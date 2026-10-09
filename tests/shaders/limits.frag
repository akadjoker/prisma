#version 450

layout(set = 1, binding = 0) uniform sampler2D uTexture0;
layout(set = 1, binding = 1) uniform sampler2D uTexture1;
layout(set = 1, binding = 2) uniform sampler2D uTexture2;
layout(set = 1, binding = 3) uniform sampler2D uTexture3;
layout(set = 1, binding = 4) uniform sampler2D uTexture4;
layout(set = 1, binding = 5) uniform sampler2D uTexture5;
layout(set = 1, binding = 6) uniform sampler2D uTexture6;
layout(set = 1, binding = 7) uniform sampler2D uTexture7;
layout(set = 1, binding = 8) uniform sampler2D uTexture8;
layout(set = 1, binding = 9) uniform sampler2D uTexture9;
layout(set = 1, binding = 10) uniform sampler2D uTexture10;
layout(set = 1, binding = 11) uniform sampler2D uTexture11;
layout(set = 1, binding = 12) uniform sampler2D uTexture12;
layout(set = 1, binding = 13) uniform sampler2D uTexture13;
layout(set = 1, binding = 14) uniform sampler2D uTexture14;
layout(set = 1, binding = 15) uniform sampler2D uTexture15;
layout(set = 1, binding = 16) uniform sampler2D uTexture16;
layout(set = 1, binding = 17) uniform sampler2D uTexture17;
layout(set = 1, binding = 18) uniform sampler2D uTexture18;
layout(set = 1, binding = 19) uniform sampler2D uTexture19;
layout(set = 1, binding = 20) uniform sampler2D uTexture20;
layout(set = 1, binding = 21) uniform sampler2D uTexture21;
layout(set = 1, binding = 22) uniform sampler2D uTexture22;
layout(set = 1, binding = 23) uniform sampler2D uTexture23;

layout(set = 2, binding = 0, std430) readonly buffer Buffer0
{
    uint uValue0;
};

layout(set = 2, binding = 1, std430) readonly buffer Buffer1
{
    uint uValue1;
};

layout(set = 2, binding = 2, std430) readonly buffer Buffer2
{
    uint uValue2;
};

layout(set = 2, binding = 3, std430) readonly buffer Buffer3
{
    uint uValue3;
};

layout(set = 2, binding = 4, std430) readonly buffer Buffer4
{
    uint uValue4;
};

layout(set = 2, binding = 5, std430) readonly buffer Buffer5
{
    uint uValue5;
};

layout(set = 2, binding = 6, std430) readonly buffer Buffer6
{
    uint uValue6;
};

layout(set = 2, binding = 7, std430) readonly buffer Buffer7
{
    uint uValue7;
};

layout(set = 2, binding = 8, std430) readonly buffer Buffer8
{
    uint uValue8;
};

layout(set = 2, binding = 9, std430) readonly buffer Buffer9
{
    uint uValue9;
};

layout(set = 2, binding = 10, std430) readonly buffer Buffer10
{
    uint uValue10;
};

layout(location = 0) out vec4 oColor;

void main()
{
    float textureSum = 0.0;
    textureSum += texture(uTexture0, vec2(0.5)).r * 255.0 * 1.0;
    textureSum += texture(uTexture1, vec2(0.5)).r * 255.0 * 2.0;
    textureSum += texture(uTexture2, vec2(0.5)).r * 255.0 * 3.0;
    textureSum += texture(uTexture3, vec2(0.5)).r * 255.0 * 4.0;
    textureSum += texture(uTexture4, vec2(0.5)).r * 255.0 * 5.0;
    textureSum += texture(uTexture5, vec2(0.5)).r * 255.0 * 6.0;
    textureSum += texture(uTexture6, vec2(0.5)).r * 255.0 * 7.0;
    textureSum += texture(uTexture7, vec2(0.5)).r * 255.0 * 8.0;
    textureSum += texture(uTexture8, vec2(0.5)).r * 255.0 * 9.0;
    textureSum += texture(uTexture9, vec2(0.5)).r * 255.0 * 10.0;
    textureSum += texture(uTexture10, vec2(0.5)).r * 255.0 * 11.0;
    textureSum += texture(uTexture11, vec2(0.5)).r * 255.0 * 12.0;
    textureSum += texture(uTexture12, vec2(0.5)).r * 255.0 * 13.0;
    textureSum += texture(uTexture13, vec2(0.5)).r * 255.0 * 14.0;
    textureSum += texture(uTexture14, vec2(0.5)).r * 255.0 * 15.0;
    textureSum += texture(uTexture15, vec2(0.5)).r * 255.0 * 16.0;
    textureSum += texture(uTexture16, vec2(0.5)).r * 255.0 * 17.0;
    textureSum += texture(uTexture17, vec2(0.5)).r * 255.0 * 18.0;
    textureSum += texture(uTexture18, vec2(0.5)).r * 255.0 * 19.0;
    textureSum += texture(uTexture19, vec2(0.5)).r * 255.0 * 20.0;
    textureSum += texture(uTexture20, vec2(0.5)).r * 255.0 * 21.0;
    textureSum += texture(uTexture21, vec2(0.5)).r * 255.0 * 22.0;
    textureSum += texture(uTexture22, vec2(0.5)).r * 255.0 * 23.0;
    textureSum += texture(uTexture23, vec2(0.5)).r * 255.0 * 24.0;
    float bufferSum = 0.0;
    bufferSum += float(uValue0) * 1.0;
    bufferSum += float(uValue1) * 2.0;
    bufferSum += float(uValue2) * 3.0;
    bufferSum += float(uValue3) * 4.0;
    bufferSum += float(uValue4) * 5.0;
    bufferSum += float(uValue5) * 6.0;
    bufferSum += float(uValue6) * 7.0;
    bufferSum += float(uValue7) * 8.0;
    bufferSum += float(uValue8) * 9.0;
    bufferSum += float(uValue9) * 10.0;
    bufferSum += float(uValue10) * 11.0;
    oColor = vec4(textureSum / 4900.0, bufferSum / 506.0, 0.0, 1.0);
}
