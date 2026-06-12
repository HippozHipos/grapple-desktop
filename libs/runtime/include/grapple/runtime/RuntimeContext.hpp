#pragma once

#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/media/MediaReader.hpp>
#include <grapple/model/ModelService.hpp>
#include <grapple/projection/RenderPlan.hpp>
#include <grapple/runtime/RuntimeCache.hpp>

namespace grapple::runtime {

struct RuntimeContext {
  foundation::ProjectId projectId;
  foundation::RevisionId revision;
  foundation::NodeId nodeId;
  projection::RenderStage stage;
  foundation::TimeRange nodeRange;
  foundation::TimeSeconds time;
  foundation::FrameNumber frame;
  foundation::FrameRate frameRate;
  media::IMediaReader& media;
  IRuntimeCache& cache;
  model::IModelService& models;
};

} // namespace grapple::runtime

