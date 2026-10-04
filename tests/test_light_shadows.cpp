#include "Check.h"
#include "LightShadows.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace
{

unsigned state = 12345u;

float random01()
{
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) / 16777216.0f;
}

float randomRange(float low, float high) { return low + (high - low) * random01(); }

bool inside(const Math::Mat4& viewProjection, const Math::Vec3& point, float slack)
{
    const Math::Vec4 clip = viewProjection * Math::Vec4(point, 1.0f);
    if (clip.w <= 0.0f) return false;
    const float limit = clip.w * (1.0f + slack);
    return fabsf(clip.x) <= limit && fabsf(clip.y) <= limit && clip.z >= -clip.w * slack &&
           clip.z <= limit;
}

float depth(const Math::Mat4& viewProjection, const Math::Vec3& point)
{
    const Math::Vec4 clip = viewProjection * Math::Vec4(point, 1.0f);
    return clip.z / clip.w;
}

} // namespace

int main()
{
    const float white[3] = { 1.0f, 1.0f, 1.0f };
    const Math::Vec3 centre(1.0f, 2.0f, 3.0f);
    const float radius = 5.0f;

    static zenapp::LightSet single;
    zenapp::clearLights(&single);
    const float centrePosition[3] = { centre.x, centre.y, centre.z };
    zenapp::addPointLight(&single, centrePosition, white, 100.0f, radius);

    zenapp::LightShadows shadows;
    zenapp::selectLightShadows(single, Math::Vec3(0.0f, 0.0f, 0.0f), 64, &shadows);
    CHECK(shadows.maps.size() == 6);
    CHECK(shadows.firstMap.size() == 1 && shadows.firstMap[0] == 0);
    for (unsigned face = 0; face < shadows.maps.size(); ++face)
    {
        CHECK(shadows.maps[face].light == 0);
        CHECK(shadows.maps[face].face == face);
    }

    unsigned outsideOwnFace = 0;
    unsigned insideOtherFace = 0;
    const unsigned samples = 20000;
    for (unsigned s = 0; s < samples && shadows.maps.size() == 6; ++s)
    {
        Math::Vec3 direction(randomRange(-1.0f, 1.0f), randomRange(-1.0f, 1.0f),
                randomRange(-1.0f, 1.0f));
        const float length = direction.Length();
        if (length < 0.05f) continue;
        direction = direction * (1.0f / length);
        const Math::Vec3 point = centre + direction * randomRange(0.05f, radius * 0.99f);
        const unsigned face = zenapp::pointShadowFace(point - centre);
        if (!inside(shadows.maps[face].viewProjection, point, 0.001f)) ++outsideOwnFace;

        const float x = fabsf(direction.x);
        const float y = fabsf(direction.y);
        const float z = fabsf(direction.z);
        const float largest = x > y ? (x > z ? x : z) : (y > z ? y : z);
        const float second = largest == x ? (y > z ? y : z) : largest == y ? (x > z ? x : z)
                                                                           : (x > y ? x : y);
        if (second > largest * 0.98f) continue;
        for (unsigned other = 0; other < 6; ++other)
            if (other != face && inside(shadows.maps[other].viewProjection, point, 0.0f))
                ++insideOtherFace;
    }
    CHECK(outsideOwnFace == 0);
    CHECK(insideOtherFace == 0);

    if (shadows.maps.size() == 6)
    {
        const Math::Mat4& positiveX = shadows.maps[0].viewProjection;
        const float nearDepth = depth(positiveX, centre + Math::Vec3(zenapp::kShadowNear, 0, 0));
        const float middleDepth = depth(positiveX, centre + Math::Vec3(1.0f, 0.0f, 0.0f));
        const float farDepth = depth(positiveX, centre + Math::Vec3(radius, 0.0f, 0.0f));
        CHECK(fabsf(nearDepth) < 1e-3f);
        CHECK(fabsf(farDepth - 1.0f) < 1e-3f);
        CHECK(nearDepth < middleDepth && middleDepth < farDepth);

        const Math::Vec4 axis = positiveX * Math::Vec4(centre + Math::Vec3(2.0f, 0.0f, 0.0f), 1.0f);
        CHECK(fabsf(axis.x) < 1e-4f && fabsf(axis.y) < 1e-4f);
        const Math::Vec4 above =
                positiveX * Math::Vec4(centre + Math::Vec3(2.0f, 1.0f, 0.0f), 1.0f);
        CHECK(above.y > 0.0f && fabsf(above.x) < 1e-4f);
    }

    static zenapp::LightSet many;
    zenapp::clearLights(&many);
    const float p1[3] = { 3.0f, 0.0f, 0.0f };
    const float p2[3] = { 1.0f, 0.0f, 0.0f };
    const float p3[3] = { 2.0f, 0.0f, 0.0f };
    const float ps[3] = { 0.0f, 2.5f, 0.0f };
    const float down[3] = { 0.0f, -1.0f, 0.0f };
    const int far = zenapp::addPointLight(&many, p1, white, 10.0f, 4.0f);
    const int nearest = zenapp::addPointLight(&many, p2, white, 10.0f, 4.0f);
    const int middle = zenapp::addPointLight(&many, p3, white, 10.0f, 4.0f);
    const int spot = zenapp::addSpotLight(&many, ps, down, white, 10.0f, 6.0f, 0.3f, 0.6f);
    const Math::Vec3 viewer(0.0f, 0.0f, 0.0f);

    zenapp::selectLightShadows(many, viewer, 13, &shadows);
    CHECK(shadows.maps.size() == 13);
    CHECK(shadows.firstMap[nearest] == 0);
    CHECK(shadows.firstMap[middle] == 6);
    CHECK(shadows.firstMap[spot] == 12);
    CHECK(shadows.firstMap[far] == -1);

    zenapp::selectLightShadows(many, viewer, 7, &shadows);
    CHECK(shadows.maps.size() == 7);
    CHECK(shadows.firstMap[nearest] == 0);
    CHECK(shadows.firstMap[middle] == -1);
    CHECK(shadows.firstMap[spot] == 6);

    zenapp::selectLightShadows(many, viewer, 5, &shadows);
    CHECK(shadows.maps.size() == 1);
    CHECK(shadows.firstMap[spot] == 0);

    zenapp::selectLightShadows(many, viewer, 0, &shadows);
    CHECK(shadows.maps.size() == 0);
    CHECK(shadows.firstMap[nearest] == -1 && shadows.firstMap[spot] == -1);

    zenapp::selectLightShadows(many, viewer, 64, &shadows);
    CHECK(shadows.maps.size() == 19);
    if (shadows.firstMap[spot] >= 0)
    {
        const zenapp::ShadowMapView& map = shadows.maps[shadows.firstMap[spot]];
        const Math::Vec3 origin(ps[0], ps[1], ps[2]);
        const Math::Vec4 axis = map.viewProjection * Math::Vec4(origin + Math::Vec3(0, -3, 0), 1);
        CHECK(fabsf(axis.x) < 1e-4f && fabsf(axis.y) < 1e-4f && axis.w > 0.0f);
        unsigned missed = 0;
        for (unsigned s = 0; s < 2000; ++s)
        {
            const float angle = randomRange(0.0f, 0.6f * 0.999f);
            const float turn = randomRange(0.0f, 6.2831853f);
            const float reach = randomRange(0.05f, 5.9f);
            const Math::Vec3 direction(sinf(angle) * cosf(turn), -cosf(angle),
                    sinf(angle) * sinf(turn));
            if (!inside(map.viewProjection, origin + direction * reach, 0.001f)) ++missed;
        }
        CHECK(missed == 0);
        CHECK(!inside(map.viewProjection, origin + Math::Vec3(0.0f, 1.0f, 0.0f), 0.0f));
    }

    if (failures) printf("%d failed\n", failures);
    else
        printf("ok\n");
    return failures ? 1 : 0;
}
