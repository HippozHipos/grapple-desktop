#pragma once

#include <grapple/agent/AgentTool.hpp>
#include <grapple/foundation/Result.hpp>

namespace grapple::agent {

class AgentToolRegistry;

foundation::Result<void> registerProjectTools(AgentToolRegistry& registry);

AgentTool makeProjectInspectTool();
AgentTool makeAssetListTool();
AgentTool makeAssetImportTool();
AgentTool makeCompositionInspectTool();
AgentTool makeCameraCreateTool();
AgentTool makeCameraUpdateTool();
AgentTool makeTimelineCreateTrackTool();
AgentTool makeTimelineCreateClipTool();
AgentTool makeTimelineMoveClipTool();
AgentTool makeTimelineTrimClipTool();
AgentTool makeTimelineUpdateClipTransformTool();
AgentTool makeEffectCreateNodeTool();
AgentTool makeEffectDeleteNodeTool();
AgentTool makeEffectUpdateParamValueTool();
AgentTool makeEffectConnectPortsTool();
AgentTool makeEffectDisconnectPortsTool();
AgentTool makeRenderPlanInspectTool();
AgentTool makeRuntimeInspectDiagnosticsTool();
AgentTool makeNoteCreateTool();
AgentTool makeNoteUpdateTool();

} // namespace grapple::agent
