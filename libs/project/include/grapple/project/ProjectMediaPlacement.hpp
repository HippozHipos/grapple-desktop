#pragma once

#include <grapple/asset/Asset.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/project/ProjectCommand.hpp>
#include <grapple/project/ProjectIdAllocator.hpp>
#include <grapple/project/ProjectQuery.hpp>

#include <optional>
#include <vector>

namespace grapple::project {

struct MediaPlacementDraft {
  AddMediaToTimelineCommand command;
  foundation::NodeId compositionNodeId;
  foundation::NodeId trackNodeId;
  foundation::NodeId clipNodeId;
  std::optional<foundation::NodeId> createdCameraNodeId;
};

foundation::Result<MediaPlacementDraft> buildMediaPlacementDraft(
  IProjectIdAllocator& ids,
  const asset::Asset& selectedAsset,
  std::optional<foundation::TimeSeconds> timelineStart,
  std::optional<foundation::TimeSeconds> duration,
  const std::vector<CompositionSummary>& compositions
);

} // namespace grapple::project
