#pragma once

#include "mathc.h"

namespace zenapp
{

inline Math::Mat4 perspectiveZeroToOne(float fovY, float aspect, float nearPlane, float farPlane)
{
    Math::Mat4 depthZeroToOne = Math::Mat4::Identity();
    depthZeroToOne.col2.z = 0.5f;
    depthZeroToOne.col3.z = 0.5f;
    return depthZeroToOne * Math::Mat4::Perspective(fovY, aspect, nearPlane, farPlane);
}

} // namespace zenapp
