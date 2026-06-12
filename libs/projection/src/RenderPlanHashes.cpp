#include <grapple/projection/RenderPlanHashes.hpp>

#include <grapple/timeline/TimelineSerializer.hpp>

#include <sstream>

namespace grapple::projection {

foundation::Hash256 hashRenderClipImplementation() {
  return foundation::stableHash("runtime.clip.media");
}

foundation::Hash256 hashRenderClipParams(const RenderClip& clip) {
  std::ostringstream stream;
  stream << "clip|" << clip.trackNodeId.value() << '|'
         << timeline::serializeCanonicalClipPayload(clip.payload);
  return foundation::stableHash(stream.str());
}

foundation::Hash256 hashRenderCameraImplementation() {
  return foundation::stableHash("runtime.camera");
}

foundation::Hash256 hashRenderCameraParams(const RenderCamera& camera) {
  std::ostringstream stream;
  stream << "camera|" << camera.sourceNodeId.value() << '|'
         << timeline::serializeCanonicalCameraPayload(timeline::CameraPayload{
              camera.name,
              camera.transform,
              camera.lens
            });
  return foundation::stableHash(stream.str());
}

foundation::Hash256 hashRenderEffectImplementation(const RenderEffectNode& effectNode) {
  return foundation::stableHash(timeline::serializeCanonicalEffectImplementation(effectNode.payload.implementation));
}

foundation::Hash256 hashRenderEffectParams(const RenderEffectNode& effectNode) {
  std::ostringstream stream;
  stream << timeline::serializeCanonicalEffectParams(effectNode.payload);
  return foundation::stableHash(stream.str());
}

} // namespace grapple::projection
