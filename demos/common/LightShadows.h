#pragma once

#include "Lights.h"
#include "Projection.h"
#include "mathc.h"

#include <ct/sort.hpp>
#include <ct/vector.hpp>

#include <math.h>

namespace zenapp
{

struct ShadowMapView
{
    Math::Mat4 viewProjection;
    unsigned light = 0;
    unsigned face = 0;
};

struct LightShadows
{
    ct::Vector<ShadowMapView> maps;
    ct::Vector<int> firstMap;
};

const float kShadowNear = 0.01f;
const float kShadowMaxHalfAngle = 1.4f;

inline unsigned pointShadowFace(const Math::Vec3& fromLight)
{
    const float x = fabsf(fromLight.x);
    const float y = fabsf(fromLight.y);
    const float z = fabsf(fromLight.z);
    const float largest = x > y ? (x > z ? x : z) : (y > z ? y : z);
    if (largest == x) return fromLight.x >= 0.0f ? 0u : 1u;
    if (largest == y) return fromLight.y >= 0.0f ? 2u : 3u;
    return fromLight.z >= 0.0f ? 4u : 5u;
}

inline Math::Mat4 shadowView(const Math::Vec3& position, const Math::Vec3& direction)
{
    Math::Vec3 up(0.0f, 1.0f, 0.0f);
    if (fabsf(direction.Dot(up)) > 0.999f) up = Math::Vec3(up.z, up.x, up.y);
    return Math::Mat4::LookAt(position, position + direction, up);
}

inline Math::Mat4 pointShadowView(unsigned face, const Math::Vec3& position)
{
    static const float directions[6][3] = { { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } };
    return shadowView(position,
            Math::Vec3(directions[face][0], directions[face][1], directions[face][2]));
}

inline Math::Mat4 shadowProjection(float halfAngle, float radius)
{
    const float clamped = halfAngle < kShadowMaxHalfAngle ? halfAngle : kShadowMaxHalfAngle;
    return perspectiveZeroToOne(clamped * 2.0f, 1.0f, kShadowNear, radius);
}

inline void selectLightShadows(const LightSet& set, const Math::Vec3& viewer, unsigned maxMaps,
        LightShadows* out)
{
    out->maps.clear();
    out->firstMap.resize(set.count);
    ct::Vector<unsigned> order;
    ct::Vector<float> distance;
    order.reserve(set.count);
    distance.resize(set.count);
    for (unsigned i = 0; i < set.count; ++i)
    {
        const FroxelLight& light = set.culling[i];
        out->firstMap[i] = -1;
        distance[i] = (Math::Vec3(light.position[0], light.position[1], light.position[2]) - viewer)
                              .Length();
        if (light.radius > kShadowNear) order.push_back(i);
    }
    if (order.size() > 1)
        ct::sort(order.data(), order.data() + order.size(), [&distance](unsigned a, unsigned b) {
            if (distance[a] != distance[b]) return distance[a] < distance[b];
            return a < b;
        });

    for (unsigned o = 0; o < order.size() && out->maps.size() < maxMaps; ++o)
    {
        const unsigned index = order[o];
        const FroxelLight& light = set.culling[index];
        const unsigned needed = light.spot ? 1u : 6u;
        if (out->maps.size() + needed > maxMaps) continue;

        const Math::Vec3 position(light.position[0], light.position[1], light.position[2]);
        const Math::Mat4 projection =
                shadowProjection(light.spot ? light.outerAngle : 0.78539816f, light.radius);
        out->firstMap[index] = static_cast<int>(out->maps.size());
        for (unsigned face = 0; face < needed; ++face)
        {
            ShadowMapView map;
            map.light = index;
            map.face = face;
            const Math::Mat4 view =
                    light.spot ? shadowView(position, Math::Vec3(light.direction[0],
                                                              light.direction[1],
                                                              light.direction[2]))
                               : pointShadowView(face, position);
            map.viewProjection = projection * view;
            out->maps.push_back(map);
        }
    }
}

} // namespace zenapp
