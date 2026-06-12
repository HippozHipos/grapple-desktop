#pragma once

#include <grapple/foundation/Hash.hpp>
#include <grapple/projection/RenderPlan.hpp>

namespace grapple::projection {

foundation::Hash256 hashRenderClipImplementation();
foundation::Hash256 hashRenderClipParams(const RenderClip& clip);
foundation::Hash256 hashRenderEffectImplementation(const RenderEffectNode& effectNode);
foundation::Hash256 hashRenderEffectParams(const RenderEffectNode& effectNode);

} // namespace grapple::projection
