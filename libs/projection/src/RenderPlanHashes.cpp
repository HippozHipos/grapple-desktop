#include <grapple/projection/RenderPlanHashes.hpp>

#include <grapple/projection/RenderPlanSerializer.hpp>
#include <grapple/timeline/TimelineSerializer.hpp>

#include <iomanip>
#include <sstream>

namespace grapple::projection {

foundation::Hash256 hashRenderPlan(const RenderPlan& plan) {
  return foundation::stableHash(serializeCanonicalRenderPlanContent(plan));
}

foundation::Hash256 hashRenderClipImplementation() {
  return foundation::stableHash("runtime.clip.media");
}

foundation::Hash256 hashRenderClipParams(const RenderClip& clip) {
  std::ostringstream stream;
  stream << "clip|" << clip.trackNodeId.value() << '|'
         << timeline::serializeCanonicalClipPayload(clip.payload);
  return foundation::stableHash(stream.str());
}

foundation::Hash256 hashRenderTextClipImplementation() {
  return foundation::stableHash("runtime.clip.text");
}

foundation::Hash256 hashRenderTextClipParams(const RenderTextClip& clip) {
  std::ostringstream stream;
  stream << "text_clip|" << clip.trackNodeId.value() << '|'
         << timeline::serializeCanonicalTextClipPayload(clip.payload);
  return foundation::stableHash(stream.str());
}

foundation::Hash256 hashRenderAudioClipImplementation() {
  return foundation::stableHash("runtime.clip.audio");
}

foundation::Hash256 hashRenderAudioClipParams(const RenderAudioClip& clip) {
  std::ostringstream stream;
  stream << "audio_clip|" << clip.trackNodeId.value() << '|'
         << timeline::serializeCanonicalClipPayload(clip.payload);
  return foundation::stableHash(stream.str());
}

foundation::Hash256 hashRenderCameraImplementation() {
  return foundation::stableHash("runtime.camera");
}

foundation::Hash256 hashRenderCameraParams(const RenderCamera& camera) {
  std::ostringstream stream;
  stream << "camera|" << camera.sourceNodeId.value() << '|'
         << timeline::serializeCanonicalTransform(camera.state.transform)
         << "|focalLength=" << std::setprecision(17) << camera.state.lens.focalLength;
  return foundation::stableHash(stream.str());
}

foundation::Hash256 hashRenderEffectImplementation(const RenderEffectNode& effectNode) {
  return foundation::stableHash(timeline::serializeCanonicalEffectImplementation(effectNode.payload.implementation));
}

foundation::Hash256 hashRenderEffectParams(const RenderEffectNode& effectNode) {
  std::ostringstream stream;
  stream << timeline::serializeCanonicalRuntimeEffectParams(effectNode.payload);
  return foundation::stableHash(stream.str());
}

} // namespace grapple::projection
