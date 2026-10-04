#include "Check.h"
#include "Froxelizer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{

uint32_t seed = 987654321u;

float random01()
{
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(seed >> 8) / 16777216.0f;
}

float randomRange(float low, float high)
{
    return low + (high - low) * random01();
}

void perspective(float fovY, float aspect, float nearPlane, float farPlane, float* m)
{
    memset(m, 0, 16 * sizeof(float));
    const float t = tanf(fovY * 0.5f);
    m[0] = 1.0f / (aspect * t);
    m[5] = 1.0f / t;
    m[10] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    m[11] = -1.0f;
    m[14] = -2.0f * farPlane * nearPlane / (farPlane - nearPlane);
}

void normalize(float* v)
{
    const float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (int i = 0; i < 3; ++i) v[i] /= length;
}

void lookAt(const float* eye, const float* target, float* m)
{
    float f[3] = { target[0] - eye[0], target[1] - eye[1], target[2] - eye[2] };
    normalize(f);
    float s[3] = { f[1] * 0.0f - f[2] * 1.0f, f[2] * 0.0f - f[0] * 0.0f, f[0] * 1.0f - f[1] * 0.0f };
    normalize(s);
    const float u[3] = { s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2],
        s[0] * f[1] - s[1] * f[0] };
    memset(m, 0, 16 * sizeof(float));
    m[0] = s[0];
    m[4] = s[1];
    m[8] = s[2];
    m[1] = u[0];
    m[5] = u[1];
    m[9] = u[2];
    m[2] = -f[0];
    m[6] = -f[1];
    m[10] = -f[2];
    m[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    m[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    m[14] = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2];
    m[15] = 1.0f;
}

void worldFromClip(const float* view, const float* projection, float ndcX, float ndcY,
        float depth, float* world)
{
    const float v[3] = { ndcX * depth / projection[0], ndcY * depth / projection[5], -depth };
    const float t[3] = { view[12], view[13], view[14] };
    for (int j = 0; j < 3; ++j)
    {
        world[j] = 0.0f;
        for (int i = 0; i < 3; ++i) world[j] += view[j * 4 + i] * (v[i] - t[i]);
    }
}

struct Scene
{
    float view[16];
    float projection[16];
    unsigned width;
    unsigned height;
    float nearPlane;
    float farPlane;
    float zLightNear;
    float zLightFar;
};

Scene makeScene(unsigned width, unsigned height)
{
    Scene scene;
    scene.width = width;
    scene.height = height;
    scene.nearPlane = 0.1f;
    scene.farPlane = 100.0f;
    scene.zLightNear = 2.0f;
    scene.zLightFar = 80.0f;
    perspective(0.9f, static_cast<float>(width) / static_cast<float>(height), scene.nearPlane,
            scene.farPlane, scene.projection);
    const float eye[3] = { 3.0f, 2.0f, 9.0f };
    const float target[3] = { -1.0f, 0.5f, -20.0f };
    lookAt(eye, target, scene.view);
    return scene;
}

unsigned checkSoundness(zenapp::Froxelizer& froxelizer, const Scene& scene,
        const zenapp::FroxelLight* lights, unsigned count, unsigned samples, double* meanList)
{
    unsigned misses = 0;
    double total = 0.0;
    for (unsigned s = 0; s < samples; ++s)
    {
        const float ndcX = randomRange(-1.0f, 1.0f);
        const float ndcY = randomRange(-1.0f, 1.0f);
        const float depth = randomRange(0.2f, scene.zLightFar * 0.999f);
        float p[3];
        worldFromClip(scene.view, scene.projection, ndcX, ndcY, depth, p);

        const unsigned f = froxelizer.froxelIndexFor(ndcX, ndcY, depth);
        const uint32_t entry = froxelizer.entries()[f];
        const unsigned listed = entry & 0xFFu;
        const unsigned offset = entry >> 16;
        total += listed;
        for (unsigned i = 0; i < count; ++i)
        {
            const zenapp::FroxelLight& light = lights[i];
            float d[3] = { p[0] - light.position[0], p[1] - light.position[1],
                p[2] - light.position[2] };
            const float distance = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (distance >= light.radius * 0.999f) continue;
            if (light.spot)
            {
                const float cosine = (d[0] * light.direction[0] + d[1] * light.direction[1] +
                                             d[2] * light.direction[2]) / distance;
                if (cosine <= cosf(light.outerAngle) * 1.001f) continue;
            }
            bool found = false;
            for (unsigned k = 0; k < listed && !found; ++k)
                found = froxelizer.records()[offset + k] == i;
            if (!found) ++misses;
        }
    }
    *meanList = total / samples;
    return misses;
}

void fillLights(zenapp::FroxelLight* lights, unsigned count, float radiusLow, float radiusHigh,
        const Scene& scene)
{
    for (unsigned i = 0; i < count; ++i)
    {
        float p[3];
        worldFromClip(scene.view, scene.projection, randomRange(-1.1f, 1.1f),
                randomRange(-1.1f, 1.1f), randomRange(1.0f, 60.0f), p);
        zenapp::FroxelLight& light = lights[i];
        memcpy(light.position, p, sizeof(p));
        light.radius = randomRange(radiusLow, radiusHigh);
        light.spot = random01() < 0.4f;
        float d[3] = { randomRange(-1.0f, 1.0f), randomRange(-1.0f, 0.2f), randomRange(-1.0f, 1.0f) };
        normalize(d);
        memcpy(light.direction, d, sizeof(d));
        light.outerAngle = randomRange(0.2f, 0.9f);
    }
}

} // namespace

int main()
{
    const unsigned sizes[4][2] = { { 1920, 1080 }, { 960, 540 }, { 1280, 800 }, { 640, 640 } };
    for (const unsigned* size: sizes)
    {
        const Scene scene = makeScene(size[0], size[1]);
        zenapp::Froxelizer froxelizer;
        froxelizer.setDepthRange(scene.zLightNear, scene.zLightFar);
        froxelizer.prepare(scene.width, scene.height, scene.projection, scene.nearPlane,
                scene.farPlane);
        const zenapp::FroxelParams& params = froxelizer.params();
        CHECK(froxelizer.froxelCount() <= zenapp::Froxelizer::kEntryCount);
        CHECK(params.countZ == zenapp::Froxelizer::kSliceCount);
        CHECK(params.cellsXY[0] * 8 >= 1.0f);
        CHECK(static_cast<float>(params.countX) >= params.cellsXY[0]);
        CHECK(static_cast<float>(params.countY) >= params.cellsXY[1]);
        CHECK(params.countX * params.countY * params.countZ == froxelizer.froxelCount());
        const unsigned last = froxelizer.froxelIndexFor(0.999f, 0.999f, scene.zLightFar);
        CHECK(last < froxelizer.froxelCount());
        const unsigned first = froxelizer.froxelIndexFor(-1.0f, -1.0f, 0.01f);
        CHECK(first == 0);
        printf("%ux%u: %u x %u x %u froxels, %.1f pixels per cell\n", scene.width, scene.height,
                params.countX, params.countY, params.countZ,
                scene.width / params.cellsXY[0]);
    }

    {
        const Scene scene = makeScene(1280, 720);
        zenapp::Froxelizer froxelizer;
        froxelizer.setDepthRange(scene.zLightNear, scene.zLightFar);
        froxelizer.prepare(scene.width, scene.height, scene.projection, scene.nearPlane,
                scene.farPlane);

        froxelizer.froxelize(scene.view, nullptr, 0);
        bool empty = true;
        for (unsigned i = 0; i < zenapp::Froxelizer::kEntryCount; ++i)
            empty = empty && froxelizer.entries()[i] == 0;
        CHECK(empty);

        bool sawOverflow = false;
        for (unsigned round = 0; round < 6; ++round)
        {
            zenapp::FroxelLight lights[200];
            const unsigned count = 40 + round * 32;
            fillLights(lights, count, 2.0f, 9.0f, scene);
            froxelizer.froxelize(scene.view, lights, count);
            double mean = 0.0;
            const unsigned misses = checkSoundness(froxelizer, scene, lights, count, 30000, &mean);
            printf("%u lights: %u missed, mean %.2f lights per froxel%s\n", count, misses, mean,
                    froxelizer.recordsOverflowed() ? " (records overflowed)" : "");
            CHECK(misses == 0);
            if (!froxelizer.recordsOverflowed()) CHECK(mean < 0.3 * count);
            else
                sawOverflow = true;
        }

        CHECK(sawOverflow);
        zenapp::FroxelLight many[255];
        fillLights(many, 255, 25.0f, 40.0f, scene);
        froxelizer.froxelize(scene.view, many, 255);
        double mean = 0.0;
        const unsigned misses = checkSoundness(froxelizer, scene, many, 255, 20000, &mean);
        printf("255 large lights (record overflow): %u missed, mean %.2f lights per froxel\n",
                misses, mean);
        CHECK(misses == 0);
        CHECK(froxelizer.recordsOverflowed());

        zenapp::FroxelLight one[1];
        fillLights(one, 1, 12.0f, 12.0f, scene);
        one[0].spot = false;
        froxelizer.froxelize(scene.view, one, 1);
        unsigned shared = 0;
        unsigned lit = 0;
        for (unsigned f = 1; f < froxelizer.froxelCount(); ++f)
        {
            if (froxelizer.entries()[f] == 0) continue;
            ++lit;
            if (froxelizer.entries()[f] == froxelizer.entries()[f - 1]) ++shared;
        }
        printf("one light: %u lit froxels, %u share a record with their neighbour\n", lit, shared);
        CHECK(lit > 0);
        CHECK(shared > 0);
        CHECK(!froxelizer.recordsOverflowed());
    }

    printf(failures ? "test_froxel: %d failures\n" : "test_froxel: all passed\n", failures);
    return failures ? 1 : 0;
}
