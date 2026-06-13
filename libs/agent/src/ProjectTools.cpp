#include <grapple/agent/ProjectTools.hpp>

#include <grapple/foundation/Json.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/project/ProjectCommand.hpp>
#include <grapple/project/ProjectQuery.hpp>

#include <json/json.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace grapple::agent {

namespace {

constexpr const char ProjectInspectSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "properties": {}
})json";

constexpr const char AssetListSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "properties": {}
})json";

constexpr const char TimelineCreateTrackSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": ["compositionNodeId", "name"],
  "properties": {
    "compositionNodeId": {"type": "string"},
    "name": {"type": "string"}
  }
})json";

constexpr const char TimelineCreateClipSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": ["trackNodeId", "assetId", "kind", "timelineRange", "sourceRange", "playbackRate"],
  "properties": {
    "trackNodeId": {"type": "string"},
    "assetId": {"type": "string"},
    "kind": {"enum": ["video", "audio", "image"]},
    "timelineRange": {
      "type": "object",
      "required": ["start", "end"],
      "properties": {
        "start": {"type": "number"},
        "end": {"type": "number"}
      }
    },
    "sourceRange": {
      "type": "object",
      "required": ["start", "end"],
      "properties": {
        "start": {"type": "number"},
        "end": {"type": "number"}
      }
    },
    "playbackRate": {"type": "number"}
  }
})json";

constexpr const char TimelineMoveClipSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": ["clipNodeId", "newStart"],
  "properties": {
    "clipNodeId": {"type": "string"},
    "newStart": {"type": "number"}
  }
})json";

constexpr const char TimelineTrimClipSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": ["clipNodeId", "timelineRange", "sourceRange"],
  "properties": {
    "clipNodeId": {"type": "string"},
    "timelineRange": {
      "type": "object",
      "required": ["start", "end"],
      "properties": {
        "start": {"type": "number"},
        "end": {"type": "number"}
      }
    },
    "sourceRange": {
      "type": "object",
      "required": ["start", "end"],
      "properties": {
        "start": {"type": "number"},
        "end": {"type": "number"}
      }
    }
  }
})json";

constexpr const char EffectCreateNodeSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": [
    "targetNodeId",
    "displayName",
    "implementationKind",
    "language",
    "entrypoint",
    "source",
    "sourcePort",
    "targetPort",
    "inputPorts",
    "outputPorts",
    "activeRange",
    "params"
  ],
  "properties": {
    "targetNodeId": {"type": "string"},
    "displayName": {"type": "string"},
    "implementationKind": {"enum": ["builtin", "python", "shader"]},
    "language": {"type": "string"},
    "entrypoint": {"type": "string"},
    "source": {"type": "string"},
    "sourcePort": {"type": "string"},
    "targetPort": {"type": "string"},
    "inputPorts": {"type": "array", "items": {"type": "string"}},
    "outputPorts": {"type": "array", "items": {"type": "string"}},
    "activeRange": {
      "type": "object",
      "required": ["start", "end"],
      "properties": {
        "start": {"type": "number"},
        "end": {"type": "number"}
      }
    },
    "params": {
      "type": "array",
      "minItems": 1,
      "items": {
        "type": "object",
        "required": ["name", "label", "value", "numeric"],
        "properties": {
          "name": {"type": "string"},
          "label": {"type": "string"},
          "value": {"type": "number"},
          "numeric": {
            "type": "object",
            "required": ["min", "max"],
            "properties": {
              "min": {"type": "number"},
              "max": {"type": "number"},
              "step": {"type": "number"}
            }
          }
        }
      }
    }
  }
})json";

constexpr const char NoteCreateSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": ["title", "markdown"],
  "properties": {
    "title": {"type": "string"},
    "markdown": {"type": "string"}
  }
})json";

constexpr const char NoteUpdateSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": ["nodeId", "title", "markdown"],
  "properties": {
    "nodeId": {"type": "string"},
    "title": {"type": "string"},
    "markdown": {"type": "string"}
  }
})json";

foundation::Error argumentError(const std::string& path, const std::string& message) {
  return foundation::Error{"agent.tool_arguments_invalid", path + ": " + message};
}

foundation::Result<Json::Value> parseArguments(const std::string& json) {
  Json::CharReaderBuilder builder;
  std::string errors;
  Json::Value root;
  const std::unique_ptr<Json::CharReader> reader{builder.newCharReader()};
  if (!reader->parse(json.data(), json.data() + json.size(), &root, &errors)) {
    return argumentError("$", "Invalid JSON. " + errors);
  }
  if (!root.isObject()) {
    return argumentError("$", "Tool arguments must be a JSON object.");
  }
  return root;
}

foundation::Result<Json::Value> requiredMember(const Json::Value& object, const char* key, const std::string& path) {
  if (!object.isObject()) {
    return argumentError(path, "Expected object.");
  }
  if (!object.isMember(key)) {
    return argumentError(path + "." + key, "Missing required field.");
  }
  return object[key];
}

foundation::Result<std::string> requiredStringMember(const Json::Value& object, const char* key, const std::string& path) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (!value.value().isString()) {
    return argumentError(path + "." + key, "Expected string.");
  }
  return value.value().asString();
}

foundation::Result<double> requiredDoubleMember(const Json::Value& object, const char* key, const std::string& path) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (!value.value().isNumeric()) {
    return argumentError(path + "." + key, "Expected number.");
  }
  return value.value().asDouble();
}

foundation::Result<Json::Value> requiredArrayMember(const Json::Value& object, const char* key, const std::string& path) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (!value.value().isArray()) {
    return argumentError(path + "." + key, "Expected array.");
  }
  return value.value();
}

foundation::Result<foundation::TimeRange> parseTimeRange(const Json::Value& object, const std::string& path) {
  if (!object.isObject()) {
    return argumentError(path, "Expected time range object.");
  }
  auto start = requiredDoubleMember(object, "start", path);
  if (!start) {
    return start.error();
  }
  auto end = requiredDoubleMember(object, "end", path);
  if (!end) {
    return end.error();
  }
  return foundation::TimeRange{foundation::TimeSeconds{start.value()}, foundation::TimeSeconds{end.value()}};
}

foundation::Result<timeline::EffectImplementationKind> parseImplementationKind(const std::string& value, const std::string& path) {
  if (value == "builtin") {
    return timeline::EffectImplementationKind::Builtin;
  }
  if (value == "python") {
    return timeline::EffectImplementationKind::Python;
  }
  if (value == "shader") {
    return timeline::EffectImplementationKind::Shader;
  }
  return argumentError(path, "Expected builtin, python, or shader.");
}

foundation::Result<timeline::ClipKind> parseClipKind(const std::string& value, const std::string& path) {
  if (value == "video") {
    return timeline::ClipKind::Video;
  }
  if (value == "audio") {
    return timeline::ClipKind::Audio;
  }
  if (value == "image") {
    return timeline::ClipKind::Image;
  }
  return argumentError(path, "Expected video, audio, or image.");
}

foundation::Result<timeline::ParamValue> parseParamValue(const Json::Value& value, const std::string& path) {
  if (value.isNumeric()) {
    return timeline::ParamValue{value.asDouble()};
  }
  return argumentError(path, "Expected number.");
}

foundation::Result<timeline::ParamSet> parseParamSet(const Json::Value& array, const std::string& path) {
  if (!array.isArray()) {
    return argumentError(path, "Expected parameter array.");
  }
  if (array.empty()) {
    return argumentError(path, "Agent-created effects must expose at least one editable parameter.");
  }

  timeline::ParamSet params;
  for (Json::ArrayIndex index = 0; index < array.size(); ++index) {
    const std::string itemPath = path + "[" + std::to_string(index) + "]";
    if (!array[index].isObject()) {
      return argumentError(itemPath, "Expected parameter object.");
    }
    auto name = requiredStringMember(array[index], "name", itemPath);
    if (!name) {
      return name.error();
    }
    auto value = requiredMember(array[index], "value", itemPath);
    if (!value) {
      return value.error();
    }
    auto paramValue = parseParamValue(value.value(), itemPath + ".value");
    if (!paramValue) {
      return paramValue.error();
    }

    auto label = requiredStringMember(array[index], "label", itemPath);
    if (!label) {
      return label.error();
    }
    const Json::Value& numericObject = array[index]["numeric"];
    if (!numericObject.isObject()) {
      return argumentError(itemPath + ".numeric", "Expected numeric control object.");
    }
    auto min = requiredDoubleMember(numericObject, "min", itemPath + ".numeric");
    if (!min) {
      return min.error();
    }
    auto max = requiredDoubleMember(numericObject, "max", itemPath + ".numeric");
    if (!max) {
      return max.error();
    }
    timeline::Param::NumericControl numeric{min.value(), max.value(), std::nullopt};
    if (numericObject.isMember("step")) {
      auto step = requiredDoubleMember(numericObject, "step", itemPath + ".numeric");
      if (!step) {
        return step.error();
      }
      numeric.step = step.value();
    }
    timeline::Param::Control control{label.value(), numeric};
    params.values.push_back(timeline::Param{name.value(), paramValue.value(), control});
  }
  return params;
}

foundation::Result<std::vector<timeline::EffectPort>> parsePorts(const Json::Value& array, const std::string& path) {
  if (!array.isArray()) {
    return argumentError(path, "Expected port array.");
  }

  std::vector<timeline::EffectPort> ports;
  ports.reserve(array.size());
  for (Json::ArrayIndex index = 0; index < array.size(); ++index) {
    if (!array[index].isString()) {
      return argumentError(path + "[" + std::to_string(index) + "]", "Expected port name string.");
    }
    ports.push_back(timeline::EffectPort{array[index].asString()});
  }
  return ports;
}

foundation::Result<timeline::EffectPortSet> parsePortSet(const Json::Value& object, const std::string& path) {
  auto inputs = requiredArrayMember(object, "inputPorts", path);
  if (!inputs) {
    return inputs.error();
  }
  auto outputs = requiredArrayMember(object, "outputPorts", path);
  if (!outputs) {
    return outputs.error();
  }
  auto parsedInputs = parsePorts(inputs.value(), path + ".inputPorts");
  if (!parsedInputs) {
    return parsedInputs.error();
  }
  auto parsedOutputs = parsePorts(outputs.value(), path + ".outputPorts");
  if (!parsedOutputs) {
    return parsedOutputs.error();
  }
  return timeline::EffectPortSet{parsedInputs.value(), parsedOutputs.value()};
}

foundation::Result<project::ProjectSnapshot> readProjectSnapshot(
  AgentToolContext& context,
  std::string_view operation
) {
  auto query = context.queries.query(project::GetProjectSnapshotQuery{});
  if (!query) {
    return query.error();
  }
  const auto* snapshotResult = std::get_if<project::ProjectSnapshotResult>(&query.value());
  if (snapshotResult == nullptr) {
    return foundation::Error{
      "agent.project_snapshot_result_missing",
      std::string{operation} + " query returned the wrong result type."
    };
  }
  return snapshotResult->snapshot;
}

const char* assetMediaTypeText(asset::AssetMediaType mediaType) {
  switch (mediaType) {
    case asset::AssetMediaType::Video:
      return "video";
    case asset::AssetMediaType::Audio:
      return "audio";
    case asset::AssetMediaType::Image:
      return "image";
  }

  return "unknown";
}

void writeAssetJson(std::ostream& stream, const asset::Asset& asset) {
  stream << '{'
         << "\"assetId\":" << foundation::jsonQuoted(asset.id.value())
         << ",\"name\":" << foundation::jsonQuoted(asset.name)
         << ",\"mediaType\":" << foundation::jsonQuoted(assetMediaTypeText(asset.metadata.mediaType))
         << ",\"sourcePath\":" << foundation::jsonQuoted(asset.metadata.sourcePath.value);

  if (asset.metadata.thumbnailPath.has_value()) {
    stream << ",\"thumbnailPath\":" << foundation::jsonQuoted(asset.metadata.thumbnailPath->value);
  }
  if (asset.metadata.duration.has_value()) {
    stream << ",\"duration\":" << asset.metadata.duration->value;
  }
  if (asset.metadata.dimensions.has_value()) {
    stream << ",\"dimensions\":{\"width\":" << asset.metadata.dimensions->width
           << ",\"height\":" << asset.metadata.dimensions->height
           << '}';
  }
  if (asset.metadata.frameRate.has_value()) {
    stream << ",\"frameRate\":{\"numerator\":" << asset.metadata.frameRate->numerator
           << ",\"denominator\":" << asset.metadata.frameRate->denominator
           << '}';
  }

  stream << '}';
}

} // namespace

AgentTool makeProjectInspectTool() {
  return AgentTool{
    foundation::ToolId{"tool_project_inspect"},
    "project.inspect",
    "Inspect Project",
    "Returns the current project revision and graph counts.",
    ProjectInspectSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto snapshot = readProjectSnapshot(context, "Project inspect");
      if (!snapshot) {
        return snapshot.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"projectId\":" << foundation::jsonQuoted(snapshot.value().info.id.value())
              << ",\"revision\":" << foundation::jsonQuoted(snapshot.value().revision.value())
              << ",\"revisionNumber\":" << snapshot.value().revisionNumber
              << ",\"canonicalHash\":" << foundation::jsonQuoted(snapshot.value().canonicalHash.toHex())
              << ",\"graph\":{\"nodes\":" << snapshot.value().graph.nodes().size()
              << ",\"edges\":" << snapshot.value().graph.edges().size()
              << "},\"assets\":{\"count\":" << snapshot.value().assets.assets().size()
              << "}}";

      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        snapshot.value().revision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeAssetListTool() {
  return AgentTool{
    foundation::ToolId{"tool_asset_list"},
    "asset.list",
    "List Assets",
    "Lists project assets from the canonical project snapshot.",
    AssetListSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto snapshot = readProjectSnapshot(context, "Asset list");
      if (!snapshot) {
        return snapshot.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"revision\":" << foundation::jsonQuoted(snapshot.value().revision.value())
              << ",\"assets\":[";
      const std::vector<asset::Asset>& assets = snapshot.value().assets.assets();
      for (std::size_t index = 0; index < assets.size(); ++index) {
        if (index != 0) {
          payload << ',';
        }
        writeAssetJson(payload, assets[index]);
      }
      payload << "]}";

      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        snapshot.value().revision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeTimelineCreateTrackTool() {
  return AgentTool{
    foundation::ToolId{"tool_timeline_create_track"},
    "timeline.create_track",
    "Create Timeline Track",
    "Creates a timeline track in an explicit composition through Project Core.",
    TimelineCreateTrackSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto compositionNodeId = requiredStringMember(arguments.value(), "compositionNodeId", "$");
      if (!compositionNodeId) {
        return compositionNodeId.error();
      }
      auto name = requiredStringMember(arguments.value(), "name", "$");
      if (!name) {
        return name.error();
      }

      auto snapshot = readProjectSnapshot(context, "Create track");
      if (!snapshot) {
        return snapshot.error();
      }

      const std::int64_t nextRevisionNumber = snapshot.value().revisionNumber + 1;
      const foundation::CommandId commandId{"cmd_agent_create_track_rev_" + std::to_string(nextRevisionNumber)};
      const foundation::NodeId trackNodeId{"node_agent_track_rev_" + std::to_string(nextRevisionNumber)};
      const foundation::EdgeId containmentEdgeId{"edge_agent_track_contains_rev_" + std::to_string(nextRevisionNumber)};
      auto command = context.commands.apply(project::ProjectCommandEnvelope{
        commandId,
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::CreateTrackCommand{
          trackNodeId,
          foundation::NodeId{compositionNodeId.value()},
          containmentEdgeId,
          name.value(),
          0
        }
      });
      if (!command) {
        return command.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"commandId\":" << foundation::jsonQuoted(commandId.value())
              << ",\"trackNodeId\":" << foundation::jsonQuoted(trackNodeId.value())
              << ",\"containmentEdgeId\":" << foundation::jsonQuoted(containmentEdgeId.value())
              << ",\"compositionNodeId\":" << foundation::jsonQuoted(compositionNodeId.value())
              << ",\"revision\":" << foundation::jsonQuoted(command.value().afterRevision.value())
              << '}';
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        command.value().afterRevision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeTimelineCreateClipTool() {
  return AgentTool{
    foundation::ToolId{"tool_timeline_create_clip"},
    "timeline.create_clip",
    "Create Timeline Clip",
    "Creates a clip with explicit timing, media kind, asset, and track through Project Core.",
    TimelineCreateClipSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto trackNodeId = requiredStringMember(arguments.value(), "trackNodeId", "$");
      if (!trackNodeId) {
        return trackNodeId.error();
      }
      auto assetId = requiredStringMember(arguments.value(), "assetId", "$");
      if (!assetId) {
        return assetId.error();
      }
      auto kindName = requiredStringMember(arguments.value(), "kind", "$");
      if (!kindName) {
        return kindName.error();
      }
      auto kind = parseClipKind(kindName.value(), "$.kind");
      if (!kind) {
        return kind.error();
      }
      auto timelineRangeObject = requiredMember(arguments.value(), "timelineRange", "$");
      if (!timelineRangeObject) {
        return timelineRangeObject.error();
      }
      auto timelineRange = parseTimeRange(timelineRangeObject.value(), "$.timelineRange");
      if (!timelineRange) {
        return timelineRange.error();
      }
      auto sourceRangeObject = requiredMember(arguments.value(), "sourceRange", "$");
      if (!sourceRangeObject) {
        return sourceRangeObject.error();
      }
      auto sourceRange = parseTimeRange(sourceRangeObject.value(), "$.sourceRange");
      if (!sourceRange) {
        return sourceRange.error();
      }
      auto playbackRate = requiredDoubleMember(arguments.value(), "playbackRate", "$");
      if (!playbackRate) {
        return playbackRate.error();
      }

      auto snapshot = readProjectSnapshot(context, "Create clip");
      if (!snapshot) {
        return snapshot.error();
      }

      const std::int64_t nextRevisionNumber = snapshot.value().revisionNumber + 1;
      const foundation::CommandId commandId{"cmd_agent_create_clip_rev_" + std::to_string(nextRevisionNumber)};
      const foundation::NodeId clipNodeId{"node_agent_clip_rev_" + std::to_string(nextRevisionNumber)};
      const foundation::EdgeId containmentEdgeId{"edge_agent_clip_contains_rev_" + std::to_string(nextRevisionNumber)};
      auto command = context.commands.apply(project::ProjectCommandEnvelope{
        commandId,
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::CreateClipCommand{
          clipNodeId,
          foundation::NodeId{trackNodeId.value()},
          containmentEdgeId,
          timeline::ClipPayload{
            kind.value(),
            timelineRange.value(),
            sourceRange.value(),
            playbackRate.value(),
            foundation::AssetId{assetId.value()},
            timeline::Transform{}
          },
          0
        }
      });
      if (!command) {
        return command.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"commandId\":" << foundation::jsonQuoted(commandId.value())
              << ",\"clipNodeId\":" << foundation::jsonQuoted(clipNodeId.value())
              << ",\"containmentEdgeId\":" << foundation::jsonQuoted(containmentEdgeId.value())
              << ",\"trackNodeId\":" << foundation::jsonQuoted(trackNodeId.value())
              << ",\"assetId\":" << foundation::jsonQuoted(assetId.value())
              << ",\"revision\":" << foundation::jsonQuoted(command.value().afterRevision.value())
              << '}';
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        command.value().afterRevision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeTimelineMoveClipTool() {
  return AgentTool{
    foundation::ToolId{"tool_timeline_move_clip"},
    "timeline.move_clip",
    "Move Timeline Clip",
    "Moves a clip to an explicit timeline start through Project Core.",
    TimelineMoveClipSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto clipNodeId = requiredStringMember(arguments.value(), "clipNodeId", "$");
      if (!clipNodeId) {
        return clipNodeId.error();
      }
      auto newStart = requiredDoubleMember(arguments.value(), "newStart", "$");
      if (!newStart) {
        return newStart.error();
      }

      auto snapshot = readProjectSnapshot(context, "Move clip");
      if (!snapshot) {
        return snapshot.error();
      }

      const std::int64_t nextRevisionNumber = snapshot.value().revisionNumber + 1;
      const foundation::CommandId commandId{"cmd_agent_move_clip_rev_" + std::to_string(nextRevisionNumber)};
      auto command = context.commands.apply(project::ProjectCommandEnvelope{
        commandId,
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::MoveClipCommand{
          foundation::NodeId{clipNodeId.value()},
          foundation::TimeSeconds{newStart.value()}
        }
      });
      if (!command) {
        return command.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"commandId\":" << foundation::jsonQuoted(commandId.value())
              << ",\"clipNodeId\":" << foundation::jsonQuoted(clipNodeId.value())
              << ",\"newStart\":" << newStart.value()
              << ",\"revision\":" << foundation::jsonQuoted(command.value().afterRevision.value())
              << '}';
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        command.value().afterRevision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeTimelineTrimClipTool() {
  return AgentTool{
    foundation::ToolId{"tool_timeline_trim_clip"},
    "timeline.trim_clip",
    "Trim Timeline Clip",
    "Trims a clip to explicit timeline and source ranges through Project Core.",
    TimelineTrimClipSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto clipNodeId = requiredStringMember(arguments.value(), "clipNodeId", "$");
      if (!clipNodeId) {
        return clipNodeId.error();
      }
      auto timelineRangeObject = requiredMember(arguments.value(), "timelineRange", "$");
      if (!timelineRangeObject) {
        return timelineRangeObject.error();
      }
      auto timelineRange = parseTimeRange(timelineRangeObject.value(), "$.timelineRange");
      if (!timelineRange) {
        return timelineRange.error();
      }
      auto sourceRangeObject = requiredMember(arguments.value(), "sourceRange", "$");
      if (!sourceRangeObject) {
        return sourceRangeObject.error();
      }
      auto sourceRange = parseTimeRange(sourceRangeObject.value(), "$.sourceRange");
      if (!sourceRange) {
        return sourceRange.error();
      }

      auto snapshot = readProjectSnapshot(context, "Trim clip");
      if (!snapshot) {
        return snapshot.error();
      }

      const std::int64_t nextRevisionNumber = snapshot.value().revisionNumber + 1;
      const foundation::CommandId commandId{"cmd_agent_trim_clip_rev_" + std::to_string(nextRevisionNumber)};
      auto command = context.commands.apply(project::ProjectCommandEnvelope{
        commandId,
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::TrimClipCommand{
          foundation::NodeId{clipNodeId.value()},
          timelineRange.value(),
          sourceRange.value()
        }
      });
      if (!command) {
        return command.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"commandId\":" << foundation::jsonQuoted(commandId.value())
              << ",\"clipNodeId\":" << foundation::jsonQuoted(clipNodeId.value())
              << ",\"revision\":" << foundation::jsonQuoted(command.value().afterRevision.value())
              << '}';
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        command.value().afterRevision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeEffectCreateNodeTool() {
  return AgentTool{
    foundation::ToolId{"tool_effect_create_node"},
    "effect.create_node",
    "Create Effect Node",
    "Creates an editable effect node with canonical code, ports, params, and user-facing parameter controls. The tool allocates project ids; agents provide edit intent and payload data only.",
    EffectCreateNodeSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }

      auto snapshot = readProjectSnapshot(context, "Create effect");
      if (!snapshot) {
        return snapshot.error();
      }
      const std::int64_t nextRevisionNumber = snapshot.value().revisionNumber + 1;
      const foundation::CommandId commandId{"cmd_agent_create_effect_rev_" + std::to_string(nextRevisionNumber)};
      const foundation::NodeId effectNodeId{"node_agent_effect_rev_" + std::to_string(nextRevisionNumber)};
      const foundation::EdgeId targetEdgeId{"edge_agent_effect_targets_rev_" + std::to_string(nextRevisionNumber)};

      auto targetNodeId = requiredStringMember(arguments.value(), "targetNodeId", "$");
      if (!targetNodeId) {
        return targetNodeId.error();
      }
      auto displayName = requiredStringMember(arguments.value(), "displayName", "$");
      if (!displayName) {
        return displayName.error();
      }
      auto implementationKindName = requiredStringMember(arguments.value(), "implementationKind", "$");
      if (!implementationKindName) {
        return implementationKindName.error();
      }
      auto implementationKind = parseImplementationKind(implementationKindName.value(), "$.implementationKind");
      if (!implementationKind) {
        return implementationKind.error();
      }
      auto language = requiredStringMember(arguments.value(), "language", "$");
      if (!language) {
        return language.error();
      }
      auto entrypoint = requiredStringMember(arguments.value(), "entrypoint", "$");
      if (!entrypoint) {
        return entrypoint.error();
      }
      auto source = requiredStringMember(arguments.value(), "source", "$");
      if (!source) {
        return source.error();
      }
      auto sourcePort = requiredStringMember(arguments.value(), "sourcePort", "$");
      if (!sourcePort) {
        return sourcePort.error();
      }
      auto targetPort = requiredStringMember(arguments.value(), "targetPort", "$");
      if (!targetPort) {
        return targetPort.error();
      }
      auto activeRangeObject = requiredMember(arguments.value(), "activeRange", "$");
      if (!activeRangeObject) {
        return activeRangeObject.error();
      }
      auto activeRange = parseTimeRange(activeRangeObject.value(), "$.activeRange");
      if (!activeRange) {
        return activeRange.error();
      }
      auto ports = parsePortSet(arguments.value(), "$");
      if (!ports) {
        return ports.error();
      }
      auto paramsArray = requiredArrayMember(arguments.value(), "params", "$");
      if (!paramsArray) {
        return paramsArray.error();
      }
      auto params = parseParamSet(paramsArray.value(), "$.params");
      if (!params) {
        return params.error();
      }

      auto command = context.commands.apply(project::ProjectCommandEnvelope{
        commandId,
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::CreateEffectCommand{
          effectNodeId,
          foundation::NodeId{targetNodeId.value()},
          targetEdgeId,
          timeline::EffectPayload{
            displayName.value(),
            timeline::EffectImplementation{
              implementationKind.value(),
              entrypoint.value(),
              timeline::EffectSource{
                timeline::EffectSourceKind::InlineSource,
                language.value(),
                source.value(),
                std::nullopt,
                foundation::stableHash(source.value())
              }
            },
            ports.value(),
            params.value(),
            activeRange.value()
          },
          graph::PortName{sourcePort.value()},
          graph::PortName{targetPort.value()},
          0
        }
      });
      if (!command) {
        return command.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"commandId\":" << foundation::jsonQuoted(commandId.value())
              << ",\"effectNodeId\":" << foundation::jsonQuoted(effectNodeId.value())
              << ",\"targetEdgeId\":" << foundation::jsonQuoted(targetEdgeId.value())
              << ",\"targetNodeId\":" << foundation::jsonQuoted(targetNodeId.value())
              << ",\"revision\":" << foundation::jsonQuoted(command.value().afterRevision.value())
              << '}';
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        command.value().afterRevision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeNoteCreateTool() {
  return AgentTool{
    foundation::ToolId{"tool_note_create"},
    "note.create",
    "Create Note",
    "Creates a project note through Project Core.",
    NoteCreateSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto title = requiredStringMember(arguments.value(), "title", "$");
      if (!title) {
        return title.error();
      }
      auto markdown = requiredStringMember(arguments.value(), "markdown", "$");
      if (!markdown) {
        return markdown.error();
      }

      auto snapshot = readProjectSnapshot(context, "Create note");
      if (!snapshot) {
        return snapshot.error();
      }

      const std::int64_t nextRevisionNumber = snapshot.value().revisionNumber + 1;
      const foundation::CommandId commandId{"cmd_agent_create_note_rev_" + std::to_string(nextRevisionNumber)};
      const foundation::NodeId noteNodeId{"node_agent_note_rev_" + std::to_string(nextRevisionNumber)};
      auto command = context.commands.apply(project::ProjectCommandEnvelope{
        commandId,
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::CreateNoteCommand{
          noteNodeId,
          timeline::NotePayload{title.value(), markdown.value()}
        }
      });
      if (!command) {
        return command.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"commandId\":" << foundation::jsonQuoted(commandId.value())
              << ",\"noteNodeId\":" << foundation::jsonQuoted(noteNodeId.value())
              << ",\"revision\":" << foundation::jsonQuoted(command.value().afterRevision.value())
              << '}';
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        command.value().afterRevision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeNoteUpdateTool() {
  return AgentTool{
    foundation::ToolId{"tool_note_update"},
    "note.update",
    "Update Note",
    "Updates an existing project note through Project Core.",
    NoteUpdateSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto nodeId = requiredStringMember(arguments.value(), "nodeId", "$");
      if (!nodeId) {
        return nodeId.error();
      }
      auto title = requiredStringMember(arguments.value(), "title", "$");
      if (!title) {
        return title.error();
      }
      auto markdown = requiredStringMember(arguments.value(), "markdown", "$");
      if (!markdown) {
        return markdown.error();
      }

      auto snapshot = readProjectSnapshot(context, "Update note");
      if (!snapshot) {
        return snapshot.error();
      }

      const std::int64_t nextRevisionNumber = snapshot.value().revisionNumber + 1;
      const foundation::CommandId commandId{"cmd_agent_update_note_rev_" + std::to_string(nextRevisionNumber)};
      const foundation::NodeId noteNodeId{nodeId.value()};
      auto command = context.commands.apply(project::ProjectCommandEnvelope{
        commandId,
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::UpdateNoteCommand{
          noteNodeId,
          timeline::NotePayload{title.value(), markdown.value()}
        }
      });
      if (!command) {
        return command.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"commandId\":" << foundation::jsonQuoted(commandId.value())
              << ",\"noteNodeId\":" << foundation::jsonQuoted(noteNodeId.value())
              << ",\"revision\":" << foundation::jsonQuoted(command.value().afterRevision.value())
              << '}';
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        command.value().afterRevision,
        payload.str(),
        {}
      };
    }
  };
}

} // namespace grapple::agent
