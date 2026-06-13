#include <grapple/projection/RenderPlanBuilder.hpp>

#include <utility>

namespace grapple::projection {

foundation::Result<BuildRenderPlanResult> RenderPlanBuilder::buildRenderPlan(
  const BuildRenderPlanRequest& request
) const {
  RenderPlan plan{
    request.timeline.projectId,
    request.timeline.revision,
    RenderStage{request.timeline.stage.name},
    request.timeline.duration,
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    request.timeline.diagnostics
  };

  for (const TimelineAsset& asset : request.timeline.assets) {
    plan.assets.push_back(RenderAsset{asset.assetId, asset.versionHash});
  }

  for (const TimelineLayer& layer : request.timeline.layers) {
    plan.layers.push_back(RenderLayer{layer.sourceNodeId, layer.name});
  }

  for (const TimelineAudioTrack& track : request.timeline.audioTracks) {
    plan.audioTracks.push_back(RenderAudioTrack{track.sourceNodeId, track.name});
  }

  for (const TimelineClip& clip : request.timeline.clips) {
    plan.clips.push_back(RenderClip{clip.sourceNodeId, clip.trackNodeId, clip.payload});
  }

  for (const TimelineAudioClip& clip : request.timeline.audioClips) {
    plan.audioClips.push_back(RenderAudioClip{clip.sourceNodeId, clip.trackNodeId, clip.payload});
  }

  for (const TimelineCamera& camera : request.timeline.cameras) {
    plan.cameras.push_back(RenderCamera{camera.sourceNodeId, camera.name, camera.transform, camera.lens});
  }

  for (const TimelineEffectGraph& effectGraph : request.timeline.effectGraphs) {
    RenderEffectGraph renderEffectGraph{effectGraph.id, effectGraph.targetNodeId, {}, {}};

    for (const TimelineEffectNode& node : effectGraph.nodes) {
      renderEffectGraph.nodes.push_back(RenderEffectNode{node.sourceNodeId, node.payload});
    }

    for (const TimelineEffectEdge& edge : effectGraph.edges) {
      renderEffectGraph.edges.push_back(RenderEffectEdge{
        edge.sourceEdgeId,
        edge.sourceNodeId,
        edge.sourcePort,
        edge.targetNodeId,
        edge.targetPort,
        edge.order
      });
    }

    plan.effectGraphs.push_back(std::move(renderEffectGraph));
  }

  return BuildRenderPlanResult{plan, plan.diagnostics};
}

} // namespace grapple::projection
