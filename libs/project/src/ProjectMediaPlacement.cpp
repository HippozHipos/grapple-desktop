#include <grapple/project/ProjectMediaPlacement.hpp>

#include "internal/ProjectInvariants.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace grapple::project {

namespace {

foundation::Result<foundation::TimeSeconds> durationForPlacement(
  const asset::Asset& selectedAsset,
  const std::optional<foundation::TimeSeconds>& requestedDuration
) {
  if (requestedDuration.has_value()) {
    return requestedDuration.value();
  }
  if (selectedAsset.metadata.duration.has_value()) {
    return selectedAsset.metadata.duration.value();
  }
  return foundation::Error{
    "project.asset_duration_missing",
    "Placing this asset requires an explicit duration because the asset has no duration metadata."
  };
}

foundation::TimeSeconds appendStartForComposition(const CompositionSummary& composition) {
  double end = 0.0;
  for (const CompositionTrackSummary& track : composition.tracks) {
    for (const CompositionClipSummary& clip : track.clips) {
      end = std::max(end, clip.timelineRange.end.value);
    }
  }
  return foundation::TimeSeconds{end};
}

const CompositionTrackSummary* firstTrackOfKind(
  const CompositionSummary& composition,
  timeline::TrackKind kind
) {
  for (const CompositionTrackSummary& track : composition.tracks) {
    if (track.kind == kind) {
      return &track;
    }
  }
  return nullptr;
}

} // namespace

foundation::Result<MediaPlacementDraft> buildMediaPlacementDraft(
  IProjectIdAllocator& ids,
  const asset::Asset& selectedAsset,
  std::optional<foundation::TimeSeconds> timelineStart,
  std::optional<foundation::TimeSeconds> requestedDuration,
  const std::vector<CompositionSummary>& compositions
) {
  const timeline::ClipKind clipKind = invariant::clipKindForAssetMediaType(selectedAsset.metadata.mediaType);
  const timeline::TrackKind trackKind = invariant::trackKindForClipKind(clipKind);
  auto duration = durationForPlacement(selectedAsset, requestedDuration);
  if (!duration) {
    return duration.error();
  }

  std::optional<CreateCompositionCommand> compositionCommand;
  const CompositionSummary* targetComposition = compositions.empty() ? nullptr : &compositions.front();
  foundation::NodeId compositionNodeId = targetComposition == nullptr
    ? ids.nextNodeId("composition")
    : targetComposition->nodeId;
  if (targetComposition == nullptr) {
    compositionCommand = CreateCompositionCommand{compositionNodeId, "Main"};
  }

  std::optional<CreateTrackCommand> trackCommand;
  const CompositionTrackSummary* targetTrack = targetComposition == nullptr
    ? nullptr
    : firstTrackOfKind(*targetComposition, trackKind);
  foundation::NodeId trackNodeId = targetTrack == nullptr
    ? ids.nextNodeId("track")
    : targetTrack->nodeId;
  if (targetTrack == nullptr) {
    const std::string trackName = trackKind == timeline::TrackKind::Audio ? "Audio" : "Video";
    std::int64_t trackOrder = 0;
    if (targetComposition != nullptr) {
      for (const CompositionTrackSummary& track : targetComposition->tracks) {
        if (track.kind == trackKind) {
          ++trackOrder;
        }
      }
    }
    trackCommand = CreateTrackCommand{
      trackNodeId,
      compositionNodeId,
      ids.nextEdgeId("contains_track"),
      trackName,
      trackKind,
      trackOrder
    };
  }

  std::optional<CreateCameraCommand> cameraCommand;
  if (trackKind == timeline::TrackKind::Visual && (targetComposition == nullptr || targetComposition->cameras.empty())) {
    cameraCommand = CreateCameraCommand{
      ids.nextNodeId("camera"),
      compositionNodeId,
      ids.nextEdgeId("contains_camera"),
      timeline::CameraPayload{
        "Camera",
        timeline::CameraState{
          timeline::Transform2D{},
          timeline::CameraLens{35.0}
        }
      },
      targetComposition == nullptr ? 0 : static_cast<std::int64_t>(targetComposition->cameras.size())
    };
  }
  const std::optional<foundation::NodeId> createdCameraNodeId = cameraCommand.has_value()
    ? std::optional<foundation::NodeId>{cameraCommand->nodeId}
    : std::nullopt;

  const foundation::TimeSeconds clipStart = timelineStart.has_value()
    ? timelineStart.value()
    : (targetComposition == nullptr ? foundation::TimeSeconds{0.0} : appendStartForComposition(*targetComposition));
  const foundation::NodeId clipNodeId = ids.nextNodeId("clip");
  const std::int64_t clipOrder = targetTrack == nullptr
    ? 0
    : static_cast<std::int64_t>(targetTrack->clips.size());

  return MediaPlacementDraft{
    AddMediaToTimelineCommand{
      std::move(compositionCommand),
      std::move(trackCommand),
      std::move(cameraCommand),
      CreateClipCommand{
        clipNodeId,
        trackNodeId,
        ids.nextEdgeId("contains_clip"),
        timeline::ClipPayload{
          clipKind,
          foundation::TimeRange{
            clipStart,
            foundation::TimeSeconds{clipStart.value + duration.value().value}
          },
          foundation::TimeRange{
            foundation::TimeSeconds{0.0},
            duration.value()
          },
          1.0,
          selectedAsset.id,
          timeline::Transform2D{}
        },
        clipOrder
      }
    },
    compositionNodeId,
    trackNodeId,
    clipNodeId,
    createdCameraNodeId
  };
}

} // namespace grapple::project
