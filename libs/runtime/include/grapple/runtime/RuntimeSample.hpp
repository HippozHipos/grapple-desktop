#pragma once

#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/projection/RenderPlan.hpp>
#include <grapple/runtime/RuntimeDiagnostic.hpp>
#include <grapple/runtime/RuntimeOutput.hpp>
#include <grapple/runtime/RuntimeQuality.hpp>

#include <vector>

namespace grapple::runtime {

struct PreparedRuntimePlan;

using ResolvedLayer = projection::RenderLayer;
using ResolvedClip = projection::RenderClip;
using ResolvedCamera = projection::RenderCamera;

struct RuntimeSample {
  foundation::TimeSeconds time;
  std::vector<ResolvedLayer> layers;
  std::vector<ResolvedClip> clips;
  std::vector<ResolvedCamera> cameras;
  std::vector<RuntimeEffectOutput> effectOutputs;
  std::vector<RuntimeDiagnostic> diagnostics;
};

struct RuntimeSampleRequest {
  const PreparedRuntimePlan& prepared;
  foundation::TimeSeconds time;
  RuntimeQuality quality = RuntimeQuality::Interactive;
};

struct RuntimeSampleResult {
  RuntimeSample sample;
  std::vector<RuntimeDiagnostic> diagnostics;
};

struct RuntimeFrameResult {
  foundation::FrameNumber frame;
  RuntimeSample sample;
};

struct RuntimeRangeRequest {
  const PreparedRuntimePlan& prepared;
  foundation::TimeRange range;
  foundation::FrameRate frameRate;
  RuntimeQuality quality = RuntimeQuality::Interactive;
};

struct RuntimeRangeResult {
  foundation::TimeRange range;
  std::vector<RuntimeFrameResult> frames;
  std::vector<RuntimeDiagnostic> diagnostics;
};

} // namespace grapple::runtime
