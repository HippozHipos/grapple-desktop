#include <grapple/agent/AgentRunSerializer.hpp>

#include <grapple/foundation/Json.hpp>

#include <json/json.h>

#include <chrono>
#include <memory>
#include <sstream>
#include <string>

namespace grapple::agent {

namespace {

std::int64_t millisecondsSinceEpoch(std::chrono::system_clock::time_point time) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}

std::chrono::system_clock::time_point timePointFromMilliseconds(std::int64_t milliseconds) {
  return std::chrono::system_clock::time_point{std::chrono::milliseconds{milliseconds}};
}

const char* serializedAgentRunStatus(AgentRunStatus status) {
  switch (status) {
    case AgentRunStatus::Pending:
      return "pending";
    case AgentRunStatus::Running:
      return "running";
    case AgentRunStatus::Succeeded:
      return "succeeded";
    case AgentRunStatus::Failed:
      return "failed";
  }

  return "failed";
}

foundation::Result<AgentRunStatus> parseAgentRunStatus(const std::string& status) {
  if (status == "pending") {
    return AgentRunStatus::Pending;
  }
  if (status == "running") {
    return AgentRunStatus::Running;
  }
  if (status == "succeeded") {
    return AgentRunStatus::Succeeded;
  }
  if (status == "failed") {
    return AgentRunStatus::Failed;
  }
  return foundation::Error{"agent.run_status_invalid", "Unknown agent run status."};
}

foundation::Error parseError(const std::string& path, const std::string& message) {
  return foundation::Error{"agent.run_log_json_invalid", path + ": " + message};
}

foundation::Result<Json::Value> parseJson(const std::string& json) {
  Json::CharReaderBuilder builder;
  std::string errors;
  Json::Value root;
  const std::unique_ptr<Json::CharReader> reader{builder.newCharReader()};
  if (!reader->parse(json.data(), json.data() + json.size(), &root, &errors)) {
    return parseError("$.agentRuns", "Invalid JSON. " + errors);
  }
  if (!root.isArray()) {
    return parseError("$.agentRuns", "Expected JSON array.");
  }
  return root;
}

foundation::Result<Json::Value> requiredMember(const Json::Value& object, const char* key, const std::string& path) {
  if (!object.isObject()) {
    return parseError(path, "Expected object.");
  }
  if (!object.isMember(key)) {
    return parseError(path + "." + key, "Missing required field.");
  }
  return object[key];
}

foundation::Result<std::string> requiredStringMember(const Json::Value& object, const char* key, const std::string& path) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (!value.value().isString()) {
    return parseError(path + "." + key, "Expected string.");
  }
  return value.value().asString();
}

foundation::Result<std::int64_t> requiredInt64Member(const Json::Value& object, const char* key, const std::string& path) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (!value.value().isIntegral()) {
    return parseError(path + "." + key, "Expected integer.");
  }
  return value.value().asInt64();
}

foundation::Result<std::optional<foundation::RunId>> optionalRunIdMember(
  const Json::Value& object,
  const char* key,
  const std::string& path
) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (value.value().isNull()) {
    return std::optional<foundation::RunId>{};
  }
  if (!value.value().isString()) {
    return parseError(path + "." + key, "Expected string or null.");
  }
  return std::optional<foundation::RunId>{foundation::RunId{value.value().asString()}};
}

foundation::Result<AgentRun> parseAgentRun(const Json::Value& object, const std::string& path) {
  auto id = requiredStringMember(object, "id", path);
  if (!id) {
    return id.error();
  }
  auto projectId = requiredStringMember(object, "projectId", path);
  if (!projectId) {
    return projectId.error();
  }
  auto parentRunId = optionalRunIdMember(object, "parentRunId", path);
  if (!parentRunId) {
    return parentRunId.error();
  }
  auto statusName = requiredStringMember(object, "status", path);
  if (!statusName) {
    return statusName.error();
  }
  auto status = parseAgentRunStatus(statusName.value());
  if (!status) {
    return status.error();
  }
  auto createdAtMs = requiredInt64Member(object, "createdAtMs", path);
  if (!createdAtMs) {
    return createdAtMs.error();
  }

  return AgentRun{
    foundation::RunId{id.value()},
    foundation::ProjectId{projectId.value()},
    parentRunId.value(),
    status.value(),
    timePointFromMilliseconds(createdAtMs.value())
  };
}

void writeOptionalRunId(std::ostringstream& stream, const std::optional<foundation::RunId>& runId) {
  if (runId.has_value()) {
    stream << foundation::jsonQuoted(runId->value());
  } else {
    stream << "null";
  }
}

} // namespace

std::string serializeCanonicalAgentRuns(std::span<const AgentRun> runs) {
  std::ostringstream stream;
  stream << '[';
  for (std::size_t index = 0; index < runs.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    const AgentRun& run = runs[index];
    stream << '{';
    foundation::writeJsonStringProperty(stream, "id", run.id.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "projectId", run.projectId.value());
    stream << ",\"parentRunId\":";
    writeOptionalRunId(stream, run.parentRunId);
    stream << ',';
    foundation::writeJsonStringProperty(stream, "status", serializedAgentRunStatus(run.status));
    stream << ",\"createdAtMs\":" << millisecondsSinceEpoch(run.createdAt);
    stream << '}';
  }
  stream << ']';
  return stream.str();
}

foundation::Result<std::vector<AgentRun>> deserializeCanonicalAgentRuns(const std::string& json) {
  auto root = parseJson(json);
  if (!root) {
    return root.error();
  }

  std::vector<AgentRun> runs;
  runs.reserve(root.value().size());
  for (Json::ArrayIndex index = 0; index < root.value().size(); ++index) {
    const std::string itemPath = "$.agentRuns[" + std::to_string(index) + "]";
    auto run = parseAgentRun(root.value()[index], itemPath);
    if (!run) {
      return run.error();
    }
    runs.push_back(run.value());
  }
  return runs;
}

} // namespace grapple::agent
