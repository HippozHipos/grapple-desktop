#pragma once

#include <grapple/foundation/Hash.hpp>
#include <grapple/projection/RenderPlan.hpp>

namespace grapple::projection {

foundation::Hash256 hashRenderPlan(const RenderPlan& plan);
foundation::Hash256 hashRenderClipImplementation();
foundation::Hash256 hashRenderClipParams(const RenderClip& clip);
foundation::Hash256 hashRenderAudioClipImplementation();
foundation::Hash256 hashRenderAudioClipParams(const RenderAudioClip& clip);
foundation::Hash256 hashRenderCameraImplementation();
foundation::Hash256 hashRenderCameraParams(const RenderCamera& camera);
foundation::Hash256 hashRenderEffectImplementation(const RenderEffectNode& effectNode);
foundation::Hash256 hashRenderEffectParams(const RenderEffectNode& effectNode);

} // namespace grapple::projection
