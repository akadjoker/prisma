#pragma once

#include "prisma/rhi/Caps.h"
#include "prisma/rhi/Types.h"

namespace prisma
{

class Driver
{
public:
    virtual ~Driver() = default;

    virtual DriverType type() const = 0;
    virtual const Caps& caps() const = 0;

    virtual void beginFrame() = 0;
    virtual void beginRenderPass(const RenderPassDesc& desc) = 0;
    virtual void endRenderPass() = 0;
    virtual void endFrame() = 0;
    virtual void present() = 0;
};

bool isDriverSupported(DriverType type);
Driver* createDriver(const DriverDesc& desc, DriverError* error = nullptr);
void destroyDriver(Driver* driver);
const char* driverErrorText(DriverError error);

} // namespace prisma
