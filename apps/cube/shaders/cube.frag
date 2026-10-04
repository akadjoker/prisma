#version 450

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec2 vUv;

layout(location = 0) out vec4 oColor;

void main()
{
    vec2 cell = floor(vUv * 8.0);
    float light = mod(cell.x + cell.y, 2.0) < 0.5 ? 1.0 : 110.0 / 255.0;
    oColor = vec4(vColor * light, 1.0);
}
