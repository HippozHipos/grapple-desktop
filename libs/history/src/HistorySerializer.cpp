#include <grapple/history/HistorySerializer.hpp>

#include <grapple/foundation/Json.hpp>

#include <json/json.h>

#include <chrono>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

namespace grapple::history {

namespace {

std::int64_t millisecondsSinceEpoch(std::chrono::system_clock::time_point time) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}

std::chrono::system_clock::time_point timePointFromMilliseconds(std::int64_t milliseconds) {
  return std::chrono::system_clock::time_point{std::chrono::milliseconds{milliseconds}};
}

void writeOptionalRunId(std::ostringstream& stream, const std::optional<foundation::RunId>& runId) {
  if (runId.has_value()) {
    stream << foundation::jsonQuoted(runId->value());
  } else {
    stream << "null";
  }
}

foundation::Error parseError(const std::string& path, const std::string& message) {
  return foundation::Error{"history.log_json_invalid", path + ": " + message};
}

foundation::Result<Json::Value> parseJson(const std::string& json, const std::string& path) {
  Json::CharReaderBuilder builder;
  std::string errors;
  Json::Value root;
  const std::unique_ptr<Json::CharReader> reader{builder.newCharReader()};
  if (!reader->parse(json.data(), json.data() + json.size(), &root, &errors)) {
    return parseError(path, "Invalid JSON. " + errors);
  }
  if (!root.isArray()) {
    return parseError(path, "Expected JSON array.");
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

foundation::Result<CommandRecord> parseCommandRecord(const Json::Value& object, const std::string& path) {
  auto id = requiredStringMember(object, "id", path);
  if (!id) {
    return id.error();
  }
  auto projectId = requiredStringMember(object, "projectId", path);
  if (!projectId) {
    return projectId.error();
  }
  auto beforeRevision = requiredStringMember(object, "beforeRevision", path);
  if (!beforeRevision) {
    return beforeRevision.error();
  }
  auto afterRevision = requiredStringMember(object, "afterRevision", path);
  if (!afterRevision) {
    return afterRevision.error();
  }
  auto serializedName = requiredStringMember(object, "serializedName", path);
  if (!serializedName) {
    return serializedName.error();
  }
  auto serializedPayload = requiredStringMember(object, "serializedPayload", path);
  if (!serializedPayload) {
    return serializedPayload.error();
  }
  auto sourceKind = requiredStringMember(object, "sourceKind", path);
  if (!sourceKind) {
    return sourceKind.error();
  }
  auto sourceRunId = optionalRunIdMember(object, "sourceRunId", path);
  if (!sourceRunId) {
    return sourceRunId.error();
  }
  auto sourceActorName = requiredStringMember(object, "sourceActorName", path);
  if (!sourceActorName) {
    return sourceActorName.error();
  }
  auto createdAtMs = requiredInt64Member(object, "createdAtMs", path);
  if (!createdAtMs) {
    return createdAtMs.error();
  }

  return CommandRecord{
    foundation::CommandId{id.value()},
    foundation::ProjectId{projectId.value()},
    foundation::RevisionId{beforeRevision.value()},
    foundation::RevisionId{afterRevision.value()},
    serializedName.value(),
    serializedPayload.value(),
    sourceKind.value(),
    sourceRunId.value(),
    sourceActorName.value(),
    timePointFromMilliseconds(createdAtMs.value())
  };
}

foundation::Result<EventRecord> parseEventRecord(const Json::Value& object, const std::string& path) {
  auto id = requiredStringMember(object, "id", path);
  if (!id) {
    return id.error();
  }
  auto projectId = requiredStringMember(object, "projectId", path);
  if (!projectId) {
    return projectId.error();
  }
  auto revision = requiredStringMember(object, "revision", path);
  if (!revision) {
    return revision.error();
  }
  auto serializedName = requiredStringMember(object, "serializedName", path);
  if (!serializedName) {
    return serializedName.error();
  }
  auto serializedPayload = requiredStringMember(object, "serializedPayload", path);
  if (!serializedPayload) {
    return serializedPayload.error();
  }
  auto createdAtMs = requiredInt64Member(object, "createdAtMs", path);
  if (!createdAtMs) {
    return createdAtMs.error();
  }

  return EventRecord{
    foundation::EventId{id.value()},
    foundation::ProjectId{projectId.value()},
    foundation::RevisionId{revision.value()},
    serializedName.value(),
    serializedPayload.value(),
    timePointFromMilliseconds(createdAtMs.value())
  };
}

} // namespace

std::string serializeCanonicalCommandLog(const CommandLogStore& log) {
  std::ostringstream stream;
  stream << '[';
  const std::vector<CommandRecord>& records = log.records();
  for (std::size_t index = 0; index < records.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    const CommandRecord& record = records[index];
    stream << '{';
    foundation::writeJsonStringProperty(stream, "id", record.id.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "projectId", record.projectId.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "beforeRevision", record.beforeRevision.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "afterRevision", record.afterRevision.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "serializedName", record.serializedName);
    stream << ',';
    foundation::writeJsonStringProperty(stream, "serializedPayload", record.serializedPayload);
    stream << ',';
    foundation::writeJsonStringProperty(stream, "sourceKind", record.sourceKind);
    stream << ",\"sourceRunId\":";
    writeOptionalRunId(stream, record.sourceRunId);
    stream << ',';
    foundation::writeJsonStringProperty(stream, "sourceActorName", record.sourceActorName);
    stream << ",\"createdAtMs\":" << millisecondsSinceEpoch(record.createdAt);
    stream << '}';
  }
  stream << ']';
  return stream.str();
}

std::string serializeCanonicalEventLog(const EventLogStore& log) {
  std::ostringstream stream;
  stream << '[';
  const std::vector<EventRecord>& records = log.records();
  for (std::size_t index = 0; index < records.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    const EventRecord& record = records[index];
    stream << '{';
    foundation::writeJsonStringProperty(stream, "id", record.id.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "projectId", record.projectId.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "revision", record.revision.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "serializedName", record.serializedName);
    stream << ',';
    foundation::writeJsonStringProperty(stream, "serializedPayload", record.serializedPayload);
    stream << ",\"createdAtMs\":" << millisecondsSinceEpoch(record.createdAt);
    stream << '}';
  }
  stream << ']';
  return stream.str();
}

foundation::Result<CommandLogStore> deserializeCanonicalCommandLog(const std::string& json) {
  auto root = parseJson(json, "$.commands");
  if (!root) {
    return root.error();
  }

  CommandLogStore log;
  for (Json::ArrayIndex index = 0; index < root.value().size(); ++index) {
    const std::string itemPath = "$.commands[" + std::to_string(index) + "]";
    auto record = parseCommandRecord(root.value()[index], itemPath);
    if (!record) {
      return record.error();
    }
    auto appended = log.append(record.value());
    if (!appended) {
      return parseError(itemPath, appended.error().message);
    }
  }
  return log;
}

foundation::Result<EventLogStore> deserializeCanonicalEventLog(const std::string& json) {
  auto root = parseJson(json, "$.events");
  if (!root) {
    return root.error();
  }

  EventLogStore log;
  for (Json::ArrayIndex index = 0; index < root.value().size(); ++index) {
    const std::string itemPath = "$.events[" + std::to_string(index) + "]";
    auto record = parseEventRecord(root.value()[index], itemPath);
    if (!record) {
      return record.error();
    }
    auto appended = log.append(record.value());
    if (!appended) {
      return parseError(itemPath, appended.error().message);
    }
  }
  return log;
}

} // namespace grapple::history
