#pragma once

#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/Geometry.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/projection/RenderPlan.hpp>
#include <grapple/render/RenderDiagnostic.hpp>
#include <grapple/render/RenderQuality.hpp>
#include <grapple/runtime/RuntimeDiagnostic.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace grapple::render {

enum class RenderedMediaKind {
  Video,
  Image
};

struct RenderedMediaFrame {
  foundation::NodeId clipNodeId;
  foundation::NodeId trackNodeId;
  foundation::AssetId assetId;
  RenderedMediaKind kind = RenderedMediaKind::Video;
  foundation::TimeSeconds sourceTime;
  timeline::Transform2D transform;
  std::optional<foundation::Vec3> tintColor;
  double tintAmount = 0.0;
  double exposure = 0.0;
};

struct RenderedAudioClip {
  foundation::NodeId clipNodeId;
  foundation::NodeId trackNodeId;
  foundation::AssetId assetId;
  foundation::TimeRange timelineRange;
  foundation::TimeRange sourceRange;
  double playbackRate = 1.0;
};

struct RenderedTextFrame {
  foundation::NodeId clipNodeId;
  foundation::NodeId trackNodeId;
  std::string text;
  timeline::Transform2D transform;
  timeline::TextClipStyle style;
};

struct RenderedImage {
  foundation::Resolution resolution;
  std::vector<std::uint8_t> rgbaPixels;
};

struct RenderedCamera {
  foundation::NodeId cameraNodeId;
  timeline::CameraState state;
};

struct SourceFrameRequest {
  foundation::AssetId assetId;
  foundation::TimeSeconds sourceTime;
  std::optional<foundation::Resolution> targetResolution;
};

struct SourceFrame {
  foundation::AssetId assetId;
  foundation::TimeSeconds sourceTime;
  foundation::Resolution resolution;
  std::vector<std::uint8_t> rgbaPixels;
};

class IRenderFrameSource {
public:
  virtual ~IRenderFrameSource() = default;
  virtual foundation::Result<SourceFrame> frameAt(const SourceFrameRequest& request) = 0;
};

struct RenderFrame {
  foundation::RevisionId sourceRevision;
  foundation::Hash256 renderPlanHash;
  foundation::TimeSeconds time;
  std::string description;
  std::vector<RenderedMediaFrame> mediaFrames;
  std::vector<RenderedTextFrame> textFrames;
  std::vector<RenderedAudioClip> audioClips;
  std::vector<RenderedCamera> cameras;
  std::optional<RenderedImage> image;
};

struct RenderFrameMetrics {
  double runtimeSampleMs = 0.0;
  double composeMs = 0.0;
  double sourceFrameMs = 0.0;
  double totalMs = 0.0;
  std::size_t sourceFrames = 0;
};

struct RenderFrameRequest {
  foundation::TimeSeconds time;
  RenderQuality quality = RenderQuality::Draft;
  std::optional<foundation::Resolution> outputResolution;
};

struct RenderFrameResult {
  RenderFrame frame;
  std::vector<runtime::RuntimeDiagnostic> runtimeDiagnostics;
  std::vector<RenderDiagnostic> renderDiagnostics;
  RenderFrameMetrics metrics;
};

class IRenderRangeSink {
public:
  virtual ~IRenderRangeSink() = default;
  virtual foundation::Result<void> writeFrame(std::size_t frameIndex, const RenderFrameResult& frame) = 0;
};

struct RenderRangeRequest {
  foundation::TimeRange range;
  foundation::FrameRate frameRate;
  RenderQuality quality = RenderQuality::Final;
  std::optional<foundation::Resolution> outputResolution;
  IRenderRangeSink* sink = nullptr;
};

struct RenderRangeResult {
  foundation::RevisionId sourceRevision;
  foundation::Hash256 renderPlanHash;
  std::size_t framesEvaluated = 0;
  std::vector<runtime::RuntimeDiagnostic> runtimeDiagnostics;
  std::vector<RenderDiagnostic> renderDiagnostics;
};

} // namespace grapple::render
