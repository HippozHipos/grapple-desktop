#include <grapple/agent/ProjectTools.hpp>

#include <grapple/foundation/Json.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/project/ProjectCommand.hpp>
#include <grapple/project/ProjectQuery.hpp>

#include <json/json.h>

#include <memory>
#include <optional>
#include <sstream>
#include <string>

namespace grapple::agent {

namespace {

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

foundation::Result<timeline::ParamValue> parseParamValue(const Json::Value& value, const std::string& path) {
  if (value.isBool()) {
    return timeline::ParamValue{value.asBool()};
  }
  if (value.isString()) {
    return timeline::ParamValue{value.asString()};
  }
  if (value.isNumeric()) {
    return timeline::ParamValue{value.asDouble()};
  }
  return argumentError(path, "Expected boolean, string, or number.");
}

foundation::Result<timeline::ParamSet> parseParamSet(const Json::Value& array, const std::string& path) {
  if (!array.isArray()) {
    return argumentError(path, "Expected parameter array.");
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

    timeline::Param::Control control;
    if (array[index].isMember("label")) {
      auto label = requiredStringMember(array[index], "label", itemPath);
      if (!label) {
        return label.error();
      }
      control.label = label.value();
    }
    if (array[index].isMember("numeric")) {
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
      control.numeric = numeric;
    }
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

} // namespace

AgentTool makeProjectInspectTool() {
  return AgentTool{
    foundation::ToolId{"tool_project_inspect"},
    "project.inspect",
    "Inspect Project",
    "Returns the current project revision and graph counts.",
    "ProjectInspectRequest",
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto query = context.queries.query(project::GetProjectSnapshotQuery{});
      if (!query) {
        return query.error();
      }

      const auto* snapshotResult = std::get_if<project::ProjectSnapshotResult>(&query.value());
      if (snapshotResult == nullptr) {
        return foundation::Error{"agent.project_snapshot_result_missing", "Project inspect query returned the wrong result type."};
      }

      const project::ProjectSnapshot& snapshot = snapshotResult->snapshot;
      std::ostringstream payload;
      payload << '{'
              << "\"projectId\":" << foundation::jsonQuoted(snapshot.info.id.value())
              << ",\"revision\":" << foundation::jsonQuoted(snapshot.revision.value())
              << ",\"revisionNumber\":" << snapshot.revisionNumber
              << ",\"canonicalHash\":" << foundation::jsonQuoted(snapshot.canonicalHash.toHex())
              << ",\"graph\":{\"nodes\":" << snapshot.graph.nodes().size()
              << ",\"edges\":" << snapshot.graph.edges().size()
              << "},\"assets\":{\"count\":" << snapshot.assets.assets().size()
              << "}}";

      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        snapshot.revision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeProjectCreateEffectTool() {
  return AgentTool{
    foundation::ToolId{"tool_project_create_effect"},
    "project.create_effect",
    "Create Effect",
    "Creates an editable effect node with canonical code, ports, params, and user-facing parameter controls.",
    "ProjectCreateEffectRequest",
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }

      auto commandId = requiredStringMember(arguments.value(), "commandId", "$");
      if (!commandId) {
        return commandId.error();
      }
      auto effectNodeId = requiredStringMember(arguments.value(), "effectNodeId", "$");
      if (!effectNodeId) {
        return effectNodeId.error();
      }
      auto targetNodeId = requiredStringMember(arguments.value(), "targetNodeId", "$");
      if (!targetNodeId) {
        return targetNodeId.error();
      }
      auto targetEdgeId = requiredStringMember(arguments.value(), "targetEdgeId", "$");
      if (!targetEdgeId) {
        return targetEdgeId.error();
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
        foundation::CommandId{commandId.value()},
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::CreateEffectCommand{
          foundation::NodeId{effectNodeId.value()},
          foundation::NodeId{targetNodeId.value()},
          foundation::EdgeId{targetEdgeId.value()},
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
              << "\"effectNodeId\":" << foundation::jsonQuoted(effectNodeId.value())
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

} // namespace grapple::agent
