#pragma once

#include <grapple/projection/RenderPlan.hpp>

#include <string>

namespace grapple::projection {

std::string serializeCanonicalRenderPlan(const RenderPlan& plan);

} // namespace grapple::projection
