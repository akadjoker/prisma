#pragma once

#include <math.h>
#include <string.h>

namespace zenapp
{

enum
{
    kMaxLights = 16
};

struct LightData
{
    float positionFalloff[4];
    float colorIntensity[4];
    float direction[4];
    float spot[4];
};

struct LightUniforms
{
    float sunDirection[4];
    float sunColorIntensity[4];
    float counts[4];
    LightData lights[kMaxLights];
};

inline void clearLights(LightUniforms* lights)
{
    memset(lights, 0, sizeof(*lights));
}

inline void setSunLight(LightUniforms* lights, const float* toLight, const float* color,
        float intensity)
{
    const float length = sqrtf(toLight[0] * toLight[0] + toLight[1] * toLight[1] +
                               toLight[2] * toLight[2]);
    for (int i = 0; i < 3; ++i)
    {
        lights->sunDirection[i] = toLight[i] / length;
        lights->sunColorIntensity[i] = color[i];
    }
    lights->sunDirection[3] = 0.0f;
    lights->sunColorIntensity[3] = intensity;
}

inline int addPointLight(LightUniforms* lights, const float* position, const float* color,
        float intensity, float radius)
{
    const int index = static_cast<int>(lights->counts[0]);
    if (index >= kMaxLights) return -1;
    LightData& light = lights->lights[index];
    for (int i = 0; i < 3; ++i)
    {
        light.positionFalloff[i] = position[i];
        light.colorIntensity[i] = color[i];
    }
    light.positionFalloff[3] = radius > 0.0f ? 1.0f / (radius * radius) : 0.0f;
    light.colorIntensity[3] = intensity;
    light.spot[2] = 0.0f;
    lights->counts[0] = static_cast<float>(index + 1);
    return index;
}

inline int addSpotLight(LightUniforms* lights, const float* position, const float* direction,
        const float* color, float intensity, float radius, float innerAngle, float outerAngle)
{
    const int index = addPointLight(lights, position, color, intensity, radius);
    if (index < 0) return index;
    LightData& light = lights->lights[index];
    const float length = sqrtf(direction[0] * direction[0] + direction[1] * direction[1] +
                               direction[2] * direction[2]);
    for (int i = 0; i < 3; ++i) light.direction[i] = direction[i] / length;
    const float halfPi = 1.57079632679f;
    const float minimum = 0.0087266463f;
    const float outerMagnitude = fabsf(outerAngle);
    const float innerMagnitude = fabsf(innerAngle);
    const float outer = outerMagnitude < minimum ? minimum
                                                 : (outerMagnitude > halfPi ? halfPi : outerMagnitude);
    float inner = innerMagnitude < minimum ? minimum
                                           : (innerMagnitude > halfPi ? halfPi : innerMagnitude);
    inner = inner > outer ? outer : inner;
    const float cosOuter = cosf(outer);
    const float cosInner = cosf(inner);
    const float difference = cosInner - cosOuter;
    const float scale = 1.0f / (difference > 1.0f / 1024.0f ? difference : 1.0f / 1024.0f);
    light.spot[0] = scale;
    light.spot[1] = -cosOuter * scale;
    light.spot[2] = 1.0f;
    return index;
}

} // namespace zenapp
