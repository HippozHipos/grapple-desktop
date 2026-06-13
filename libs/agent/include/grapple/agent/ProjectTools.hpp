#pragma once

#include <grapple/agent/AgentTool.hpp>

namespace grapple::agent {

AgentTool makeProjectInspectTool();
AgentTool makeAssetListTool();
AgentTool makeTimelineCreateTrackTool();
AgentTool makeTimelineCreateClipTool();
AgentTool makeTimelineMoveClipTool();
AgentTool makeEffectCreateNodeTool();
AgentTool makeNoteCreateTool();
AgentTool makeNoteUpdateTool();

} // namespace grapple::agent
