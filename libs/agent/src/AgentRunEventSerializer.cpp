#include <grapple/agent/AgentRunEventSerializer.hpp>

#include <grapple/agent/AgentRunEventLog.hpp>
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

foundation::Error parseError(const std::string& path, const std::string& message) {
  return foundation::Error{"agent.run_event_log_json_invalid", path + ": " + message};
}

foundation::Result<Json::Value> parseJson(const std::string& json) {
  Json::CharReaderBuilder builder;
  std::string errors;
  Json::Value root;
  const std::unique_ptr<Json::CharReader> reader{builder.newCharReader()};
  if (!reader->parse(json.data(), json.data() + json.size(), &root, &errors)) {
    return parseError("$.agentRunEvents", "Invalid JSON. " + errors);
  }
  if (!root.isArray()) {
    return parseError("$.agentRunEvents", "Expected JSON array.");
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

foundation::Result<AgentRunEvent> parseAgentRunEvent(const Json::Value& object, const std::string& path) {
  auto runId = requiredStringMember(object, "runId", path);
  if (!runId) {
    return runId.error();
  }
  auto sequence = requiredInt64Member(object, "sequence", path);
  if (!sequence) {
    return sequence.error();
  }
  auto kindName = requiredStringMember(object, "kind", path);
  if (!kindName) {
    return kindName.error();
  }
  auto kind = parseAgentRunEventKind(kindName.value());
  if (!kind) {
    return kind.error();
  }
  auto payloadJson = requiredStringMember(object, "payloadJson", path);
  if (!payloadJson) {
    return payloadJson.error();
  }
  auto createdAtMs = requiredInt64Member(object, "createdAtMs", path);
  if (!createdAtMs) {
    return createdAtMs.error();
  }

  return AgentRunEvent{
    foundation::RunId{runId.value()},
    sequence.value(),
    kind.value(),
    payloadJson.value(),
    timePointFromMilliseconds(createdAtMs.value())
  };
}

} // namespace

std::string serializeCanonicalAgentRunEvents(std::span<const AgentRunEvent> events) {
  std::ostringstream stream;
  stream << '[';
  for (std::size_t index = 0; index < events.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    const AgentRunEvent& event = events[index];
    stream << '{';
    foundation::writeJsonStringProperty(stream, "runId", event.runId.value());
    stream << ",\"sequence\":" << event.sequence;
    stream << ',';
    foundation::writeJsonStringProperty(stream, "kind", serializedAgentRunEventKind(event.kind));
    stream << ',';
    foundation::writeJsonStringProperty(stream, "payloadJson", event.payloadJson);
    stream << ",\"createdAtMs\":" << millisecondsSinceEpoch(event.createdAt);
    stream << '}';
  }
  stream << ']';
  return stream.str();
}

foundation::Result<std::vector<AgentRunEvent>> deserializeCanonicalAgentRunEvents(const std::string& json) {
  auto root = parseJson(json);
  if (!root) {
    return root.error();
  }

  AgentRunEventLog log;
  for (Json::ArrayIndex index = 0; index < root.value().size(); ++index) {
    const std::string itemPath = "$.agentRunEvents[" + std::to_string(index) + "]";
    auto event = parseAgentRunEvent(root.value()[index], itemPath);
    if (!event) {
      return event.error();
    }
    auto appended = log.append(event.value());
    if (!appended) {
      return parseError(itemPath, appended.error().message);
    }
  }
  return log.records();
}

} // namespace grapple::agent
