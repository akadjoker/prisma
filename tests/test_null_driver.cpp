#include "prisma/rhi/Driver.h"

#include <stdio.h>

static int failures = 0;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

int main()
{
    using namespace prisma;

    CHECK(isDriverSupported(DriverType::Null));
    CHECK(!isDriverSupported(DriverType::Vulkan));

    DriverDesc desc;
    DriverError error = DriverError::None;

    desc.type = DriverType::Vulkan;
    CHECK(createDriver(desc, &error) == nullptr);
    CHECK(error == DriverError::NotCompiled);

    if (isDriverSupported(DriverType::OpenGL))
    {
        desc.type = DriverType::OpenGL;
        CHECK(createDriver(desc, &error) == nullptr);
        CHECK(error == DriverError::MissingPlatform);
    }

    desc.type = DriverType::Null;
    Driver* driver = createDriver(desc, &error);
    CHECK(error == DriverError::None);
    CHECK(driver != nullptr);
    if (driver)
    {
        CHECK(driver->type() == DriverType::Null);
        CHECK(!driver->caps().compute);
        driver->beginFrame();
        driver->beginRenderPass(RenderPassDesc());
        driver->endRenderPass();
        driver->endFrame();
        driver->present();
        destroyDriver(driver);
    }

    if (failures)
    {
        printf("%d failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
