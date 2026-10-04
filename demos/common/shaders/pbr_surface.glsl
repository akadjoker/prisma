#ifndef PBR_SURFACE_GLSL
#define PBR_SURFACE_GLSL

const float kMinPerceptualRoughness = 0.045;
const float kMinNoV = 1e-4;

struct PbrSurface
{
    vec3 diffuseColor;
    vec3 f0;
    float perceptualRoughness;
    float roughness;
    float noV;
    vec3 dfg;
    vec3 energyCompensation;
};

const float kPi = 3.14159265358979;

#endif
