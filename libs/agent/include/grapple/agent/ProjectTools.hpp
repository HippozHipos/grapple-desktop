#pragma once

#include <grapple/agent/AgentTool.hpp>

namespace grapple::agent {

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
AgentTool makeEffectCreateNodeTool();
AgentTool makeEffectUpdateParamValueTool();
AgentTool makeEffectConnectPortsTool();
AgentTool makeEffectDisconnectPortsTool();
AgentTool makeRenderPlanInspectTool();
AgentTool makeRuntimeInspectDiagnosticsTool();
AgentTool makeNoteCreateTool();
AgentTool makeNoteUpdateTool();

} // namespace grapple::agent
