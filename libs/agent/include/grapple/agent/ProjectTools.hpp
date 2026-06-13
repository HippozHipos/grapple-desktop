#pragma once

#include <grapple/agent/AgentTool.hpp>

namespace grapple::agent {

AgentTool makeProjectInspectTool();
AgentTool makeAssetListTool();
AgentTool makeCompositionInspectTool();
AgentTool makeTimelineCreateTrackTool();
AgentTool makeTimelineCreateClipTool();
AgentTool makeTimelineMoveClipTool();
AgentTool makeTimelineTrimClipTool();
AgentTool makeEffectCreateNodeTool();
AgentTool makeEffectUpdateParamsTool();
AgentTool makeEffectConnectPortsTool();
AgentTool makeEffectDisconnectPortsTool();
AgentTool makeNoteCreateTool();
AgentTool makeNoteUpdateTool();

} // namespace grapple::agent
