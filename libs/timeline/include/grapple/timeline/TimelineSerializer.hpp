#pragma once

#include <grapple/foundation/Geometry.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/timeline/EffectPayload.hpp>
#include <grapple/timeline/Payloads.hpp>
#include <grapple/timeline/Transform2D.hpp>

#include <string>

namespace grapple::timeline {

std::string serializeCanonicalTimeRange(const foundation::TimeRange& range);
std::string serializedTrackKind(TrackKind kind);
std::string serializeCanonicalTrackPayload(const TrackPayload& payload);
std::string serializeCanonicalTransform(const Transform2D& transform);
std::string serializeCanonicalParamValue(const ParamValue& value);
std::string serializeCanonicalParamKeyframe(const Param::Keyframe& keyframe);
std::string serializeCanonicalParamSet(const ParamSet& params);
std::string serializeCanonicalRuntimeParamSet(const ParamSet& params);
std::string serializeCanonicalCameraPayload(const CameraPayload& payload);
std::string serializeCanonicalClipPayload(const ClipPayload& payload);
std::string serializeCanonicalEffectImplementation(const EffectImplementation& implementation);
std::string serializeCanonicalEffectParams(const EffectPayload& payload);
std::string serializeCanonicalRuntimeEffectParams(const EffectPayload& payload);
std::string serializeCanonicalRuntimeEffectPayload(const EffectPayload& payload);
std::string serializeCanonicalEffectPayload(const EffectPayload& payload);

} // namespace grapple::timeline
