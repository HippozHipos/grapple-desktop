#pragma once

#include <grapple/foundation/Time.hpp>
#include <grapple/timeline/EffectPayload.hpp>

namespace grapple::timeline {

ParamValue sampleParamValue(const Param& param, foundation::TimeSeconds time);

} // namespace grapple::timeline
