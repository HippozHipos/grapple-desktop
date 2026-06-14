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
AgentTool makeTimelineDeleteTrackTool();
AgentTool makeTimelineCreateClipTool();
AgentTool makeTimelineDeleteClipTool();
AgentTool makeTimelineMoveClipTool();
AgentTool makeTimelineTrimClipTool();
AgentTool makeTimelineUpdateClipTransformTool();
AgentTool makeEffectCreateNodeTool();
AgentTool makeEffectDeleteNodeTool();
AgentTool makeEffectUpdateParamValueTool();
AgentTool makeEffectCreateParamKeyframeTool();
AgentTool makeEffectUpdateParamKeyframeTool();
AgentTool makeEffectDeleteParamKeyframeTool();
AgentTool makeEffectConnectPortsTool();
AgentTool makeEffectDisconnectPortsTool();
AgentTool makeRenderPlanInspectTool();
AgentTool makeRuntimeInspectDiagnosticsTool();
AgentTool makeNoteListTool();
AgentTool makeNoteCreateTool();
AgentTool makeNoteUpdateTool();

} // namespace grapple::agent
